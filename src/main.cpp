#include <Arduino.h>
#include <esp_mac.h>

#include "debug_api.h"
#include "sensor_battery.h"
#include "sensor_inputs.h"
#include "sensor_net.h"
#include "sensor_power.h"
#include "wifi6_sensor_config.h"

namespace {

char g_device_id[32];
bool g_publish_all = true;
uint32_t g_pending_mask = 0;        // inputs whose change is not yet published
uint32_t g_latched_active_mask = 0; // inputs that went active since the last publish
uint32_t g_awake_since_ms = 0;
uint32_t g_last_event_ms = 0;
uint32_t g_last_full_publish_ms = 0;
uint32_t g_boot_awake_until_ms = 0;

void build_device_id()
{
    if (sizeof(CFG_DEVICE_ID) > 1) {
        strlcpy(g_device_id, CFG_DEVICE_ID, sizeof(g_device_id));
        return;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_device_id, sizeof(g_device_id), "xiaoc5_%02x%02x%02x", mac[3], mac[4], mac[5]);
}

void measure_battery()
{
#if CFG_BATTERY_ENABLED
    battery_measure();
    debug_log("BAT %lu mV (%u %%)", static_cast<unsigned long>(battery_mv()), battery_percent());
#endif
}

void handle_input_events()
{
    SensorInputEvent event;
    while (inputs_poll_event(&event)) {
        debug_log("INPUT %s -> %s", inputs_name(event.id), inputs_state_name(event.id, event.active));
        g_pending_mask |= 1UL << event.id;
        if (event.active) {
            g_latched_active_mask |= 1UL << event.id;
        }
        g_last_event_ms = millis();
    }
}

void publish_pending()
{
    if (net_mqtt_just_connected()) {
        g_publish_all = true;
    }
    if (!net_mqtt_connected()) {
        return;
    }

    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        const uint32_t bit = 1UL << i;
        if (!(g_pending_mask & bit)) {
            continue;
        }
        const bool state = inputs_get_state(id);
        // Short pulses (e.g. motion while reconnecting) are still reported as active -> inactive.
        if ((g_latched_active_mask & bit) && !state && !net_publish_input(id, true)) {
            return;
        }
        if (!net_publish_input(id, state)) {
            return;
        }
        g_pending_mask &= ~bit;
        g_latched_active_mask &= ~bit;
    }

    const uint32_t now = millis();
#if CFG_SLEEP_MODE == SLEEP_MODE_NONE
    if (now - g_last_full_publish_ms >= CFG_HEARTBEAT_S * 1000UL) {
        measure_battery();
        g_publish_all = true;
    }
#endif
    if (g_publish_all && net_publish_all()) {
        g_publish_all = false;
        g_last_full_publish_ms = now;
    }
}

void maybe_sleep()
{
#if CFG_SLEEP_MODE != SLEEP_MODE_NONE
    const uint32_t now = millis();
    if (debug_stay_awake() || static_cast<int32_t>(now - g_boot_awake_until_ms) < 0) {
        return;
    }
    if (now - g_last_event_ms < CFG_AWAKE_AFTER_EVENT_MS) {
        return;
    }
    const bool work_done = !g_publish_all && g_pending_mask == 0;
    const bool timed_out = now - g_awake_since_ms >= CFG_MAX_AWAKE_MS;
    if (!(work_done && inputs_is_settled()) && !timed_out) {
        return;
    }

    debug_log("SLEEP %s for up to %u s%s", power_sleep_mode_name(), CFG_HEARTBEAT_S,
              work_done ? "" : " (awake timeout, unpublished data)");
    net_stop();

#if CFG_SLEEP_MODE == SLEEP_MODE_LIGHT
    const WakeReason reason = power_light_sleep(CFG_HEARTBEAT_S);
    g_awake_since_ms = millis();
    g_publish_all = true;
    debug_log("WAKE reason=%s", power_wake_reason_name(reason));
    measure_battery();
    net_start();
#else
    power_deep_sleep(CFG_HEARTBEAT_S);
#endif
#endif
}

}  // namespace

void setup()
{
    debug_init();
    power_init();
    build_device_id();
    inputs_init();
    battery_init();

    debug_log("BOOT %s fw=%s id=%s wake=%s boot=%lu sleep=%s", CFG_DEVICE_NAME, CFG_FW_VERSION, g_device_id,
              power_wake_reason_name(power_last_wake_reason()), static_cast<unsigned long>(power_boot_count()),
              power_sleep_mode_name());

    g_awake_since_ms = millis();
    g_last_event_ms = g_awake_since_ms;
    if (power_last_wake_reason() == WAKE_RESET) {
        g_boot_awake_until_ms = g_awake_since_ms + CFG_AWAKE_AFTER_BOOT_MS;
    }

    // Measure before the radio starts: no TX current sag on the battery.
    measure_battery();
    net_init(g_device_id);
    net_start();
}

void loop()
{
    debug_loop();
    handle_input_events();
    net_loop();
    publish_pending();
    maybe_sleep();
    delay(1);
}