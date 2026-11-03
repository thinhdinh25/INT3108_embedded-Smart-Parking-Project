#include <ctype.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "json_scan.h"
#include "parking_state.h"
#include "rfid_session.h"

#define SECONDS_PER_HOUR 3600ULL
#define RECEIPT_DISPLAY_US 60000000ULL
#define NOTICE_DISPLAY_US 5000000ULL

static rfid_store_t store = {.version = RFID_STORE_VERSION};
static rfid_receipt_t latest_receipt;
static rfid_notice_t latest_notice;
static bool clock_set_this_boot;
static int64_t unsynced_from_us; /* records stamped at or after this time predate the clock being set */
static portMUX_TYPE session_lock = portMUX_INITIALIZER_UNLOCKED;

bool rfid_event_parse_json(const char *payload, rfid_event_t *event)
{
    if (!payload || !event || !json_consume_char(&payload, '{'))
        return false;

    bool has_type = false, has_id = false, has_gate = false, has_temp = false, has_seq = false;
    for (int field = 0; field < 5; ++field)
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
        else if (strcmp(key, "seq") == 0)
        {
            if (has_seq || !json_parse_uint32(&payload, &event->seq) || event->seq == 0)
                return false;
            has_seq = true;
        }
        else
            return false;

        json_skip_ws(&payload);
        if (*payload == ',')
        {
            if (field == 4)
                return false;
            ++payload;
        }
        else if (*payload == '}')
            break;
        else
            return false;
    }

    return has_type && has_id && has_gate && has_temp && has_seq && json_parse_object_end(&payload);
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
    rfid_log_entry_t *entry = &store.log[store.log_next];
    memcpy(entry->card_id, event->card_id, strlen(event->card_id) + 1);
    entry->result = result;
    entry->gate = event->gate;
    entry->at_s = (uint32_t)(now_us / 1000000ULL);
    entry->duration_s = duration_s;
    entry->fee_vnd = fee_vnd;
    store.log_next = (store.log_next + 1) % RFID_LOG_SIZE;
    if (store.log_count < RFID_LOG_SIZE)
        ++store.log_count;
}

#define SECONDS_PER_DAY 86400LL
#define LOCAL_OFFSET_S (7 * 3600LL) /* Vietnam, UTC+7 */

static uint32_t day_of(int64_t clock_us)
{
    const int64_t local_s = clock_us / 1000000LL + LOCAL_OFFSET_S;
    return local_s > 0 ? (uint32_t)(local_s / SECONDS_PER_DAY) : 0;
}

/* Caller holds session_lock. The bucket for `day`, or the oldest one reset for it. */
static rfid_day_stats_t *day_bucket(uint32_t day)
{
    rfid_day_stats_t *oldest = &store.days[0];
    for (int i = 0; i < RFID_STATS_DAYS; ++i)
    {
        if (store.days[i].day == day)
            return &store.days[i];
        if (store.days[i].day < oldest->day)
            oldest = &store.days[i];
    }
    *oldest = (rfid_day_stats_t){.day = day};
    return oldest;
}

