#include "sensor_battery.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "wifi6_sensor_config.h"

namespace {

uint32_t s_mv = 0;

}  // namespace

void battery_init()
{
#if CFG_BATTERY_ENABLED
    // The pad may still be held from before the last sleep.
    gpio_hold_dis(static_cast<gpio_num_t>(CFG_BAT_EN_PIN));
    pinMode(CFG_BAT_EN_PIN, OUTPUT);
    digitalWrite(CFG_BAT_EN_PIN, LOW);
    pinMode(CFG_BAT_ADC_PIN, INPUT);
#endif
}

uint32_t battery_measure()
{
#if CFG_BATTERY_ENABLED
    const gpio_num_t en = static_cast<gpio_num_t>(CFG_BAT_EN_PIN);
    gpio_hold_dis(en);
    digitalWrite(CFG_BAT_EN_PIN, HIGH);
    delay(CFG_BAT_SETTLE_MS);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < CFG_BAT_SAMPLES; i++) {
        sum += analogReadMilliVolts(CFG_BAT_ADC_PIN);
    }

    digitalWrite(CFG_BAT_EN_PIN, LOW);
    // Keep the divider disconnected through light and deep sleep (it would draw ~20 uA).
    gpio_hold_en(en);
    s_mv = sum / CFG_BAT_SAMPLES * CFG_BAT_DIVIDER;
#endif
    return s_mv;
}

uint32_t battery_mv()
{
    return s_mv;
}

uint8_t battery_percent()
{
    if (s_mv <= CFG_BAT_EMPTY_MV) {
        return 0;
    }
    if (s_mv >= CFG_BAT_FULL_MV) {
        return 100;
    }
    return static_cast<uint8_t>((s_mv - CFG_BAT_EMPTY_MV) * 100 / (CFG_BAT_FULL_MV - CFG_BAT_EMPTY_MV));
}
