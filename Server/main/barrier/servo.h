#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t servo_start(void);
esp_err_t servo_set_open(bool open);
bool servo_is_open(void);
