#include <string.h>

#include "json_scan.h"
#include "parking_update.h"

#define PARKING_UPDATE_MAX_FIELDS 4

/* Strict parser for {"type":"parking_update","slots":[5 x 0|1],"water_mm":N,"fire":bool}.
 * Keys may come in any order; "fire" is optional for older clients. */
bool parking_update_parse_json(const char *payload, parking_state_t *state)
{
    if (payload == NULL || state == NULL || !json_consume_char(&payload, '{'))
        return false;

    bool has_type = false, has_slots = false, has_water = false, has_fire = false;
    for (int field = 0; field < PARKING_UPDATE_MAX_FIELDS; ++field)
    {
        char key[12], value[16];
        if (!json_parse_string(&payload, key, sizeof(key)) ||
            !json_consume_char(&payload, ':'))
            return false;

        if (strcmp(key, "type") == 0)
        {
            if (has_type || !json_parse_string(&payload, value, sizeof(value)) ||
                strcmp(value, "parking_update") != 0)
                return false;
            has_type = true;
        }
        else if (strcmp(key, "slots") == 0)
        {
            if (has_slots || !json_consume_char(&payload, '['))
                return false;
            for (int i = 0; i < PARKING_SLOT_COUNT; ++i)
            {
                json_skip_ws(&payload);
                if (*payload != '0' && *payload != '1')
                    return false;
                state->slots[i] = *payload++ - '0';
                if (i < PARKING_SLOT_COUNT - 1 && !json_consume_char(&payload, ','))
                    return false;
            }
            if (!json_consume_char(&payload, ']'))
                return false;
            has_slots = true;
        }
        else if (strcmp(key, "water_mm") == 0)
        {
            float water;
            if (has_water || !json_parse_number(&payload, &water) ||
                !(water >= 0.0f && water <= 10000.0f))
                return false;
            state->water_mm = water;
            has_water = true;
        }
        else if (strcmp(key, "fire") == 0)
        {
            if (has_fire || !json_parse_bool(&payload, &state->fire))
                return false;
            has_fire = true;
        }
        else
            return false;

        json_skip_ws(&payload);
        if (*payload == '}')
            break;
        if (*payload != ',' || field == PARKING_UPDATE_MAX_FIELDS - 1)
            return false;
        ++payload;
    }

    return has_type && has_slots && has_water && json_parse_object_end(&payload);
}

/* {"type":"climate","temperature_c":N} sent by the RFID client every few seconds, in any key order. */
bool climate_parse_json(const char *payload, float *temperature_c)
{
    if (payload == NULL || temperature_c == NULL || !json_consume_char(&payload, '{'))
        return false;

    bool has_type = false, has_temp = false;
    for (int field = 0; field < 2; ++field)
    {
        char key[16], value[12];
        if (field > 0 && !json_consume_char(&payload, ','))
            return false;
        if (!json_parse_string(&payload, key, sizeof(key)) || !json_consume_char(&payload, ':'))
            return false;
        if (strcmp(key, "type") == 0)
        {
            if (has_type || !json_parse_string(&payload, value, sizeof(value)) ||
                strcmp(value, "climate") != 0)
                return false;
            has_type = true;
        }
        else if (strcmp(key, "temperature_c") == 0)
        {
            if (has_temp || !json_parse_number(&payload, temperature_c) ||
                !(*temperature_c >= -40.0f && *temperature_c <= 125.0f))
                return false;
            has_temp = true;
        }
        else
            return false;
    }
    return has_type && has_temp && json_parse_object_end(&payload);
}
