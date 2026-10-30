/* Host stub: tests control the clock through fake_time_us. */
#pragma once
#include <stdint.h>
extern int64_t fake_time_us;
static inline int64_t esp_timer_get_time(void) { return fake_time_us; }
