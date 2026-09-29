#pragma once

#include <Arduino.h>

#include "fw_version.h"

// ---------------------------------------------------------------------------
// Firmware identity (version: include/fw_version.h)
// ---------------------------------------------------------------------------
#define CFG_DEVICE_NAME             "WiFi6 Window Sensor"
// Empty string -> auto "xiaoc5_<last 3 MAC bytes>"
#define CFG_DEVICE_ID               ""

// ---------------------------------------------------------------------------
// Applications (select them in src/main.cpp: app_window_init(), app_motion_init())
// ---------------------------------------------------------------------------
// Window contact application (src/app_window.cpp)
#define CFG_REED_PIN                D1              // GPIO0, LP GPIO (deep-sleep wake capable)
#define CFG_REED_PULL               INPUT           // external 1 MOhm pull-up to 3V3 (see docs/reed_wiring.md)
#define CFG_REED_OPEN_LEVEL         HIGH            // magnet away -> contact open -> pulled HIGH
#define CFG_REED_DEBOUNCE_MS        50

// Motion (IR) application (src/app_motion.cpp)
#define CFG_IR_PIN                  D2              // GPIO25, light-sleep wake only
#define CFG_IR_PULL                 INPUT_PULLDOWN  // PIR push-pull output; use INPUT_PULLUP for open-collector sensors
#define CFG_IR_ACTIVE_LEVEL         HIGH            // level when motion/IR is detected
#define CFG_IR_DEBOUNCE_MS          30

#define CFG_DEBOUNCE_SAMPLE_MS      5

// ---------------------------------------------------------------------------
// Power management
// ---------------------------------------------------------------------------
#define SLEEP_MODE_NONE             0   // always on, WiFi modem sleep
#define SLEEP_MODE_LIGHT            1   // WiFi off + light sleep, wake on any enabled input or heartbeat
#define SLEEP_MODE_DEEP             2   // deep sleep, wake on LP GPIO inputs (D1 reed) or heartbeat only

#ifndef CFG_SLEEP_MODE
#define CFG_SLEEP_MODE              SLEEP_MODE_DEEP
#endif
#define CFG_AWAKE_AFTER_BOOT_MS     60000   // stay awake after reset so USB flashing/debug is possible
#define CFG_AWAKE_AFTER_EVENT_MS    2000    // minimum awake time after the last input change
#define CFG_AWAKE_AFTER_STATE_MS    20000   // stay connected after a published state change for fast follow-up changes
#define CFG_MAX_AWAKE_MS            30000   // give up publishing and sleep anyway after this time
#define CFG_HEARTBEAT_S             21600   // 6 h periodic state republish / timer wake-up

// ---------------------------------------------------------------------------
// Battery monitor (on-board 2 x 100k divider behind a load switch)
// ---------------------------------------------------------------------------
#define CFG_BATTERY_ENABLED         1
#define CFG_BAT_ADC_PIN             6       // ADC_BAT (GPIO6)
#define CFG_BAT_EN_PIN              26      // ADC_CRL (GPIO26), HIGH connects the divider
#define CFG_BAT_DIVIDER             2
#define CFG_BAT_SAMPLES             16
#define CFG_BAT_SETTLE_MS           10
#define CFG_BAT_EMPTY_MV            3300    // reported as 0 %
#define CFG_BAT_FULL_MV             4200    // reported as 100 %

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
#ifndef CFG_WIFI_RANGE_TUNING
#define CFG_WIFI_RANGE_TUNING       1       // 0 = driver defaults (band, protocols, bandwidth, TX power)
#endif
#define CFG_WIFI_BAND_2G_ONLY       1       // 2.4 GHz has better range than 5 GHz
#define CFG_WIFI_TX_POWER           WIFI_POWER_20dBm
#define CFG_WIFI_CONNECT_TIMEOUT_MS 15000
#define CFG_NET_FLUSH_MS            100     // let TCP drain before radio off

#define CFG_MQTT_BASE_TOPIC         "wifi6_sensor"
#define CFG_MQTT_BUFFER_SIZE        1024
#define CFG_MQTT_RETRY_MIN_MS       2000
#define CFG_MQTT_RETRY_MAX_MS       60000
#define CFG_HA_DISCOVERY            1
#define CFG_HA_DISCOVERY_PREFIX     "homeassistant"

// ---------------------------------------------------------------------------
// Debug API (USB serial)
// ---------------------------------------------------------------------------
#ifndef CFG_DEBUG_ENABLED
#define CFG_DEBUG_ENABLED           1
#endif
#define CFG_DEBUG_BAUD              115200
#define CFG_DEBUG_STATUS_PERIOD_MS  5000