rfid_scan_result_t rfid_session_scan(const rfid_event_t *event, uint64_t now_us)
{
    if (!event)
        return RFID_SCAN_FULL;

    /* Cross-check with the IR sensors: a car that entered without a card still takes a bay. */
    parking_state_t lot;
    int free_bays;
    parking_state_get(&lot, &free_bays);
    const bool bays_full = lot.online && free_bays == 0; /* stale data must not block entry */

    rfid_scan_result_t result = RFID_SCAN_ENTERED;
    portENTER_CRITICAL(&session_lock);
    int active_index = -1;
    int free_index = -1;
    for (int i = 0; i < RFID_MAX_ACTIVE; ++i)
    {
        if (store.sessions[i].active && strcmp(store.sessions[i].card_id, event->card_id) == 0)
        {
            active_index = i;
            break;
        }
        if (!store.sessions[i].active && free_index < 0)
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
            uint64_t entered_us = store.sessions[active_index].entered_us;
            uint64_t elapsed_seconds = now_us > entered_us ? (now_us - entered_us) / 1000000ULL : 0;
            latest_receipt.valid = true;
            memcpy(latest_receipt.card_id, event->card_id, strlen(event->card_id) + 1);
            latest_receipt.duration_seconds = elapsed_seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_seconds;
            latest_receipt.display_until_us = now_us + RECEIPT_DISPLAY_US;
            latest_receipt.fee_vnd = rfid_fee_for_seconds(elapsed_seconds);
            store.sessions[active_index].active = false;
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
        else if (lot.flood)
        {
            result = RFID_SCAN_FLOOD;
        }
        else if (free_index < 0 || bays_full)
        {
            result = RFID_SCAN_FULL;
        }
        else
        {
            store.sessions[free_index].active = true;
            memcpy(store.sessions[free_index].card_id, event->card_id,
                   strlen(event->card_id) + 1);
            store.sessions[free_index].entered_us = now_us;
            result = RFID_SCAN_ENTERED;
        }
    }
    if (result == RFID_SCAN_EXITED)
        log_event(event, result, now_us, latest_receipt.duration_seconds, latest_receipt.fee_vnd);
    else
        log_event(event, result, now_us, 0, 0);

    if (result == RFID_SCAN_ENTERED || result == RFID_SCAN_EXITED)
    {
        rfid_day_stats_t *day = day_bucket(day_of((int64_t)now_us));
        if (result == RFID_SCAN_ENTERED)
            ++day->entries;
        else
        {
            ++day->exits;
            day->revenue_vnd = day->revenue_vnd > UINT32_MAX - latest_receipt.fee_vnd
                ? UINT32_MAX : day->revenue_vnd + latest_receipt.fee_vnd;
        }
    }

    if (result == RFID_SCAN_FULL || result == RFID_SCAN_INVALID_GATE || result == RFID_SCAN_FLOOD)
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
    if (receipt->valid && (uint64_t)parking_clock_now_us() >= receipt->display_until_us)
        receipt->valid = false;
}

void rfid_session_get_notice(rfid_notice_t *notice)
{
    if (!notice)
        return;
    portENTER_CRITICAL(&session_lock);
    memcpy(notice, &latest_notice, sizeof(*notice));
    portEXIT_CRITICAL(&session_lock);
    if (notice->valid && (uint64_t)parking_clock_now_us() >= notice->display_until_us)
        notice->valid = false;
}

size_t rfid_session_get_log(rfid_log_entry_t *out, size_t max)
{
    if (!out)
        return 0;
    portENTER_CRITICAL(&session_lock);
    size_t n = store.log_count < max ? store.log_count : max;
    for (size_t i = 0; i < n; ++i)
        out[i] = store.log[(store.log_next + RFID_LOG_SIZE - 1 - i) % RFID_LOG_SIZE];
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
        if (!store.sessions[i].active)
            continue;
        memcpy(out[n].card_id, store.sessions[i].card_id, sizeof(out[n].card_id));
        out[n].entered_s = (uint32_t)(store.sessions[i].entered_us / 1000000ULL);
        ++n;
    }
    portEXIT_CRITICAL(&session_lock);
    return n;
}

bool rfid_session_check_seq(uint32_t seq)
{
    bool fresh = true;
    portENTER_CRITICAL(&session_lock);
    for (int i = 0; i < RFID_RECENT_SEQS; ++i)
        fresh &= store.recent_seqs[i] != seq;
    if (fresh)
    {
        store.recent_seqs[store.recent_next] = seq;
        store.recent_next = (store.recent_next + 1) % RFID_RECENT_SEQS;
    }
    portEXIT_CRITICAL(&session_lock);
    return fresh;
}

