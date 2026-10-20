#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t servo_start(void);
esp_err_t servo_set_in_open(bool open);
esp_err_t servo_set_out_open(bool open);
esp_err_t servo_set_all_open(bool open);
bool servo_is_in_open(void);
bool servo_is_out_open(void);
