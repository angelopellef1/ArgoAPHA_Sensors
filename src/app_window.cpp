#include "app_window.h"

#include "sensor_runtime.h"
#include "wifi6_sensor_config.h"

namespace {

const SensorBinaryEntity kWindow = {
    {"reed", CFG_REED_PIN, CFG_REED_PULL, CFG_REED_OPEN_LEVEL, CFG_REED_DEBOUNCE_MS, "open", "closed"},
    "window",
    "Window",
    "window",
};

}  // namespace

bool app_window_init()
{
    return runtime_add_binary_sensor(&kWindow);
}
