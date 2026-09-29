#include "sensor_net.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "debug_api.h"
#include "secrets.h"
#include "sensor_battery.h"
#include "sensor_power.h"
#include "wifi6_sensor_config.h"

#ifndef SECRET_WIFI5_SSID
#define SECRET_WIFI5_SSID ""
#define SECRET_WIFI5_PASSWORD ""
#endif

namespace {

constexpr uint32_t kExpireAfterS = CFG_HEARTBEAT_S * 3;

struct WifiNetwork {
    const char *ssid;
    const char *password;
    wifi_band_mode_t band;
};

constexpr WifiNetwork kNetworks[] = {
#if CFG_WIFI_RANGE_TUNING && CFG_WIFI_BAND_2G_ONLY
    {SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD, WIFI_BAND_MODE_2G_ONLY},
#else
    {SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD, WIFI_BAND_MODE_AUTO},
#endif
    {SECRET_WIFI5_SSID, SECRET_WIFI5_PASSWORD, WIFI_BAND_MODE_5G_ONLY},
};
constexpr uint8_t kNetworkCount = sizeof(SECRET_WIFI5_SSID) > 1 ? 2 : 1;

// Survives sleep so the last working network is tried first after wake-up.
RTC_DATA_ATTR uint8_t s_network_index = 0;

WiFiClient s_tcp;
PubSubClient s_mqtt(s_tcp);

char s_device_id[32];
char s_base[64];
char s_availability_topic[96];
char s_device_json[192];

bool s_radio_on = false;
bool s_wifi_was_up = false;
bool s_mqtt_was_up = false;
bool s_just_connected = false;
uint32_t s_wifi_begin_ms = 0;
uint32_t s_mqtt_next_try_ms = 0;
uint32_t s_mqtt_backoff_ms = CFG_MQTT_RETRY_MIN_MS;
NetStatus s_stats = {};
volatile uint8_t s_last_disconnect_reason = 0;
bool s_scan_done = false;

// Survives deep sleep so discovery is sent once per power cycle.
RTC_DATA_ATTR bool s_discovery_sent = false;
NetDiscoveryHandler s_discovery_handler = nullptr;

struct DiagnosticSensor {
    const char *object;
    const char *name;
    const char *unit;       // nullptr for text sensors
    const char *dev_class;
    const char *icon;
    bool enabled;
};

const DiagnosticSensor kDiagnostics[] = {
    {"rssi", "WiFi signal", "dBm", "signal_strength", nullptr, true},
    {"ssid", "WiFi network", nullptr, nullptr, "mdi:wifi", true},
    {"ip", "IP address", nullptr, nullptr, "mdi:ip-network", true},
    {"mac", "MAC address", nullptr, nullptr, "mdi:identifier", true},
    {"battery_voltage", "Battery voltage", "V", "voltage", nullptr, CFG_BATTERY_ENABLED},
    {"battery", "Battery", "%", "battery", nullptr, CFG_BATTERY_ENABLED},
};

bool publish_leaf(const char *leaf, const char *payload)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", s_base, leaf);
    return s_mqtt.publish(topic, payload, true);
}

void apply_radio_config()
{
#if CFG_WIFI_RANGE_TUNING
    // 802.11ax enables HE ER-SU/DCM with WiFi 6 APs; HT20 gives the best sensitivity.
    wifi_protocols_t protocols = {};
    protocols.ghz_2g = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX;
    protocols.ghz_5g = WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX;
    esp_err_t err = esp_wifi_set_protocols(WIFI_IF_STA, &protocols);
    if (err != ESP_OK) {
        debug_log("WIFI set protocols failed: %s", esp_err_to_name(err));
    }

    wifi_bandwidths_t bandwidths = {};
    bandwidths.ghz_2g = WIFI_BW_HT20;
    bandwidths.ghz_5g = WIFI_BW_HT20;
    err = esp_wifi_set_bandwidths(WIFI_IF_STA, &bandwidths);
    if (err != ESP_OK) {
        debug_log("WIFI set bandwidths failed: %s", esp_err_to_name(err));
    }

    if (!WiFi.setTxPower(CFG_WIFI_TX_POWER)) {
        debug_log("WIFI set TX power failed");
    }
#endif
}

