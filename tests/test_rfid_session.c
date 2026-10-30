/* RFID parking sessions: entry/exit rules, fees, capacity, receipt expiry and the event log. */
#include <string.h>

#include "parking_state.h"
#include "rfid_session.h"
#include "test.h"

int64_t fake_time_us;

#define SEC(s) ((uint64_t)(s) * 1000000ULL)

static rfid_event_t card(const char *id, rfid_gate_t gate)
{
    rfid_event_t e;
    memset(&e, 0, sizeof(e));
    strcpy(e.card_id, id);
    e.gate = gate;
    e.temperature_c = 28.0f;
    return e;
}

static rfid_scan_result_t scan(const char *id, rfid_gate_t gate, uint64_t at_s)
{
    rfid_event_t e = card(id, gate);
    return rfid_session_scan(&e, SEC(at_s));
}

static void test_fee(void)
{
    CHECK(rfid_fee_for_seconds(0) == 5000);       /* minimum one hour */
    CHECK(rfid_fee_for_seconds(1) == 5000);
    CHECK(rfid_fee_for_seconds(3600) == 5000);    /* exactly one hour */
    CHECK(rfid_fee_for_seconds(3601) == 10000);   /* every started hour */
    CHECK(rfid_fee_for_seconds(7200) == 10000);
    CHECK(rfid_fee_for_seconds(24 * 3600) == 24 * 5000);
    CHECK(rfid_fee_for_seconds(UINT64_MAX / 2) == UINT32_MAX); /* saturates, no overflow */
}

static void test_entry_exit_and_receipt(void)
{
    rfid_receipt_t r;
    fake_time_us = 0;
    rfid_session_get_receipt(&r);
    CHECK(!r.valid);

    CHECK(scan("CARD1", RFID_GATE_IN, 100) == RFID_SCAN_ENTERED);
    CHECK(scan("CARD1", RFID_GATE_IN, 110) == RFID_SCAN_INVALID_GATE);   /* already inside */
    CHECK(scan("CARD9", RFID_GATE_OUT, 120) == RFID_SCAN_INVALID_GATE);  /* never entered */
    CHECK(scan("CARD1", RFID_GATE_OUT, 100 + 3660) == RFID_SCAN_EXITED);

    fake_time_us = (int64_t)SEC(100 + 3660);
    rfid_session_get_receipt(&r);
    CHECK(r.valid && strcmp(r.card_id, "CARD1") == 0);
    CHECK(r.duration_seconds == 3660 && r.fee_vnd == 10000);

    /* The receipt stays on screen for 60 s, then disappears. */
    fake_time_us = (int64_t)SEC(100 + 3660 + 59);
    rfid_session_get_receipt(&r);
    CHECK(r.valid);
    fake_time_us = (int64_t)SEC(100 + 3660 + 60);
    rfid_session_get_receipt(&r);
    CHECK(!r.valid);

    /* After leaving, the same card can enter again. */
    CHECK(scan("CARD1", RFID_GATE_IN, 5000) == RFID_SCAN_ENTERED);
    CHECK(scan("CARD1", RFID_GATE_OUT, 5010) == RFID_SCAN_EXITED);
    fake_time_us = (int64_t)SEC(5010);
    rfid_session_get_receipt(&r);
    CHECK(r.fee_vnd == 5000 && r.duration_seconds == 10);
}

static void test_capacity_and_active_list(void)
{
    rfid_active_t active[RFID_MAX_ACTIVE];
    CHECK(rfid_session_get_active(active, RFID_MAX_ACTIVE) == 0);

    const char *ids[] = {"C0", "C1", "C2", "C3", "C4"};
    for (int i = 0; i < 5; ++i)
        CHECK(scan(ids[i], RFID_GATE_IN, 6000 + i) == RFID_SCAN_ENTERED);
    CHECK(scan("C5", RFID_GATE_IN, 6100) == RFID_SCAN_FULL);

    size_t n = rfid_session_get_active(active, RFID_MAX_ACTIVE);
    CHECK(n == 5);
    CHECK(strcmp(active[0].card_id, "C0") == 0 && active[0].entered_s == 6000);
    CHECK(rfid_session_get_active(active, 2) == 2); /* respects max */

    /* A freed slot is reused. */
    CHECK(scan("C2", RFID_GATE_OUT, 6200) == RFID_SCAN_EXITED);
    CHECK(scan("C5", RFID_GATE_IN, 6300) == RFID_SCAN_ENTERED);
    CHECK(rfid_session_get_active(active, RFID_MAX_ACTIVE) == 5);

    /* Scanning updates the temperature shown on the dashboard. */
    parking_state_t state;
    int free_count;
    rfid_event_t e = card("C5", RFID_GATE_OUT);
    e.temperature_c = 31.5f;
    CHECK(rfid_session_scan(&e, SEC(6400)) == RFID_SCAN_EXITED);
    parking_state_get(&state, &free_count);
    CHECK(state.temperature_received && state.temperature_c == 31.5f);

    for (int i = 0; i < 5; ++i)
        if (i != 2)
            scan(ids[i], RFID_GATE_OUT, 6500);
    CHECK(rfid_session_get_active(active, RFID_MAX_ACTIVE) == 0);
}

