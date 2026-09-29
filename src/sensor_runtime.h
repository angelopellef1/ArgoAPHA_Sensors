#pragma once

#include "sensor_inputs.h"

// A debounced input published as a Home Assistant binary_sensor. Must have static lifetime.
struct SensorBinaryEntity {
    SensorInputConfig input;    // active_name / inactive_name are the MQTT payloads
    const char *object;         // MQTT leaf and HA object id, e.g. "window"
    const char *label;          // HA entity name
    const char *device_class;   // HA binary_sensor device_class
};

void runtime_init();
bool runtime_add_binary_sensor(const SensorBinaryEntity *entity);
void runtime_start();
void runtime_loop();