void on_wifi_disconnected(arduino_event_id_t, arduino_event_info_t info)
{
    s_last_disconnect_reason = info.wifi_sta_disconnected.reason;
}

const char *band_name(wifi_band_mode_t band)
{
    switch (band) {
    case WIFI_BAND_MODE_2G_ONLY: return "2.4 GHz";
    case WIFI_BAND_MODE_5G_ONLY: return "5 GHz";
    default: return "2.4+5 GHz";
    }
}

void scan_for_ssid()
{
    WiFi.setBandMode(WIFI_BAND_MODE_AUTO);
    const int count = WiFi.scanNetworks(false, true);
    for (uint8_t n = 0; n < kNetworkCount; n++) {
        bool found = false;
        for (int i = 0; i < count; i++) {
            if (WiFi.SSID(i) == kNetworks[n].ssid) {
                found = true;
                debug_log("WIFI scan: '%s' rssi=%d ch=%d auth=%d", kNetworks[n].ssid, WiFi.RSSI(i), WiFi.channel(i),
                          WiFi.encryptionType(i));
            }
        }
        if (!found) {
            debug_log("WIFI scan: '%s' NOT found among %d networks", kNetworks[n].ssid, count);
        }
    }
    WiFi.scanDelete();
}

void wifi_begin()
{
    if (s_network_index >= kNetworkCount) {
        s_network_index = 0;
    }
    const WifiNetwork &net = kNetworks[s_network_index];
    if (!WiFi.setBandMode(net.band)) {
        debug_log("WIFI band mode %s failed", band_name(net.band));
    }
    WiFi.begin(net.ssid, net.password);
    s_wifi_begin_ms = millis();
    debug_log("WIFI connecting to '%s' (%s)", net.ssid, band_name(net.band));
}

bool publish_sensor_discovery(const DiagnosticSensor &sensor)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", CFG_HA_DISCOVERY_PREFIX, s_device_id, sensor.object);
    if (!sensor.enabled) {
        // Empty retained config removes a previously announced entity.
        return s_mqtt.publish(topic, "", true);
    }

    char kind[128];
    if (sensor.unit != nullptr) {
        snprintf(kind, sizeof(kind), "\"unit_of_meas\":\"%s\",\"dev_cla\":\"%s\",\"stat_cla\":\"measurement\"",
                 sensor.unit, sensor.dev_class);
    } else {
        snprintf(kind, sizeof(kind), "\"ic\":\"%s\"", sensor.icon);
    }

    char payload[640];
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s/%s\",%s,"
             "\"ent_cat\":\"diagnostic\",\"avty_t\":\"%s\",\"exp_aft\":%lu,%s}",
             sensor.name, s_device_id, sensor.object, s_base, sensor.object, kind, s_availability_topic,
             static_cast<unsigned long>(kExpireAfterS), s_device_json);
    return s_mqtt.publish(topic, payload, true);
}

bool publish_discovery()
{
    bool ok = true;
    for (const DiagnosticSensor &sensor : kDiagnostics) {
        ok = publish_sensor_discovery(sensor) && ok;
    }
    if (s_discovery_handler != nullptr) {
        ok = s_discovery_handler() && ok;
    }
    return ok;
}

bool publish_attributes()
{
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"ip\":\"%s\",\"rssi\":%d,\"channel\":%u,\"bssid\":\"%s\",\"uptime_s\":%lu,"
             "\"wake\":\"%s\",\"boot\":%lu,\"sleeps\":%lu,\"sleep_mode\":\"%s\",\"fw\":\"%s\"}",
             WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel(), WiFi.BSSIDstr().c_str(),
             static_cast<unsigned long>(millis() / 1000), power_wake_reason_name(power_last_wake_reason()),
             static_cast<unsigned long>(power_boot_count()), static_cast<unsigned long>(power_sleep_count()),
             power_sleep_mode_name(), CFG_FW_VERSION);
    return publish_leaf("attributes", payload);
}

