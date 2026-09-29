# wifi6_sensor MQTT topics

Base topic: `wifi6_sensor/<device_id>`. `<device_id>` defaults to `xiaoc5_<last 3 MAC bytes>` (e.g. `xiaoc5_a1b2c3`). Set it with `CFG_DEVICE_ID`, and change the prefix with `CFG_MQTT_BASE_TOPIC` in [include/wifi6_sensor_config.h](../include/wifi6_sensor_config.h).

All state messages are **retained**, so Home Assistant sees the last state right after a restart, even while the sensor sleeps.

## State topics

| Topic | Payload | Retained | Published when |
|---|---|---|---|
| `wifi6_sensor/<id>/availability` | `online` / `offline` | yes | `online` on every MQTT connect; `offline` is the Last Will (unclean disconnect only) |
| `wifi6_sensor/<id>/window` | `open` / `closed` | yes | reed change (debounced), every wake-up, every heartbeat |
| `wifi6_sensor/<id>/motion` | `detected` / `clear` | yes | IR change (debounced), every wake-up, every heartbeat |
| `wifi6_sensor/<id>/rssi` | integer dBm, e.g. `-63` | yes | every wake-up / heartbeat / state change |
| `wifi6_sensor/<id>/ssid` | WiFi network name, e.g. `MyHomeWiFi` | yes | every wake-up / heartbeat / state change |
| `wifi6_sensor/<id>/ip` | IPv4 address, e.g. `192.168.1.50` | yes | every wake-up / heartbeat / state change |
| `wifi6_sensor/<id>/mac` | WiFi station MAC, e.g. `AA:BB:CC:A1:B2:C3` | yes | every wake-up / heartbeat / state change |
| `wifi6_sensor/<id>/version` | firmware version, e.g. `0.1` | yes | every wake-up / heartbeat / state change |
| `wifi6_sensor/<id>/battery_voltage` | volts, 3 decimals, e.g. `3.912` | yes | every wake-up / heartbeat / state change |
| `wifi6_sensor/<id>/battery` | integer %, `0`-`100` | yes | every wake-up / heartbeat / state change |
| `wifi6_sensor/<id>/attributes` | JSON, see below | yes | every wake-up / heartbeat / state change |

Only the applications initialized in [src/main.cpp](../src/main.cpp) publish their state: `app_window_init()` publishes `window`, `app_motion_init()` publishes `motion`. The battery topics are omitted when `CFG_BATTERY_ENABLED = 0`.

Publish order: the contact/motion state is always sent first, then all diagnostic topics (rssi, ssid, ip, mac, version, battery, attributes). This also happens after every state change while the sensor is awake.

After a published state change (or a wake-up caused by an input), the sensor stays connected for `CFG_AWAKE_AFTER_STATE_MS` (20 s). A further change within this window is published immediately, without a new wake-up and WiFi/MQTT reconnect. Each change restarts the window.

The firmware version is defined in [include/fw_version.h](../include/fw_version.h) and is also shown as the device software version in Home Assistant.

The battery is measured through the on-board divider (GPIO6, enabled by GPIO26) once per wake-up, before Wi-Fi starts. A state change while awake republishes that measurement. The percentage is linear between `CFG_BAT_EMPTY_MV` (3.3 V = 0 %) and `CFG_BAT_FULL_MV` (4.2 V = 100 %). With USB and no battery attached, the reading shows the charger output and is not meaningful.

A short motion pulse that starts and ends before MQTT reconnects is still sent as `detected` followed by `clear`.

### Attributes JSON

```json
{
  "ip": "192.168.1.50",
  "rssi": -63,
  "channel": 6,
  "bssid": "AA:BB:CC:DD:EE:FF",
  "uptime_s": 1234,
  "wake": "input",
  "boot": 3,
  "sleeps": 42,
  "sleep_mode": "light",
  "fw": "0.1"
}
```

`wake`: `reset` (power-on or reset), `input` (reed or IR woke the device), `timer` (heartbeat), `other`.

## Home Assistant discovery

With `CFG_HA_DISCOVERY = 1`, retained discovery configs are published once per power cycle:

| Discovery topic | Entity | device_class |
|---|---|---|
| `homeassistant/binary_sensor/<id>/window/config` | Window (window app) | `window` |
| `homeassistant/binary_sensor/<id>/motion/config` | Motion (motion app) | `motion` |
| `homeassistant/sensor/<id>/rssi/config` | WiFi signal (diagnostic) | `signal_strength` |
| `homeassistant/sensor/<id>/ssid/config` | WiFi network (diagnostic) | - |
| `homeassistant/sensor/<id>/ip/config` | IP address (diagnostic) | - |
| `homeassistant/sensor/<id>/mac/config` | MAC address (diagnostic) | - |
| `homeassistant/sensor/<id>/version/config` | Firmware version (diagnostic) | - |
| `homeassistant/sensor/<id>/battery_voltage/config` | Battery voltage (diagnostic) | `voltage` |
| `homeassistant/sensor/<id>/battery/config` | Battery (diagnostic) | `battery` |

Only the entities of initialized applications are announced. If an application is removed from `main.cpp`, its entity stays in Home Assistant: delete the device entity in Home Assistant, or clear the retained config, e.g. `mosquitto_pub -r -n -t homeassistant/binary_sensor/<id>/motion/config`. With `CFG_BATTERY_ENABLED = 0` the battery entities get an empty retained config, which removes them.

Every entity has `expire_after = 3 x CFG_HEARTBEAT_S` (64800 s = 18 h by default). If the sensor stops reporting (flat battery, out of range), the entity turns *unavailable* even though the broker never sends the Last Will. When the device goes to sleep, it disconnects cleanly on purpose, so it is not reported as offline while sleeping.

## Manual configuration (without discovery)

```yaml
mqtt:
  binary_sensor:
    - name: "Window"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/window"
      payload_on: "open"
      payload_off: "closed"
      device_class: window
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
      json_attributes_topic: "wifi6_sensor/xiaoc5_a1b2c3/attributes"
    # Only when app_motion_init() is called in src/main.cpp
    - name: "Motion"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/motion"
      payload_on: "detected"
      payload_off: "clear"
      device_class: motion
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
  sensor:
    - name: "Window sensor WiFi signal"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/rssi"
      unit_of_measurement: "dBm"
      device_class: signal_strength
      state_class: measurement
      entity_category: diagnostic
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
    - name: "Window sensor WiFi network"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/ssid"
      icon: mdi:wifi
      entity_category: diagnostic
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
    - name: "Window sensor IP address"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/ip"
      icon: mdi:ip-network
      entity_category: diagnostic
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
    - name: "Window sensor MAC address"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/mac"
      icon: mdi:identifier
      entity_category: diagnostic
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
    - name: "Window sensor firmware version"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/version"
      icon: mdi:chip
      entity_category: diagnostic
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
    - name: "Window sensor battery voltage"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/battery_voltage"
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      entity_category: diagnostic
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
    - name: "Window sensor battery"
      state_topic: "wifi6_sensor/xiaoc5_a1b2c3/battery"
      unit_of_measurement: "%"
      device_class: battery
      state_class: measurement
      entity_category: diagnostic
      availability_topic: "wifi6_sensor/xiaoc5_a1b2c3/availability"
      expire_after: 64800
```

## Testing from a PC

```sh
mosquitto_sub -h <broker> -u <user> -P <pass> -v -t 'wifi6_sensor/#' -t 'homeassistant/+/xiaoc5_+/+/config'
```
