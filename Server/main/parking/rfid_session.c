#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "parking_state.h"
#include "rfid_session.h"

#define RFID_MAX_ACTIVE 5
#define PARKING_FEE_PER_HOUR_VND 5000
#define SECONDS_PER_HOUR 3600ULL
#define RECEIPT_DISPLAY_US 60000000ULL

typedef struct
{
    bool active;
    char card_id[RFID_CARD_ID_MAX + 1];
    uint64_t entered_us;
} active_session_t;

static active_session_t active_sessions[RFID_MAX_ACTIVE];
static rfid_receipt_t latest_receipt;
static portMUX_TYPE session_lock = portMUX_INITIALIZER_UNLOCKED;

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
        ++*p;
}

static bool consume_char(const char **p, char c)
{
    skip_ws(p);
    if (**p != c)
        return false;
    ++*p;
    return true;
}

static bool parse_string(const char **p, char *out, size_t out_size)
{
    skip_ws(p);
    if (**p != '"')
        return false;
    ++*p;

    size_t n = 0;
    while (**p && **p != '"')
    {
        unsigned char c = (unsigned char)**p;
        if (c == '\\' || c < 0x20 || n + 1 >= out_size)
            return false;
        out[n++] = (char)c;
        ++*p;
    }
    if (**p != '"')
        return false;
    ++*p;
    out[n] = '\0';
    return n > 0;
}

bool rfid_event_parse_json(const char *payload, rfid_event_t *event)
{
    if (!payload || !event || !consume_char(&payload, '{'))
        return false;

    bool has_type = false, has_id = false, has_temp = false;
    for (int field = 0; field < 3; ++field)
    {
        char key[20], value[RFID_CARD_ID_MAX + 1];
        if (!parse_string(&payload, key, sizeof(key)) ||
            !consume_char(&payload, ':'))
            return false;

        if (strcmp(key, "type") == 0)
        {
            if (has_type || !parse_string(&payload, value, sizeof(value)) ||
                strcmp(value, "rfid_event") != 0)
                return false;
            has_type = true;
        }
        else if (strcmp(key, "card_id") == 0)
        {
            if (has_id || !parse_string(&payload, event->card_id, sizeof(event->card_id)))
                return false;
            for (const char *c = event->card_id; *c; ++c)
                if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_')
                    return false;
            for (char *c = event->card_id; *c; ++c)
                *c = (char)toupper((unsigned char)*c);
            has_id = true;
        }
        else if (strcmp(key, "temperature_c") == 0)
        {
            if (has_temp)
                return false;
            skip_ws(&payload);
            char *end = NULL;
            event->temperature_c = strtof(payload, &end);
            if (end == payload || !(event->temperature_c >= -40.0f && event->temperature_c <= 125.0f))
                return false;
            payload = end;
            has_temp = true;
        }
        else
            return false;

        skip_ws(&payload);
        if (*payload == ',')
            ++payload;
        else if (*payload != '}')
            return false;
    }

    return has_type && has_id && has_temp && consume_char(&payload, '}') &&
           (skip_ws(&payload), *payload == '\0');
}

rfid_scan_result_t rfid_session_scan(const rfid_event_t *event, uint64_t now_us)
{
    if (!event)
        return RFID_SCAN_FULL;

    rfid_scan_result_t result = RFID_SCAN_ENTERED;
    portENTER_CRITICAL(&session_lock);
    int free_index = -1;
    for (int i = 0; i < RFID_MAX_ACTIVE; ++i)
    {
        if (active_sessions[i].active && strcmp(active_sessions[i].card_id, event->card_id) == 0)
        {
            uint64_t elapsed_seconds = (now_us - active_sessions[i].entered_us) / 1000000ULL;
            uint64_t billable_hours = (elapsed_seconds + SECONDS_PER_HOUR - 1) / SECONDS_PER_HOUR;
            if (billable_hours == 0)
                billable_hours = 1;
            latest_receipt.valid = true;
            memcpy(latest_receipt.card_id, event->card_id, strlen(event->card_id) + 1);
            latest_receipt.duration_seconds = elapsed_seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_seconds;
            latest_receipt.display_until_us = now_us + RECEIPT_DISPLAY_US;
            latest_receipt.fee_vnd = billable_hours > UINT32_MAX / PARKING_FEE_PER_HOUR_VND
                ? UINT32_MAX : (uint32_t)billable_hours * PARKING_FEE_PER_HOUR_VND;
            active_sessions[i].active = false;
            result = RFID_SCAN_EXITED;
            break;
        }
        if (!active_sessions[i].active && free_index < 0)
            free_index = i;
    }

    if (result == RFID_SCAN_ENTERED)
    {
        if (free_index < 0)
            result = RFID_SCAN_FULL;
        else
        {
            active_sessions[free_index].active = true;
            memcpy(active_sessions[free_index].card_id, event->card_id,
                   strlen(event->card_id) + 1);
            active_sessions[free_index].entered_us = now_us;
        }
    }
    portEXIT_CRITICAL(&session_lock);

    parking_state_set_temperature(event->temperature_c);
    return result;
}

void rfid_session_get_receipt(rfid_receipt_t *receipt)
{
    if (!receipt)
        return;
    portENTER_CRITICAL(&session_lock);
    memcpy(receipt, &latest_receipt, sizeof(*receipt));
    portEXIT_CRITICAL(&session_lock);
    if (receipt->valid && (uint64_t)esp_timer_get_time() >= receipt->display_until_us)
        receipt->valid = false;
}
