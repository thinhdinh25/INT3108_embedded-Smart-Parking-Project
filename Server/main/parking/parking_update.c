#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "parking_update.h"

static void skip_json_whitespace(const char **cursor)
{
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\r' || **cursor == '\n')
        ++(*cursor);
}

static bool consume_json_char(const char **cursor, char expected)
{
    skip_json_whitespace(cursor);
    if (**cursor != expected)
        return false;
    ++(*cursor);
    return true;
}

static bool consume_json_string(const char **cursor, const char *expected)
{
    skip_json_whitespace(cursor);
    if (**cursor != '"')
        return false;
    ++(*cursor);
    while (*expected != '\0')
    {
        if (**cursor != *expected)
            return false;
        ++(*cursor);
        ++expected;
    }
    if (**cursor != '"')
        return false;
    ++(*cursor);
    return true;
}

bool parking_update_parse_json(const char *payload, parking_state_t *state)
{
    if (payload == NULL || state == NULL)
        return false;

    const char *cursor = payload;
    bool has_type = false;
    bool has_slots = false;
    bool has_temperature = false;
    bool has_water = false;
    bool has_fire = false;
    bool expect_field = false;
    if (!consume_json_char(&cursor, '{'))
        return false;

    for (int field = 0; field < 5; ++field)
    {
        skip_json_whitespace(&cursor);
        if (*cursor == '}')
        {
            if (expect_field)
                return false;
            break;
        }

        bool is_type = false;
        bool is_slots = false;
        bool is_temperature = false;
        bool is_water = false;
        bool is_fire = false;
        if (strncmp(cursor, "\"type\"", 6) == 0)
        {
            is_type = true;
            cursor += 6;
            if (has_type)
                return false;
            has_type = true;
        }
        else if (strncmp(cursor, "\"slots\"", 7) == 0)
        {
            is_slots = true;
            cursor += 7;
            if (has_slots)
                return false;
            has_slots = true;
        }
        else if (strncmp(cursor, "\"temperature_c\"", 15) == 0)
        {
            is_temperature = true;
            cursor += 15;
            if (has_temperature)
                return false;
            has_temperature = true;
        }
        else if (strncmp(cursor, "\"water_mm\"", 10) == 0)
        {
            is_water = true;
            cursor += 10;
            if (has_water)
                return false;
            has_water = true;
        }
        else if (strncmp(cursor, "\"fire\"", 6) == 0)
        {
            is_fire = true;
            cursor += 6;
            if (has_fire)
                return false;
            has_fire = true;
        }
        else
            return false;

        expect_field = false;
        if (!consume_json_char(&cursor, ':'))
            return false;
        if (is_type)
        {
            if (!consume_json_string(&cursor, "parking_update"))
                return false;
        }
        else if (is_slots)
        {
            if (!consume_json_char(&cursor, '['))
                return false;
            for (int i = 0; i < PARKING_SLOT_COUNT; ++i)
            {
                skip_json_whitespace(&cursor);
                if (*cursor != '0' && *cursor != '1')
                    return false;
                state->slots[i] = *cursor++ - '0';
                if (i < PARKING_SLOT_COUNT - 1 && !consume_json_char(&cursor, ','))
                    return false;
            }
            if (!consume_json_char(&cursor, ']'))
                return false;
        }
        else if (is_fire)
        {
            skip_json_whitespace(&cursor);
            if (strncmp(cursor, "true", 4) == 0)
            {
                state->fire = true;
                cursor += 4;
            }
            else if (strncmp(cursor, "false", 5) == 0)
            {
                state->fire = false;
                cursor += 5;
            }
            else
                return false;
        }
        else
        {
            skip_json_whitespace(&cursor);
            if ((*cursor < '0' || *cursor > '9') && *cursor != '-')
                return false;
            char *end = NULL;
            float value = strtof(cursor, &end);
            if (end == cursor)
                return false;
            cursor = end;
            if (is_temperature)
            {
                if (!(value >= -40.0f && value <= 125.0f))
                    return false;
                state->temperature_c = value;
            }
            else if (is_water)
            {
                if (!(value >= 0.0f && value <= 10000.0f))
                    return false;
                state->water_mm = value;
            }
        }

        skip_json_whitespace(&cursor);
        if (*cursor == ',')
        {
            if (field == 4)
                return false;
            ++cursor;
            expect_field = true;
        }
        else if (*cursor != '}')
            return false;
    }

    if (!has_type || !has_slots || !has_temperature || !has_water ||
        !consume_json_char(&cursor, '}'))
        return false;
    (void)has_fire; /* Older parking clients may omit the fire sensor field. */
    skip_json_whitespace(&cursor);
    return *cursor == '\0';
}
