#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A barrier only closes after its IR beam has stayed clear this long, so a one-sample
 * flicker of the sensor while a car is underneath cannot bring it down. */
#define GATE_CLEAR_US 300000LL

typedef struct
{
    bool counting;
    int64_t clear_since_us;
} gate_hold_t;

static inline void gate_hold_reset(gate_hold_t *hold)
{
    hold->counting = false;
}

/* Call on each check of the beam; true once it has been clear for GATE_CLEAR_US. */
static inline bool gate_hold_may_close(gate_hold_t *hold, bool car_present, int64_t now_us)
{
    if (car_present)
    {
        hold->counting = false;
        return false;
    }
    if (!hold->counting)
    {
        hold->counting = true;
        hold->clear_since_us = now_us;
    }
    return now_us - hold->clear_since_us >= GATE_CLEAR_US;
}