void mqtt_try_connect(uint32_t now)
{
    const char *user = SECRET_MQTT_USER[0] ? SECRET_MQTT_USER : nullptr;
    const char *pass = SECRET_MQTT_USER[0] ? SECRET_MQTT_PASSWORD : nullptr;

    debug_log("MQTT connecting to %s:%d", SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    if (s_mqtt.connect(s_device_id, user, pass, s_availability_topic, 1, true, "offline")) {
        s_mqtt.publish(s_availability_topic, "online", true);
        s_stats.mqtt_connects++;
        s_mqtt_backoff_ms = CFG_MQTT_RETRY_MIN_MS;
        s_mqtt_was_up = true;
        s_just_connected = true;
        debug_log("MQTT connected");
#if CFG_HA_DISCOVERY
        if (!s_discovery_sent) {
            s_discovery_sent = publish_discovery();
            debug_log("MQTT HA discovery %s", s_discovery_sent ? "sent" : "failed");
        }
#endif
        return;
    }

    s_stats.mqtt_failures++;
    debug_log("MQTT connect failed, state=%d, retry in %lu ms", s_mqtt.state(),
              static_cast<unsigned long>(s_mqtt_backoff_ms));
    s_mqtt_next_try_ms = now + s_mqtt_backoff_ms;
    s_mqtt_backoff_ms = min<uint32_t>(s_mqtt_backoff_ms * 2, CFG_MQTT_RETRY_MAX_MS);
}

}  // namespace

