#ifndef PARKING_STATE_H
#define PARKING_STATE_H

#include <stdbool.h>
#include <stddef.h>

#define PARKING_SLOT_COUNT 5

typedef struct
{
    int slots[PARKING_SLOT_COUNT]; /* 1 = occupied, 0 = free */
    float temperature_c;
    float water_mm;
    bool received;
} parking_state_t;

void parking_state_update(const parking_state_t *state);
void parking_state_get(parking_state_t *state, int *free_count);

#endif
