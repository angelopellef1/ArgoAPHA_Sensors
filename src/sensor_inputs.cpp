#include "sensor_inputs.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "wifi6_sensor_config.h"

namespace {

struct InputRuntime {
    uint8_t integrator;
    uint8_t integrator_max;
    volatile bool active;
};

constexpr UBaseType_t kQueueLength = 16;

const SensorInputConfig *s_config[SENSOR_INPUT_MAX];
InputRuntime s_runtime[SENSOR_INPUT_MAX];
uint8_t s_count = 0;
QueueHandle_t s_queue = nullptr;
esp_timer_handle_t s_timer = nullptr;

bool read_active(uint8_t id)
{
    return digitalRead(s_config[id]->pin) == s_config[id]->active_level;
}

// Integrator debounce: the output flips only after the integrator saturates,
// which needs debounce_ms of (mostly) consistent samples.
void sample_callback(void *)
{
    for (uint8_t id = 0; id < s_count; id++) {
        InputRuntime &rt = s_runtime[id];
        if (read_active(id)) {
            if (rt.integrator < rt.integrator_max) {
                rt.integrator++;
            }
        } else if (rt.integrator > 0) {
            rt.integrator--;
        }

        bool next = rt.active;
        if (rt.integrator == 0) {
            next = false;
        } else if (rt.integrator >= rt.integrator_max) {
            next = true;
        }

        if (next != rt.active) {
            rt.active = next;
            const SensorInputEvent event = {id, next, millis()};
            xQueueSend(s_queue, &event, 0);
        }
    }
}

}  // namespace

int inputs_add(const SensorInputConfig *config)
{
    // The sampling timer iterates without locking, so the table is frozen once started.
    if (s_timer != nullptr || s_count >= SENSOR_INPUT_MAX) {
        return -1;
    }
    const uint8_t id = s_count;
    const gpio_num_t pin = static_cast<gpio_num_t>(config->pin);
    if (rtc_gpio_is_valid_gpio(pin)) {
        // Release the pad hold/RTC mux left by a deep-sleep wake-up.
        rtc_gpio_deinit(pin);
    }
    pinMode(config->pin, config->pull_mode);
    s_config[id] = config;
    InputRuntime &rt = s_runtime[id];
    const uint32_t max = config->debounce_ms / CFG_DEBOUNCE_SAMPLE_MS;
    rt.integrator_max = static_cast<uint8_t>(constrain(max, 1U, 255U));

    // Let the pull resistor settle before seeding the debouncer.
    delay(2);
    rt.active = read_active(id);
    rt.integrator = rt.active ? rt.integrator_max : 0;
    s_count++;
    return id;
}

void inputs_start()
{
    if (s_timer != nullptr) {
        return;
    }
    s_queue = xQueueCreate(kQueueLength, sizeof(SensorInputEvent));

    esp_timer_create_args_t args = {};
    args.callback = sample_callback;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "debounce";
    args.skip_unhandled_events = true;
    esp_timer_create(&args, &s_timer);
    esp_timer_start_periodic(s_timer, CFG_DEBOUNCE_SAMPLE_MS * 1000ULL);
}

uint8_t inputs_count()
{
    return s_count;
}

uint8_t inputs_pin(uint8_t id)
{
    return s_config[id]->pin;
}

uint8_t inputs_pull_mode(uint8_t id)
{
    return s_config[id]->pull_mode;
}

int inputs_raw_level(uint8_t id)
{
    return digitalRead(s_config[id]->pin);
}

bool inputs_get_state(uint8_t id)
{
    return s_runtime[id].active;
}

bool inputs_poll_event(SensorInputEvent *event)
{
    return s_queue != nullptr && xQueueReceive(s_queue, event, 0) == pdTRUE;
}

bool inputs_is_settled()
{
    if (s_queue != nullptr && uxQueueMessagesWaiting(s_queue) > 0) {
        return false;
    }
    for (uint8_t id = 0; id < s_count; id++) {
        const InputRuntime &rt = s_runtime[id];
        const bool saturated = rt.integrator == 0 || rt.integrator == rt.integrator_max;
        if (!saturated || read_active(id) != rt.active) {
            return false;
        }
    }
    return true;
}

const char *inputs_name(uint8_t id)
{
    return s_config[id]->name;
}

const char *inputs_state_name(uint8_t id, bool active)
{
    return active ? s_config[id]->active_name : s_config[id]->inactive_name;
}
