#include <string.h>

#include "freertos/FreeRTOS.h"

#include "parking_state.h"

static parking_state_t current_state = {0};
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;

void parking_state_update(const parking_state_t *state)
{
    if (state == NULL)
        return;

    portENTER_CRITICAL(&state_lock);
    memcpy(&current_state, state, sizeof(current_state));
    current_state.received = true;
    portEXIT_CRITICAL(&state_lock);
}

void parking_state_get(parking_state_t *state, int *free_count)
{
    if (state == NULL || free_count == NULL)
        return;

    int free_slots = 0;
    portENTER_CRITICAL(&state_lock);
    memcpy(state, &current_state, sizeof(current_state));
    for (size_t i = 0; i < PARKING_SLOT_COUNT; ++i)
        free_slots += current_state.slots[i] == 0;
    portEXIT_CRITICAL(&state_lock);
    *free_count = free_slots;
}
