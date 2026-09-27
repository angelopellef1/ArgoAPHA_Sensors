#include "debug_api.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <stdarg.h>

#include "sensor_battery.h"
#include "sensor_inputs.h"
#include "sensor_net.h"
#include "sensor_power.h"
#include "wifi6_sensor_config.h"

#if CFG_DEBUG_ENABLED

namespace {

bool s_periodic = true;
bool s_stay_awake = false;
uint32_t s_last_status_ms = 0;

void print_help()
{
    Serial.println(F("Debug commands: s=status  p=toggle periodic status  w=toggle stay awake  r=restart  h=help"));
}

}  // namespace

void debug_init()
{
    Serial.begin(CFG_DEBUG_BAUD);
    // Never block when no USB host is attached.
    Serial.setTxTimeoutMs(0);
}

void debug_log(const char *fmt, ...)
{
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    const uint32_t ms = millis();
    Serial.printf("[%6lu.%03lu] %s\n", static_cast<unsigned long>(ms / 1000),
                  static_cast<unsigned long>(ms % 1000), line);
}

void debug_print_status()
{
    NetStatus net;
    net_get_status(&net);

    const char *wifi = !net.radio_on ? "off" : (net.wifi_connected ? "connected" : "connecting");
    const char *mqtt = net.mqtt_connected ? "connected" : "disconnected";

    char inputs[96] = "";
    size_t len = 0;
    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        if (!inputs_enabled(id)) {
            len += snprintf(inputs + len, sizeof(inputs) - len, " %s=disabled", inputs_name(id));
            continue;
        }
        len += snprintf(inputs + len, sizeof(inputs) - len, " %s=%s(raw=%d)", inputs_name(id),
                        inputs_state_name(id, inputs_get_state(id)), inputs_raw_level(id));
    }

    debug_log("STATUS wifi=%s rssi=%d ch=%u ip=%s | mqtt=%s(state=%d) | inputs:%s | sleep=%s wake=%s boot=%lu "
              "sleeps=%lu stay_awake=%d bat=%lumV(%u%%) heap=%lu",
              wifi, net.rssi_dbm, net.channel, IPAddress(net.ip).toString().c_str(), mqtt, net.mqtt_state, inputs,
              power_sleep_mode_name(), power_wake_reason_name(power_last_wake_reason()),
              static_cast<unsigned long>(power_boot_count()), static_cast<unsigned long>(power_sleep_count()),
              s_stay_awake, static_cast<unsigned long>(battery_mv()), battery_percent(),
              static_cast<unsigned long>(ESP.getFreeHeap()));
    debug_log("STATS wifi_connects=%lu wifi_timeouts=%lu mqtt_connects=%lu mqtt_failures=%lu topic=%s",
              static_cast<unsigned long>(net.wifi_connects), static_cast<unsigned long>(net.wifi_timeouts),
              static_cast<unsigned long>(net.mqtt_connects), static_cast<unsigned long>(net.mqtt_failures),
              net_base_topic());
}

void debug_loop()
{
    while (Serial.available() > 0) {
        switch (Serial.read()) {
        case 's':
            debug_print_status();
            break;
        case 'p':
            s_periodic = !s_periodic;
            debug_log("periodic status %s", s_periodic ? "on" : "off");
            break;
        case 'w':
            s_stay_awake = !s_stay_awake;
            debug_log("stay awake %s", s_stay_awake ? "on" : "off");
            break;
        case 'r':
            debug_log("restarting");
            Serial.flush();
            ESP.restart();
            break;
        case 'h':
        case '?':
            print_help();
            break;
        default:
            break;
        }
    }

    const uint32_t now = millis();
    if (s_periodic && now - s_last_status_ms >= CFG_DEBUG_STATUS_PERIOD_MS) {
        s_last_status_ms = now;
        debug_print_status();
    }
}

bool debug_stay_awake()
{
    return s_stay_awake;
}

#else

void debug_init() {}
void debug_loop() {}
void debug_log(const char *, ...) {}
void debug_print_status() {}
bool debug_stay_awake() { return false; }

#endif