static void test_event_log(void)
{
    rfid_log_entry_t log[RFID_LOG_SIZE];
    CHECK(scan("LOGA", RFID_GATE_IN, 10000) == RFID_SCAN_ENTERED);
    CHECK(scan("LOGA", RFID_GATE_IN, 10001) == RFID_SCAN_INVALID_GATE);
    CHECK(scan("LOGA", RFID_GATE_OUT, 10000 + 3725) == RFID_SCAN_EXITED);

    size_t n = rfid_session_get_log(log, RFID_LOG_SIZE);
    CHECK(n == RFID_LOG_SIZE); /* earlier tests already filled the ring */
    CHECK(log[0].result == RFID_SCAN_EXITED && strcmp(log[0].card_id, "LOGA") == 0);
    CHECK(log[0].gate == RFID_GATE_OUT && log[0].at_s == 13725);
    CHECK(log[0].duration_s == 3725 && log[0].fee_vnd == 10000);
    CHECK(log[1].result == RFID_SCAN_INVALID_GATE && log[1].fee_vnd == 0 && log[1].duration_s == 0);
    CHECK(log[2].result == RFID_SCAN_ENTERED && log[2].at_s == 10000);

    /* Newest first, strictly ordered, and only the last RFID_LOG_SIZE kept. */
    for (int i = 0; i < 25; ++i)
        scan("RING", i % 2 ? RFID_GATE_OUT : RFID_GATE_IN, 20000 + i);
    n = rfid_session_get_log(log, RFID_LOG_SIZE);
    CHECK(n == RFID_LOG_SIZE);
    CHECK(log[0].at_s == 20024 && log[RFID_LOG_SIZE - 1].at_s == 20015);
    bool ordered = true;
    for (size_t i = 1; i < n; ++i)
        ordered &= log[i - 1].at_s > log[i].at_s;
    CHECK(ordered);
    CHECK(rfid_session_get_log(log, 3) == 3 && log[0].at_s == 20024);
    CHECK(rfid_session_get_log(NULL, 3) == 0);

    /* 25 alternating scans end with RING inside; drive it out so later tests start empty. */
    CHECK(scan("RING", RFID_GATE_OUT, 20030) == RFID_SCAN_EXITED);
    rfid_active_t active[RFID_MAX_ACTIVE];
    CHECK(rfid_session_get_active(active, RFID_MAX_ACTIVE) == 0);
}

static void test_rejection_notice(void)
{
    rfid_notice_t n;
    fake_time_us = (int64_t)SEC(30000);
    rfid_session_get_notice(&n);
    CHECK(!n.valid);

    /* Exit reader without entering: wrong-gate notice for 5 s, tagged with the reader used. */
    CHECK(scan("NOTE1", RFID_GATE_OUT, 30000) == RFID_SCAN_INVALID_GATE);
    rfid_session_get_notice(&n);
    CHECK(n.valid && n.result == RFID_SCAN_INVALID_GATE && n.gate == RFID_GATE_OUT);
    CHECK(strcmp(n.card_id, "NOTE1") == 0);
    fake_time_us = (int64_t)SEC(30004);
    rfid_session_get_notice(&n);
    CHECK(n.valid);
    fake_time_us = (int64_t)SEC(30005);
    rfid_session_get_notice(&n);
    CHECK(!n.valid);

    /* Entry reader while already inside. */
    CHECK(scan("NOTE1", RFID_GATE_IN, 30010) == RFID_SCAN_ENTERED);
    CHECK(scan("NOTE1", RFID_GATE_IN, 30011) == RFID_SCAN_INVALID_GATE);
    fake_time_us = (int64_t)SEC(30011);
    rfid_session_get_notice(&n);
    CHECK(n.valid && n.gate == RFID_GATE_IN);

    /* An accepted scan clears a stale rejection at once. */
    CHECK(scan("NOTE1", RFID_GATE_OUT, 30012) == RFID_SCAN_EXITED);
    fake_time_us = (int64_t)SEC(30012);
    rfid_session_get_notice(&n);
    CHECK(!n.valid);

    /* Lot full produces a FULL notice. */
    const char *ids[] = {"F0", "F1", "F2", "F3", "F4"};
    for (int i = 0; i < 5; ++i)
        CHECK(scan(ids[i], RFID_GATE_IN, 30100) == RFID_SCAN_ENTERED);
    CHECK(scan("F5", RFID_GATE_IN, 30101) == RFID_SCAN_FULL);
    fake_time_us = (int64_t)SEC(30101);
    rfid_session_get_notice(&n);
    CHECK(n.valid && n.result == RFID_SCAN_FULL && strcmp(n.card_id, "F5") == 0);
    for (int i = 0; i < 5; ++i)
        CHECK(scan(ids[i], RFID_GATE_OUT, 30200) == RFID_SCAN_EXITED);
}

int main(void)
{
    test_fee();
    test_entry_exit_and_receipt();
    test_capacity_and_active_list();
    test_event_log();
    test_rejection_notice();
    return TEST_REPORT("rfid_session");
}
