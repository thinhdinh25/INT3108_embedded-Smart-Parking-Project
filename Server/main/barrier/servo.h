#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SERVO_GATE_IN,
    SERVO_GATE_OUT,
} servo_gate_t;

esp_err_t servo_start(void);
esp_err_t servo_set_in_open(bool open);
esp_err_t servo_set_out_open(bool open);
esp_err_t servo_set_all_open(bool open);
esp_err_t servo_open_for(servo_gate_t gate, uint32_t duration_ms);
esp_err_t servo_set_fire_alarm(bool active);
bool servo_is_in_open(void);
bool servo_is_out_open(void);
