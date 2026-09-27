#pragma once

void debug_init();
void debug_loop();
void debug_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void debug_print_status();
bool debug_stay_awake();
