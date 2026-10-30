#include <ctype.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "json_scan.h"
#include "parking_state.h"
#include "rfid_session.h"

#define SECONDS_PER_HOUR 3600ULL
#define RECEIPT_DISPLAY_US 60000000ULL
#define NOTICE_DISPLAY_US 5000000ULL

typedef struct
{
    bool active;
    char card_id[RFID_CARD_ID_MAX + 1];
    uint64_t entered_us;
} active_session_t;

static active_session_t active_sessions[RFID_MAX_ACTIVE];
static rfid_receipt_t latest_receipt;
static rfid_notice_t latest_notice;
static rfid_log_entry_t event_log[RFID_LOG_SIZE]; /* ring buffer */
static size_t log_next;
static size_t log_count;
static portMUX_TYPE session_lock = portMUX_INITIALIZER_UNLOCKED;

bool rfid_event_parse_json(const char *payload, rfid_event_t *event)
{
    if (!payload || !event || !json_consume_char(&payload, '{'))
        return false;

    bool has_type = false, has_id = false, has_gate = false, has_temp = false;
    for (int field = 0; field < 4; ++field)
    {
        char key[20], value[RFID_CARD_ID_MAX + 1];
        if (!json_parse_string(&payload, key, sizeof(key)) ||
            !json_consume_char(&payload, ':'))
            return false;

        if (strcmp(key, "type") == 0)
        {
            if (has_type || !json_parse_string(&payload, value, sizeof(value)) ||
                strcmp(value, "rfid_event") != 0)
                return false;
            has_type = true;
        }
        else if (strcmp(key, "card_id") == 0)
        {
            if (has_id || !json_parse_string(&payload, event->card_id, sizeof(event->card_id)))
                return false;
            for (const char *c = event->card_id; *c; ++c)
                if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_')
                    return false;
            for (char *c = event->card_id; *c; ++c)
                *c = (char)toupper((unsigned char)*c);
            has_id = true;
        }
        else if (strcmp(key, "gate") == 0)
        {
            if (has_gate || !json_parse_string(&payload, value, sizeof(value)))
                return false;
            for (char *c = value; *c; ++c)
                *c = (char)tolower((unsigned char)*c);
            if (strcmp(value, "in") == 0)
                event->gate = RFID_GATE_IN;
            else if (strcmp(value, "out") == 0)
                event->gate = RFID_GATE_OUT;
            else
                return false;
            has_gate = true;
        }
        else if (strcmp(key, "temperature_c") == 0)
        {
            if (has_temp || !json_parse_number(&payload, &event->temperature_c) ||
                !(event->temperature_c >= -40.0f && event->temperature_c <= 125.0f))
                return false;
            has_temp = true;
        }
        else
            return false;

        json_skip_ws(&payload);
        if (*payload == ',')
        {
            if (field == 3)
                return false;
            ++payload;
        }
        else if (*payload == '}')
            break;
        else
            return false;
    }

    return has_type && has_id && has_gate && has_temp && json_parse_object_end(&payload);
}

uint32_t rfid_fee_for_seconds(uint64_t seconds)
{
    /* Every started hour is billed, minimum one hour. */
    uint64_t billable_hours = (seconds + SECONDS_PER_HOUR - 1) / SECONDS_PER_HOUR;
    if (billable_hours == 0)
        billable_hours = 1;
    return billable_hours > UINT32_MAX / PARKING_FEE_PER_HOUR_VND
        ? UINT32_MAX : (uint32_t)billable_hours * PARKING_FEE_PER_HOUR_VND;
}

/* Caller holds session_lock. */
static void log_event(const rfid_event_t *event, rfid_scan_result_t result, uint64_t now_us,
                      uint32_t duration_s, uint32_t fee_vnd)
{
    rfid_log_entry_t *entry = &event_log[log_next];
    memcpy(entry->card_id, event->card_id, strlen(event->card_id) + 1);
    entry->result = result;
    entry->gate = event->gate;
    entry->at_s = (uint32_t)(now_us / 1000000ULL);
    entry->duration_s = duration_s;
    entry->fee_vnd = fee_vnd;
    log_next = (log_next + 1) % RFID_LOG_SIZE;
    if (log_count < RFID_LOG_SIZE)
        ++log_count;
}

