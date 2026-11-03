#ifndef PARKING_STATE_H
#define PARKING_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PARKING_SLOT_COUNT 5
/* The Sensor Client sends at least once a second; three missed updates means it is gone. */
#define SENSOR_TIMEOUT_US 3000000LL
/* Flood alarm with hysteresis so a level near the threshold does not toggle it. */
#define FLOOD_ON_MM 20.0f
#define FLOOD_OFF_MM 15.0f

typedef struct
{
    int slots[PARKING_SLOT_COUNT]; /* 1 = occupied, 0 = free */
    float temperature_c;
    float water_mm;
    bool fire;
    bool received;
    bool temperature_received;
    bool online; /* set by parking_state_get: a sensor update arrived within SENSOR_TIMEOUT_US */
    bool flood;  /* water above FLOOD_ON_MM, until it falls below FLOOD_OFF_MM */
} parking_state_t;

void parking_state_update(const parking_state_t *state);
void parking_state_set_temperature(float temperature_c);
void parking_state_get(parking_state_t *state, int *free_count);

/* Time base for sessions and the log: uptime plus an offset set from a restore or a browser. */
int64_t parking_clock_now_us(void);
void parking_clock_adjust(int64_t delta_us);

#endif
