#include "sensor_power.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "sensor_inputs.h"
#include "wifi6_sensor_config.h"

namespace {

RTC_DATA_ATTR uint32_t s_boot_count = 0;
RTC_DATA_ATTR uint32_t s_sleep_count = 0;
WakeReason s_last_wake = WAKE_RESET;

WakeReason map_cause(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        return WAKE_RESET;
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_EXT1:
        return WAKE_INPUT;
    case ESP_SLEEP_WAKEUP_TIMER:
        return WAKE_TIMER;
    default:
        return WAKE_OTHER;
    }
}

}  // namespace

void power_init()
{
    s_boot_count++;
    s_last_wake = map_cause(esp_sleep_get_wakeup_cause());
    if (s_last_wake == WAKE_RESET) {
        s_sleep_count = 0;
    }
#if CFG_SLEEP_MODE == SLEEP_MODE_DEEP
    // Release the pad hold/RTC mux set before the previous deep sleep.
    rtc_gpio_deinit(static_cast<gpio_num_t>(CFG_REED_PIN));
#endif
}

WakeReason power_last_wake_reason()
{
    return s_last_wake;
}

const char *power_wake_reason_name(WakeReason reason)
{
    switch (reason) {
    case WAKE_RESET:
        return "reset";
    case WAKE_INPUT:
        return "input";
    case WAKE_TIMER:
        return "timer";
    default:
        return "other";
    }
}

const char *power_sleep_mode_name()
{
#if CFG_SLEEP_MODE == SLEEP_MODE_LIGHT
    return "light";
#elif CFG_SLEEP_MODE == SLEEP_MODE_DEEP
    return "deep";
#else
    return "none";
#endif
}

uint32_t power_boot_count()
{
    return s_boot_count;
}

uint32_t power_sleep_count()
{
    return s_sleep_count;
}

WakeReason power_light_sleep(uint32_t timer_s)
{
    // Level wake-up on the opposite of the current level == wake on change.
    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        if (!inputs_enabled(id)) {
            continue;
        }
        const gpio_num_t pin = static_cast<gpio_num_t>(inputs_pin(id));
        const gpio_int_type_t level = inputs_raw_level(id) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
        gpio_wakeup_enable(pin, level);
    }
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timer_s) * 1000000ULL);

    Serial.flush();
    s_sleep_count++;
    esp_light_sleep_start();

    s_last_wake = map_cause(esp_sleep_get_wakeup_cause());
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        if (inputs_enabled(id)) {
            gpio_wakeup_disable(static_cast<gpio_num_t>(inputs_pin(id)));
        }
    }
    return s_last_wake;
}

void power_deep_sleep(uint32_t timer_s)
{
    const gpio_num_t pin = static_cast<gpio_num_t>(CFG_REED_PIN);
    const esp_sleep_ext1_wakeup_mode_t mode =
        digitalRead(CFG_REED_PIN) ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH;

    esp_sleep_enable_ext1_wakeup_io(1ULL << pin, mode);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timer_s) * 1000000ULL);

    // Keep the internal pull active on the LP pad while the digital domain is off.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    if (CFG_REED_PULL == INPUT_PULLUP) {
        rtc_gpio_pulldown_dis(pin);
        rtc_gpio_pullup_en(pin);
    } else if (CFG_REED_PULL == INPUT_PULLDOWN) {
        rtc_gpio_pullup_dis(pin);
        rtc_gpio_pulldown_en(pin);
    }

    Serial.flush();
    s_sleep_count++;
    esp_deep_sleep_start();
}
