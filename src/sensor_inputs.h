#pragma once

#include <stdint.h>

constexpr uint8_t SENSOR_INPUT_MAX = 4;

// Provided by the application; must have static lifetime.
struct SensorInputConfig {
    const char *name;           // log name, e.g. "reed"
    uint8_t pin;
    uint8_t pull_mode;          // INPUT / INPUT_PULLUP / INPUT_PULLDOWN
    uint8_t active_level;       // pin level of the active state
    uint16_t debounce_ms;
    const char *active_name;    // e.g. "open"
    const char *inactive_name;  // e.g. "closed"
};

struct SensorInputEvent {
    uint8_t id;
    bool active;
    uint32_t timestamp_ms;
};

int inputs_add(const SensorInputConfig *config);
void inputs_start();
uint8_t inputs_count();
uint8_t inputs_pin(uint8_t id);
uint8_t inputs_pull_mode(uint8_t id);
int inputs_raw_level(uint8_t id);
bool inputs_get_state(uint8_t id);
bool inputs_poll_event(SensorInputEvent *event);
bool inputs_is_settled();
const char *inputs_name(uint8_t id);
const char *inputs_state_name(uint8_t id, bool active);
