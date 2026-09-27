# wifi6_sensor MQTT topics

Base topic: `wifi6_sensor/<device_id>`. `<device_id>` defaults to `xiaoc5_<last 3 MAC bytes>` (e.g. `xiaoc5_a1b2c3`). Set it with `CFG_DEVICE_ID`, and change the prefix with `CFG_MQTT_BASE_TOPIC` in [include/wifi6_sensor_config.h](../include/wifi6_sensor_config.h).

All state messages are **retained**, so Home Assistant sees the last state right after a restart, even while the sensor sleeps.

## State topics

| Topic | Payload | Retained | Published when |
|---|---|---|---|
| `wifi6_sensor/<id>/availability` | `online` / `offline` | yes | `online` on every MQTT connect; `offline` is the Last Will (unclean disconnect only) |
| `wifi6_sensor/<id>/window` | `open` / `closed` | yes | reed change (debounced), every wake-up, every heartbeat |
| `wifi6_sensor/<id>/motion` | `detected` / `clear` | yes | IR change (debounced), every wake-up, every heartbeat |
| `wifi6_sensor/<id>/rssi` | integer dBm, e.g. `-63` | yes | every wake-up / heartbeat |
| `wifi6_sensor/<id>/battery_voltage` | volts, 3 decimals, e.g. `3.912` | yes | every wake-up / heartbeat |
| `wifi6_sensor/<id>/battery` | integer %, `0`-`100` | yes | every wake-up / heartbeat |
| `wifi6_sensor/<id>/attributes` | JSON, see below | yes | every wake-up / heartbeat |

Disabled inputs (`CFG_REED_ENABLED` / `CFG_IR_ENABLED` = 0) publish no state. The battery topics are omitted when `CFG_BATTERY_ENABLED = 0`.

The battery is measured through the on-board divider (GPIO6, enabled by GPIO26) once per wake-up, before Wi-Fi starts. The percentage is linear between `CFG_BAT_EMPTY_MV` (3.3 V = 0 %) and `CFG_BAT_FULL_MV` (4.2 V = 100 %). With USB and no battery attached, the reading shows the charger output and is not meaningful.

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
  "fw": "1.0.0"
}
```

`wake`: `reset` (power-on or reset), `input` (reed or IR woke the device), `timer` (heartbeat), `other`.

## Home Assistant discovery

With `CFG_HA_DISCOVERY = 1`, retained discovery configs are published once per power cycle:

| Discovery topic | Entity | device_class |
|---|---|---|
| `homeassistant/binary_sensor/<id>/window/config` | Window | `window` |
| `homeassistant/binary_sensor/<id>/motion/config` | Motion | `motion` |
| `homeassistant/sensor/<id>/rssi/config` | WiFi signal (diagnostic) | `signal_strength` |
| `homeassistant/sensor/<id>/battery_voltage/config` | Battery voltage (diagnostic) | `voltage` |
| `homeassistant/sensor/<id>/battery/config` | Battery (diagnostic) | `battery` |

A disabled input gets an empty retained config, which removes the entity from Home Assistant.

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
    # Only when CFG_IR_ENABLED = 1
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
