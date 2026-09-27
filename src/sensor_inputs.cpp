#include "sensor_inputs.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "wifi6_sensor_config.h"

namespace {

struct InputConfig {
    bool enabled;
    uint8_t pin;
    uint8_t pull_mode;
    uint8_t active_level;
    uint16_t debounce_ms;
    const char *name;
    const char *active_name;
    const char *inactive_name;
};

struct InputRuntime {
    uint8_t integrator;
    uint8_t integrator_max;
    volatile bool active;
};

const InputConfig kConfig[SENSOR_INPUT_COUNT] = {
    {CFG_REED_ENABLED, CFG_REED_PIN, CFG_REED_PULL, CFG_REED_OPEN_LEVEL, CFG_REED_DEBOUNCE_MS, "reed", "open", "closed"},
    {CFG_IR_ENABLED, CFG_IR_PIN, CFG_IR_PULL, CFG_IR_ACTIVE_LEVEL, CFG_IR_DEBOUNCE_MS, "ir", "detected", "clear"},
};

constexpr UBaseType_t kQueueLength = 16;

InputRuntime s_runtime[SENSOR_INPUT_COUNT];
QueueHandle_t s_queue = nullptr;
esp_timer_handle_t s_timer = nullptr;

bool read_active(SensorInput id)
{
    return digitalRead(kConfig[id].pin) == kConfig[id].active_level;
}

// Integrator debounce: the output flips only after the integrator saturates,
// which needs debounce_ms of (mostly) consistent samples.
void sample_callback(void *)
{
    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        if (!kConfig[id].enabled) {
            continue;
        }
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

void inputs_init()
{
    s_queue = xQueueCreate(kQueueLength, sizeof(SensorInputEvent));

    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        if (!kConfig[id].enabled) {
            continue;
        }
        pinMode(kConfig[id].pin, kConfig[id].pull_mode);
        InputRuntime &rt = s_runtime[id];
        uint32_t max = kConfig[id].debounce_ms / CFG_DEBOUNCE_SAMPLE_MS;
        rt.integrator_max = static_cast<uint8_t>(constrain(max, 1U, 255U));
    }

    // Let the pull resistors settle before seeding the debouncer.
    delay(2);
    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        if (kConfig[id].enabled) {
            InputRuntime &rt = s_runtime[id];
            rt.active = read_active(id);
            rt.integrator = rt.active ? rt.integrator_max : 0;
        }
    }

    esp_timer_create_args_t args = {};
    args.callback = sample_callback;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "debounce";
    args.skip_unhandled_events = true;
    esp_timer_create(&args, &s_timer);
    esp_timer_start_periodic(s_timer, CFG_DEBOUNCE_SAMPLE_MS * 1000ULL);
}

bool inputs_enabled(SensorInput id)
{
    return id < SENSOR_INPUT_COUNT && kConfig[id].enabled;
}

uint8_t inputs_pin(SensorInput id)
{
    return kConfig[id].pin;
}

int inputs_raw_level(SensorInput id)
{
    return digitalRead(kConfig[id].pin);
}

bool inputs_get_state(SensorInput id)
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
    for (uint8_t i = 0; i < SENSOR_INPUT_COUNT; i++) {
        const SensorInput id = static_cast<SensorInput>(i);
        if (!kConfig[id].enabled) {
            continue;
        }
        const InputRuntime &rt = s_runtime[id];
        const bool saturated = rt.integrator == 0 || rt.integrator == rt.integrator_max;
        if (!saturated || read_active(id) != rt.active) {
            return false;
        }
    }
    return true;
}

const char *inputs_name(SensorInput id)
{
    return kConfig[id].name;
}

const char *inputs_state_name(SensorInput id, bool active)
{
    return active ? kConfig[id].active_name : kConfig[id].inactive_name;
}
