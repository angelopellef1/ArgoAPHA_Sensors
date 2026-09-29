#include <Arduino.h>

#include "app_window.h"
#include "sensor_runtime.h"

void setup()
{
    runtime_init();
    app_window_init();
    runtime_start();
}

void loop()
{
    runtime_loop();
}