void rfid_session_snapshot(rfid_store_t *out)
{
    const int64_t now_us = parking_clock_now_us();
    portENTER_CRITICAL(&session_lock);
    *out = store;
    portEXIT_CRITICAL(&session_lock);
    out->clock_us = now_us;
}

void rfid_session_restore(const rfid_store_t *saved)
{
    if (!saved || saved->version != RFID_STORE_VERSION || saved->log_next >= RFID_LOG_SIZE ||
        saved->log_count > RFID_LOG_SIZE || saved->recent_next >= RFID_RECENT_SEQS)
        return;
    /* Downtime is unknown, so the clock resumes just after it stopped (on a new whole second,
     * so even second-resolution log times tell old from new) until a browser sets it.
     * If the saved times were never Unix time, all of them move when it is set. */
    const int64_t resume_us = (saved->clock_us / 1000000LL + 1) * 1000000LL;
    portENTER_CRITICAL(&session_lock);
    store = *saved;
    unsynced_from_us = saved->clock_synced ? resume_us : 0;
    clock_set_this_boot = false;
    portEXIT_CRITICAL(&session_lock);
    parking_clock_adjust(resume_us - parking_clock_now_us());
}

static uint64_t shifted(uint64_t t, int64_t delta)
{
    return delta < 0 && (uint64_t)-delta > t ? 0 : t + (uint64_t)delta;
}

void rfid_session_set_clock(uint64_t unix_s)
{
    const int64_t delta = (int64_t)(unix_s * 1000000ULL) - parking_clock_now_us();
    parking_clock_adjust(delta);
    portENTER_CRITICAL(&session_lock);
    if (!clock_set_this_boot)
    {
        for (int i = 0; i < RFID_MAX_ACTIVE; ++i)
            if (store.sessions[i].active && (int64_t)store.sessions[i].entered_us >= unsynced_from_us)
                store.sessions[i].entered_us = shifted(store.sessions[i].entered_us, delta);
        for (int i = 0; i < store.log_count; ++i)
            if ((int64_t)store.log[i].at_s * 1000000LL >= unsynced_from_us)
                store.log[i].at_s = (uint32_t)(shifted(store.log[i].at_s * 1000000ULL, delta) / 1000000ULL);
        latest_receipt.display_until_us = shifted(latest_receipt.display_until_us, delta);
        latest_notice.display_until_us = shifted(latest_notice.display_until_us, delta);
        for (int i = 0; i < RFID_STATS_DAYS; ++i)
        {
            rfid_day_stats_t *day = &store.days[i];
            const int64_t start_us = ((int64_t)day->day * SECONDS_PER_DAY - LOCAL_OFFSET_S) * 1000000LL;
            if ((day->entries || day->exits) && (unsynced_from_us == 0 || start_us >= unsynced_from_us))
                day->day = day_of(start_us + delta);
        }
        store.clock_synced = true;
        clock_set_this_boot = true;
    }
    portEXIT_CRITICAL(&session_lock);
}

void rfid_session_get_stats(rfid_day_stats_t *today, rfid_day_stats_t *week)
{
    const uint32_t now_day = day_of(parking_clock_now_us());
    rfid_day_stats_t t = {.day = now_day}, w = {.day = now_day};
    portENTER_CRITICAL(&session_lock);
    for (int i = 0; i < RFID_STATS_DAYS; ++i)
    {
        const rfid_day_stats_t *d = &store.days[i];
        if (d->day > now_day || now_day - d->day >= RFID_STATS_DAYS)
            continue;
        if (d->day == now_day)
            t = *d;
        w.revenue_vnd = w.revenue_vnd > UINT32_MAX - d->revenue_vnd ? UINT32_MAX : w.revenue_vnd + d->revenue_vnd;
        w.entries += d->entries;
        w.exits += d->exits;
    }
    portEXIT_CRITICAL(&session_lock);
    if (today)
        *today = t;
    if (week)
        *week = w;
}
