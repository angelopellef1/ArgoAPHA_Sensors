#pragma once

#include <stdint.h>

enum WakeReason : uint8_t {
    WAKE_RESET = 0,
    WAKE_INPUT,
    WAKE_TIMER,
    WAKE_OTHER
};

void power_init();
WakeReason power_last_wake_reason();
const char *power_wake_reason_name(WakeReason reason);
const char *power_sleep_mode_name();
uint32_t power_boot_count();
uint32_t power_sleep_count();
bool power_can_wake_from_deep_sleep(uint8_t pin);
WakeReason power_light_sleep(uint32_t timer_s);
[[noreturn]] void power_deep_sleep(uint32_t timer_s);