void net_init(const char *device_id)
{
    strlcpy(s_device_id, device_id, sizeof(s_device_id));
    snprintf(s_base, sizeof(s_base), "%s/%s", CFG_MQTT_BASE_TOPIC, s_device_id);
    snprintf(s_availability_topic, sizeof(s_availability_topic), "%s/availability", s_base);
    snprintf(s_device_json, sizeof(s_device_json),
             "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mdl\":\"XIAO ESP32-C5\","
             "\"mf\":\"Seeed Studio\",\"sw\":\"%s\"}",
             s_device_id, CFG_DEVICE_NAME, CFG_FW_VERSION);

    s_mqtt.setServer(SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    s_mqtt.setBufferSize(CFG_MQTT_BUFFER_SIZE);
    s_mqtt.setSocketTimeout(5);
    s_mqtt.setKeepAlive(30);
    WiFi.onEvent(on_wifi_disconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void net_start()
{
    if (s_radio_on) {
        return;
    }
    WiFi.persistent(false);
    WiFi.setHostname(s_device_id);
    WiFi.mode(WIFI_STA);
    apply_radio_config();
    WiFi.setSleep(true);
    WiFi.setAutoReconnect(true);
    wifi_begin();
    s_radio_on = true;
    s_mqtt_next_try_ms = millis();
}

void net_stop()
{
    if (!s_radio_on) {
        return;
    }
    if (s_mqtt.connected()) {
        s_mqtt.loop();
        delay(CFG_NET_FLUSH_MS);
        // Clean disconnect: broker does not fire the LWT, entity stays available while sleeping.
        s_mqtt.disconnect();
    }
    delay(CFG_NET_FLUSH_MS);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_radio_on = false;
    s_wifi_was_up = false;
    s_mqtt_was_up = false;
    debug_log("NET radio off");
}

void net_loop()
{
    if (!s_radio_on) {
        return;
    }
    const uint32_t now = millis();
    const bool wifi_up = WiFi.status() == WL_CONNECTED;

    if (wifi_up != s_wifi_was_up) {
        s_wifi_was_up = wifi_up;
        if (wifi_up) {
            s_stats.wifi_connects++;
            s_mqtt_next_try_ms = now;
            s_mqtt_backoff_ms = CFG_MQTT_RETRY_MIN_MS;
            debug_log("WIFI connected ssid='%s' ip=%s rssi=%d ch=%u (%lu ms)", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel(),
                      static_cast<unsigned long>(now - s_wifi_begin_ms));
        } else {
            s_wifi_begin_ms = now;
            debug_log("WIFI lost");
        }
    }

    if (!wifi_up) {
        if (now - s_wifi_begin_ms > CFG_WIFI_CONNECT_TIMEOUT_MS) {
            s_stats.wifi_timeouts++;
            const wifi_err_reason_t reason = static_cast<wifi_err_reason_t>(s_last_disconnect_reason);
            debug_log("WIFI connect timeout on '%s', status=%d, reason=%d (%s)", kNetworks[s_network_index].ssid,
                      WiFi.status(), reason, WiFi.disconnectReasonName(reason));
            WiFi.disconnect();
            if (!s_scan_done) {
                s_scan_done = true;
                scan_for_ssid();
            }
            s_network_index = (s_network_index + 1) % kNetworkCount;
            wifi_begin();
        }
        return;
    }

    if (s_mqtt.connected()) {
        s_mqtt.loop();
        return;
    }
    if (s_mqtt_was_up) {
        s_mqtt_was_up = false;
        debug_log("MQTT lost, state=%d", s_mqtt.state());
    }
    if (static_cast<int32_t>(now - s_mqtt_next_try_ms) >= 0) {
        mqtt_try_connect(now);
    }
}

bool net_mqtt_connected()
{
    return s_radio_on && s_mqtt.connected();
}

bool net_mqtt_just_connected()
{
    const bool value = s_just_connected;
    s_just_connected = false;
    return value;
}

bool net_publish_state(const char *leaf, const char *payload)
{
    return net_mqtt_connected() && publish_leaf(leaf, payload);
}

bool net_publish_binary_discovery(const char *object, const char *label, const char *device_class,
                                  const char *payload_on, const char *payload_off)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/%s/config", CFG_HA_DISCOVERY_PREFIX, s_device_id, object);

    char payload[640];
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s/%s\","
             "\"pl_on\":\"%s\",\"pl_off\":\"%s\",\"dev_cla\":\"%s\","
             "\"avty_t\":\"%s\",\"exp_aft\":%lu,\"json_attr_t\":\"%s/attributes\",%s}",
             label, s_device_id, object, s_base, object, payload_on, payload_off, device_class,
             s_availability_topic, static_cast<unsigned long>(kExpireAfterS), s_base, s_device_json);
    return s_mqtt.publish(topic, payload, true);
}

void net_set_discovery_handler(NetDiscoveryHandler handler)
{
    s_discovery_handler = handler;
}

bool net_publish_diagnostics()
{
    if (!net_mqtt_connected()) {
        return false;
    }
    char rssi[8];
    snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI());
    bool ok = publish_leaf("rssi", rssi);
    ok = publish_leaf("ssid", WiFi.SSID().c_str()) && ok;
    ok = publish_leaf("ip", WiFi.localIP().toString().c_str()) && ok;
    ok = publish_leaf("mac", WiFi.macAddress().c_str()) && ok;
#if CFG_BATTERY_ENABLED
    char value[16];
    const uint32_t mv = battery_mv();
    snprintf(value, sizeof(value), "%lu.%03lu", static_cast<unsigned long>(mv / 1000),
             static_cast<unsigned long>(mv % 1000));
    ok = publish_leaf("battery_voltage", value) && ok;
    snprintf(value, sizeof(value), "%u", battery_percent());
    ok = publish_leaf("battery", value) && ok;
#endif
    return publish_attributes() && ok;
}

void net_get_status(NetStatus *status)
{
    *status = s_stats;
    status->radio_on = s_radio_on;
    status->wifi_connected = s_radio_on && WiFi.status() == WL_CONNECTED;
    status->mqtt_connected = net_mqtt_connected();
    status->mqtt_state = s_mqtt.state();
    status->rssi_dbm = status->wifi_connected ? WiFi.RSSI() : 0;
    status->channel = status->wifi_connected ? WiFi.channel() : 0;
    status->ip = status->wifi_connected ? static_cast<uint32_t>(WiFi.localIP()) : 0;
}

const char *net_base_topic()
{
    return s_base;
}
