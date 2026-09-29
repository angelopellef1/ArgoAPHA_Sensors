#include "app_motion.h"

#include "sensor_runtime.h"
#include "wifi6_sensor_config.h"

namespace {

const SensorBinaryEntity kMotion = {
    {"ir", CFG_IR_PIN, CFG_IR_PULL, CFG_IR_ACTIVE_LEVEL, CFG_IR_DEBOUNCE_MS, "detected", "clear"},
    "motion",
    "Motion",
    "motion",
};

}  // namespace

bool app_motion_init()
{
    return runtime_add_binary_sensor(&kMotion);
}