rfid_scan_result_t rfid_session_scan(const rfid_event_t *event, uint64_t now_us)
{
    if (!event)
        return RFID_SCAN_FULL;

    rfid_scan_result_t result = RFID_SCAN_ENTERED;
    portENTER_CRITICAL(&session_lock);
    int active_index = -1;
    int free_index = -1;
    for (int i = 0; i < RFID_MAX_ACTIVE; ++i)
    {
        if (active_sessions[i].active && strcmp(active_sessions[i].card_id, event->card_id) == 0)
        {
            active_index = i;
            break;
        }
        if (!active_sessions[i].active && free_index < 0)
            free_index = i;
    }

    if (active_index >= 0)
    {
        /* Card is currently inside: can exit if and only if scanned on the OUT reader. */
        if (event->gate != RFID_GATE_OUT)
        {
            result = RFID_SCAN_INVALID_GATE;
        }
        else
        {
            uint64_t elapsed_seconds = (now_us - active_sessions[active_index].entered_us) / 1000000ULL;
            latest_receipt.valid = true;
            memcpy(latest_receipt.card_id, event->card_id, strlen(event->card_id) + 1);
            latest_receipt.duration_seconds = elapsed_seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_seconds;
            latest_receipt.display_until_us = now_us + RECEIPT_DISPLAY_US;
            latest_receipt.fee_vnd = rfid_fee_for_seconds(elapsed_seconds);
            active_sessions[active_index].active = false;
            result = RFID_SCAN_EXITED;
        }
    }
    else
    {
        /* Card is currently outside: can enter if and only if scanned on the IN reader. */
        if (event->gate != RFID_GATE_IN)
        {
            result = RFID_SCAN_INVALID_GATE;
        }
        else if (free_index < 0)
        {
            result = RFID_SCAN_FULL;
        }
        else
        {
            active_sessions[free_index].active = true;
            memcpy(active_sessions[free_index].card_id, event->card_id,
                   strlen(event->card_id) + 1);
            active_sessions[free_index].entered_us = now_us;
            result = RFID_SCAN_ENTERED;
        }
    }
    if (result == RFID_SCAN_EXITED)
        log_event(event, result, now_us, latest_receipt.duration_seconds, latest_receipt.fee_vnd);
    else
        log_event(event, result, now_us, 0, 0);

    if (result == RFID_SCAN_FULL || result == RFID_SCAN_INVALID_GATE)
    {
        latest_notice.valid = true;
        memcpy(latest_notice.card_id, event->card_id, strlen(event->card_id) + 1);
        latest_notice.result = result;
        latest_notice.gate = event->gate;
        latest_notice.display_until_us = now_us + NOTICE_DISPLAY_US;
    }
    else
        latest_notice.valid = false; /* an accepted scan replaces a stale rejection */
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

void rfid_session_get_notice(rfid_notice_t *notice)
{
    if (!notice)
        return;
    portENTER_CRITICAL(&session_lock);
    memcpy(notice, &latest_notice, sizeof(*notice));
    portEXIT_CRITICAL(&session_lock);
    if (notice->valid && (uint64_t)esp_timer_get_time() >= notice->display_until_us)
        notice->valid = false;
}

size_t rfid_session_get_log(rfid_log_entry_t *out, size_t max)
{
    if (!out)
        return 0;
    portENTER_CRITICAL(&session_lock);
    size_t n = log_count < max ? log_count : max;
    for (size_t i = 0; i < n; ++i)
        out[i] = event_log[(log_next + RFID_LOG_SIZE - 1 - i) % RFID_LOG_SIZE];
    portEXIT_CRITICAL(&session_lock);
    return n;
}

size_t rfid_session_get_active(rfid_active_t *out, size_t max)
{
    if (!out)
        return 0;
    size_t n = 0;
    portENTER_CRITICAL(&session_lock);
    for (int i = 0; i < RFID_MAX_ACTIVE && n < max; ++i)
    {
        if (!active_sessions[i].active)
            continue;
        memcpy(out[n].card_id, active_sessions[i].card_id, sizeof(out[n].card_id));
        out[n].entered_s = (uint32_t)(active_sessions[i].entered_us / 1000000ULL);
        ++n;
    }
    portEXIT_CRITICAL(&session_lock);
    return n;
}
