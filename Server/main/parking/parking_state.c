#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

#include "parking_state.h"

static parking_state_t current_state = {0};
static int64_t clock_offset_us;
static int64_t sensor_seen_us;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;

void parking_state_update(const parking_state_t *state)
{
    if (state == NULL)
        return;

    portENTER_CRITICAL(&state_lock);
    memcpy(current_state.slots, state->slots, sizeof(current_state.slots));
    current_state.water_mm = state->water_mm;
    if (state->water_mm >= FLOOD_ON_MM)
        current_state.flood = true;
    else if (state->water_mm <= FLOOD_OFF_MM)
        current_state.flood = false;
    sensor_seen_us = esp_timer_get_time();
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
    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&state_lock);
    memcpy(state, &current_state, sizeof(current_state));
    state->online = current_state.received && now_us - sensor_seen_us < SENSOR_TIMEOUT_US;
    for (size_t i = 0; i < PARKING_SLOT_COUNT; ++i)
        free_slots += current_state.slots[i] == 0;
    portEXIT_CRITICAL(&state_lock);
    *free_count = free_slots;
}

int64_t parking_clock_now_us(void)
{
    portENTER_CRITICAL(&state_lock); /* 64-bit read is not atomic on the 32-bit core */
    int64_t offset = clock_offset_us;
    portEXIT_CRITICAL(&state_lock);
    return offset + esp_timer_get_time();
}

void parking_clock_adjust(int64_t delta_us)
{
    portENTER_CRITICAL(&state_lock);
    clock_offset_us += delta_us;
    portEXIT_CRITICAL(&state_lock);
}
