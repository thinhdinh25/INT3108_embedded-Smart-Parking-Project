#ifndef RFID_SESSION_H
#define RFID_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#define RFID_CARD_ID_MAX 31

typedef struct
{
    char card_id[RFID_CARD_ID_MAX + 1];
    float temperature_c;
} rfid_event_t;

typedef struct
{
    bool valid;
    char card_id[RFID_CARD_ID_MAX + 1];
    uint32_t fee_vnd;
    uint32_t duration_seconds;
    uint32_t time_in_seconds;
    uint32_t time_out_seconds;
} rfid_receipt_t;

typedef enum
{
    RFID_SCAN_ENTERED,
    RFID_SCAN_EXITED,
    RFID_SCAN_FULL,
} rfid_scan_result_t;

bool rfid_event_parse_json(const char *payload, rfid_event_t *event);
rfid_scan_result_t rfid_session_scan(const rfid_event_t *event, uint64_t now_us);
void rfid_session_get_receipt(rfid_receipt_t *receipt);

#endif
