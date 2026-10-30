#ifndef RFID_SESSION_H
#define RFID_SESSION_H

#include <stdbool.h>
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

bool rfid_event_parse_json(const char *payload, rfid_event_t *event);
rfid_scan_result_t rfid_session_scan(const rfid_event_t *event, uint64_t now_us);
void rfid_session_get_receipt(rfid_receipt_t *receipt);

#endif
