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
    memcpy(current_state.slots, state->slots, sizeof(current_state.slots));
    current_state.water_mm = state->water_mm;
    current_state.fire = state->fire;
    if (state->temperature_received)
    {
        current_state.temperature_c = state->temperature_c;
        current_state.temperature_received = true;
    }
    current_state.received = true;
    portEXIT_CRITICAL(&state_lock);
}

void parking_state_set_temperature(float temperature_c)
{
    portENTER_CRITICAL(&state_lock);
    current_state.temperature_c = temperature_c;
    current_state.temperature_received = true;
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
