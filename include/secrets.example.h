#pragma once

// Copy this file to include/secrets.h and fill in your values.
// include/secrets.h is git-ignored.

#define SECRET_WIFI_SSID        "your-ssid"
#define SECRET_WIFI_PASSWORD    "your-wifi-password"

// Optional fallback network (5 GHz), tried when the primary one fails. "" = disabled.
#define SECRET_WIFI5_SSID       ""
#define SECRET_WIFI5_PASSWORD   ""

#define SECRET_MQTT_HOST        "192.168.1.10"   // IP or hostname of the Home Assistant MQTT broker
#define SECRET_MQTT_PORT        1883
#define SECRET_MQTT_USER        "mqtt-user"      // "" if the broker allows anonymous access
#define SECRET_MQTT_PASSWORD    "mqtt-password"
