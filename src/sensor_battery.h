#pragma once

#include <stdint.h>

void battery_init();
// Connects the divider, averages the ADC and disconnects it again; returns mV (0 if disabled).
uint32_t battery_measure();
uint32_t battery_mv();
uint8_t battery_percent();
