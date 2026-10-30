#ifndef RFID_SESSION_H
#define RFID_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFID_CARD_ID_MAX 31

typedef enum
{
    RFID_GATE_IN,
    RFID_GATE_OUT,
} rfid_gate_t;

typedef struct
{
    char card_id[RFID_CARD_ID_MAX + 1];
    rfid_gate_t gate;
    float temperature_c;
} rfid_event_t;

typedef struct
{
    bool valid;
    char card_id[RFID_CARD_ID_MAX + 1];
    uint32_t fee_vnd;
    uint32_t duration_seconds;
    uint64_t display_until_us;
} rfid_receipt_t;

typedef enum
{
    RFID_SCAN_ENTERED,
    RFID_SCAN_EXITED,
    RFID_SCAN_FULL,
    RFID_SCAN_INVALID_GATE,
} rfid_scan_result_t;

/* Short on-screen notice for a rejected scan (wrong gate or lot full). */
typedef struct
{
    bool valid;
    char card_id[RFID_CARD_ID_MAX + 1];
    rfid_scan_result_t result; /* RFID_SCAN_FULL or RFID_SCAN_INVALID_GATE */
    rfid_gate_t gate;          /* reader the card was scanned on */
    uint64_t display_until_us;
} rfid_notice_t;

#define RFID_MAX_ACTIVE 5
#define RFID_LOG_SIZE 10
#define PARKING_FEE_PER_HOUR_VND 5000

/* Times are seconds since boot; the dashboard converts them to wall-clock time. */
typedef struct
{
    char card_id[RFID_CARD_ID_MAX + 1];
    rfid_scan_result_t result;
    rfid_gate_t gate;
    uint32_t at_s;
    uint32_t duration_s; /* exits only */
    uint32_t fee_vnd;    /* exits only */
} rfid_log_entry_t;

typedef struct
{
    char card_id[RFID_CARD_ID_MAX + 1];
    uint32_t entered_s;
} rfid_active_t;

bool rfid_event_parse_json(const char *payload, rfid_event_t *event);
rfid_scan_result_t rfid_session_scan(const rfid_event_t *event, uint64_t now_us);
void rfid_session_get_receipt(rfid_receipt_t *receipt);
void rfid_session_get_notice(rfid_notice_t *notice);
uint32_t rfid_fee_for_seconds(uint64_t seconds);
/* Newest first; returns the number of entries copied. */
size_t rfid_session_get_log(rfid_log_entry_t *out, size_t max);
size_t rfid_session_get_active(rfid_active_t *out, size_t max);

#endif
