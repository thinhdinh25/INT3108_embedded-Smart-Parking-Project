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
    uint32_t seq; /* chosen by the RFID Client; a retry repeats it */
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
    RFID_SCAN_FLOOD, /* entry refused while the flood alarm is on; exits still work */
    RFID_SCAN_AWAITING_PAYMENT, /* exit with QR payment on: QR shown, barrier stays closed */
    RFID_SCAN_PAY_BUSY,         /* another car is still paying at the exit */
    RFID_SCAN_PAY_TIMEOUT,      /* notice only: no transfer arrived in time, pay at the booth */
} rfid_scan_result_t;

/* Short on-screen notice for a rejected scan (wrong gate or lot full). */
typedef struct
{
    bool valid;
    char card_id[RFID_CARD_ID_MAX + 1];
    rfid_scan_result_t result; /* a rejection, or RFID_SCAN_PAY_TIMEOUT */
    rfid_gate_t gate;          /* reader the card was scanned on */
    uint64_t display_until_us;
} rfid_notice_t;

#define RFID_MAX_ACTIVE 5
#define RFID_LOG_SIZE 10
#define PARKING_FEE_PER_HOUR_VND 5000

/* Times are parking-clock seconds (parking_clock_now_us): Unix time once a browser has set
 * the clock, otherwise counted from the first boot and carried across reboots. */
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

/* Revenue and visits for one local day (UTC+7 once the clock is set). */
#define RFID_STATS_DAYS 7
typedef struct
{
    uint32_t day; /* parking-clock day number */
    uint32_t revenue_vnd;
    uint16_t entries;
    uint16_t exits;
} rfid_day_stats_t;

/* Everything that must survive a reboot; it is also the module's live state, so a snapshot
 * is one copy. parking_persist.c stores it in NVS. */
#define RFID_STORE_VERSION 3
#define RFID_RECENT_SEQS 16
typedef struct
{
    uint32_t version;
    int64_t clock_us;  /* parking clock when the snapshot was taken */
    bool clock_synced; /* times are Unix time (set by a browser at least once) */
    uint32_t recent_seqs[RFID_RECENT_SEQS]; /* scans already acted on, to ignore retries */
    uint8_t recent_next;
    struct
    {
        bool active;
        char card_id[RFID_CARD_ID_MAX + 1];
        uint64_t entered_us;
    } sessions[RFID_MAX_ACTIVE];
    rfid_log_entry_t log[RFID_LOG_SIZE]; /* ring buffer */
    uint8_t log_next;
    uint8_t log_count;
    rfid_day_stats_t days[RFID_STATS_DAYS]; /* any order; the oldest is reused for a new day */
} rfid_store_t;

/* The one exit payment in progress (single exit lane). */
#define RFID_PAY_CODE_LEN 8 /* "PK" + 6 characters, written in the transfer note */
typedef struct
{
    bool active;
    bool expired; /* time limit passed: TFT says pay at the booth, a late transfer still counts */
    char card_id[RFID_CARD_ID_MAX + 1];
    char code[RFID_PAY_CODE_LEN + 1];
    uint32_t fee_vnd;
    uint32_t duration_s;
    uint64_t expires_us;
} rfid_payment_t;

bool rfid_event_parse_json(const char *payload, rfid_event_t *event);
rfid_scan_result_t rfid_session_scan(const rfid_event_t *event, uint64_t now_us);
void rfid_session_get_receipt(rfid_receipt_t *receipt);
void rfid_session_get_notice(rfid_notice_t *notice);
uint32_t rfid_fee_for_seconds(uint64_t seconds);
/* Newest first; returns the number of entries copied. */
size_t rfid_session_get_log(rfid_log_entry_t *out, size_t max);
size_t rfid_session_get_active(rfid_active_t *out, size_t max);
/* Each scan is resent by the RFID Client until ACKed. True the first time a seq is seen
 * (and remembers it); false for a retry, which must be ACKed again but not acted on. */
bool rfid_session_check_seq(uint32_t seq);
void rfid_session_snapshot(rfid_store_t *out);
void rfid_session_restore(const rfid_store_t *saved);
/* Sets the parking clock to Unix time; times stamped before the first set are shifted with it. */
void rfid_session_set_clock(uint64_t unix_s);
/* timeout_s > 0: an exit scan starts a QR payment instead of opening the barrier.
 * 0 (default): exits open at once. */
void rfid_session_enable_payment(uint32_t timeout_s);
void rfid_session_get_payment(rfid_payment_t *out);
/* A transfer with this code arrived: completes that exit. False if no payment has it. */
bool rfid_session_payment_done(const char *code);
/* Paid in cash at the booth: completes the exit of a card that is inside. */
bool rfid_session_pay_cash(const char *card_id);

/* Today's totals and the totals of the last RFID_STATS_DAYS days, including today. */
void rfid_session_get_stats(rfid_day_stats_t *today, rfid_day_stats_t *week);

#endif
