#pragma once

#include <stdint.h>

enum SensorInput : uint8_t {
    SENSOR_INPUT_REED = 0,
    SENSOR_INPUT_IR,
    SENSOR_INPUT_COUNT
};

struct SensorInputEvent {
    SensorInput id;
    bool active;            // reed: true = open, IR: true = detected
    uint32_t timestamp_ms;
};

void inputs_init();
bool inputs_enabled(SensorInput id);
uint8_t inputs_pin(SensorInput id);
int inputs_raw_level(SensorInput id);
bool inputs_get_state(SensorInput id);
bool inputs_poll_event(SensorInputEvent *event);
bool inputs_is_settled();
const char *inputs_name(SensorInput id);
const char *inputs_state_name(SensorInput id, bool active);
