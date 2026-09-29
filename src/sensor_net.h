#pragma once

#include <stdint.h>

// Publishes the application entities' Home Assistant discovery; true = all sent.
using NetDiscoveryHandler = bool (*)();

struct NetStatus {
    bool radio_on;
    bool wifi_connected;
    bool mqtt_connected;
    int mqtt_state;         // PubSubClient::state()
    int rssi_dbm;
    uint8_t channel;
    uint32_t ip;
    uint32_t wifi_connects;
    uint32_t wifi_timeouts;
    uint32_t mqtt_connects;
    uint32_t mqtt_failures;
};

void net_init(const char *device_id);
void net_start();
void net_stop();
void net_loop();
bool net_mqtt_connected();
bool net_mqtt_just_connected();
bool net_publish_state(const char *leaf, const char *payload);
bool net_publish_binary_discovery(const char *object, const char *label, const char *device_class,
                                  const char *payload_on, const char *payload_off);
void net_set_discovery_handler(NetDiscoveryHandler handler);
bool net_publish_diagnostics();
void net_get_status(NetStatus *status);
const char *net_base_topic();
