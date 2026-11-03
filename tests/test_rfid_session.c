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

static void test_ir_cross_check(void)
{
    /* Every bay is taken by a car the RFID system does not know about: no entry. */
    parking_state_t lot = {.slots = {1, 1, 1, 1, 1}};
    parking_state_update(&lot);
    CHECK(scan("IR1", RFID_GATE_IN, 40000) == RFID_SCAN_FULL);
    lot.slots[2] = 0;
    parking_state_update(&lot);
    CHECK(scan("IR1", RFID_GATE_IN, 40001) == RFID_SCAN_ENTERED);
    CHECK(scan("IR1", RFID_GATE_OUT, 40002) == RFID_SCAN_EXITED);
    lot.slots[0] = lot.slots[1] = lot.slots[3] = lot.slots[4] = 0;
    parking_state_update(&lot);
}

static void test_persistence_and_clock(void)
{
    rfid_active_t active[RFID_MAX_ACTIVE];
    rfid_log_entry_t log[RFID_LOG_SIZE];
    rfid_store_t saved;

    /* Never-set clock: times count from the first boot. */
    fake_time_us = (int64_t)SEC(50000);
    CHECK(scan("KEEP", RFID_GATE_IN, 49900) == RFID_SCAN_ENTERED);
    CHECK(rfid_session_check_seq(7));
    rfid_session_snapshot(&saved);
    CHECK(saved.clock_us == (int64_t)SEC(50000) && !saved.clock_synced);

    /* Power cut: uptime restarts, the clock resumes on the next second and the car is still in. */
    fake_time_us = (int64_t)SEC(2);
    rfid_session_restore(&saved);
    CHECK(parking_clock_now_us() == (int64_t)SEC(50001));
    CHECK(rfid_session_get_active(active, RFID_MAX_ACTIVE) == 1 && strcmp(active[0].card_id, "KEEP") == 0);
    CHECK(!rfid_session_check_seq(7)); /* seen scans survive too, so a retry is not re-applied */

    /* A browser sets Unix time: every stored time moves with the clock, durations stay right. */
    const uint64_t T = 1700000000ULL;
    fake_time_us = (int64_t)SEC(62); /* clock now 50061 */
    rfid_session_set_clock(T);
    CHECK(parking_clock_now_us() == (int64_t)SEC(T));
    rfid_session_get_active(active, RFID_MAX_ACTIVE);
    CHECK(active[0].entered_s == T - 161);
    rfid_session_get_log(log, 1);
    CHECK(log[0].at_s == T - 161 && strcmp(log[0].card_id, "KEEP") == 0);

    /* A second set in the same boot only corrects drift, it does not move records again. */
    rfid_session_set_clock(T + 5);
    rfid_session_get_active(active, RFID_MAX_ACTIVE);
    CHECK(active[0].entered_s == T - 161);
    CHECK(scan("KEEP", RFID_GATE_OUT, T + 5) == RFID_SCAN_EXITED);
    rfid_session_get_log(log, 1);
    CHECK(log[0].duration_s == 166 && log[0].fee_vnd == 5000);

    /* Unix-time snapshot, reboot, a car enters before any browser connects, then the clock
     * is set with the real downtime: only times stamped since the reboot move. */
    CHECK(scan("OLD", RFID_GATE_IN, T + 5) == RFID_SCAN_ENTERED);
    rfid_session_snapshot(&saved);
    CHECK(saved.clock_synced);
    fake_time_us = (int64_t)SEC(1);
    rfid_session_restore(&saved);          /* clock resumes at T+6 */
    fake_time_us = (int64_t)SEC(11);
    CHECK(scan("NEW", RFID_GATE_IN, T + 16) == RFID_SCAN_ENTERED);
    rfid_session_set_clock(T + 3600 + 16); /* the server was off for an hour */
    size_t n = rfid_session_get_active(active, RFID_MAX_ACTIVE);
    bool old_kept = false, new_moved = false;
    for (size_t i = 0; i < n; ++i)
    {
        old_kept |= strcmp(active[i].card_id, "OLD") == 0 && active[i].entered_s == T + 5;
        new_moved |= strcmp(active[i].card_id, "NEW") == 0 && active[i].entered_s == T + 3600 + 16;
    }
    CHECK(n == 2 && old_kept && new_moved);

    /* A corrupt or older-format save is ignored. */
    saved.version = 0;
    rfid_session_restore(&saved);
    CHECK(rfid_session_get_active(active, RFID_MAX_ACTIVE) == 2);
}

static void test_retry_dedupe(void)
{
    /* A retried scan (same seq) is acted on once; the last 16 seqs are remembered. */
    CHECK(rfid_session_check_seq(1000));
    CHECK(!rfid_session_check_seq(1000));
    CHECK(rfid_session_check_seq(1001));
    for (uint32_t s = 2000; s < 2016; ++s)
        rfid_session_check_seq(s);
    CHECK(rfid_session_check_seq(1000)); /* pushed out: a much later reuse is a new scan */
}

static uint64_t clock_s(void)
{
    return (uint64_t)parking_clock_now_us() / 1000000ULL;
}

static void test_offline_and_flood(void)
{
    parking_state_t st, lot = {.water_mm = 0};
    int free_count;

    /* The sensor counts as online for 3 s after its last update. */
    fake_time_us = (int64_t)SEC(100000);
    parking_state_update(&lot);
    fake_time_us = (int64_t)SEC(100000) + 2999999;
    parking_state_get(&st, &free_count);
    CHECK(st.online);
    fake_time_us = (int64_t)SEC(100003);
    parking_state_get(&st, &free_count);
    CHECK(st.received && !st.online);

    /* Flood alarm: on at 20 mm, stays on down to 15 mm (hysteresis). */
    const float levels[] = {19.9f, 20.0f, 17.0f, 15.1f, 15.0f};
    const bool expected[] = {false, true, true, true, false};
    bool hysteresis_ok = true;
    for (int i = 0; i < 5; ++i)
    {
        lot.water_mm = levels[i];
        parking_state_update(&lot);
        parking_state_get(&st, &free_count);
        hysteresis_ok &= st.flood == expected[i] && st.online;
    }
    CHECK(hysteresis_ok);

    /* During a flood new cars are refused with a notice, but cars inside can leave. */
    lot.water_mm = 25.0f;
    parking_state_update(&lot);
    CHECK(scan("FLOOD1", RFID_GATE_IN, clock_s()) == RFID_SCAN_FLOOD);
    rfid_notice_t n;
    rfid_session_get_notice(&n);
    CHECK(n.valid && n.result == RFID_SCAN_FLOOD && strcmp(n.card_id, "FLOOD1") == 0);
    CHECK(scan("OLD", RFID_GATE_OUT, clock_s()) == RFID_SCAN_EXITED);
    lot.water_mm = 0.0f;
    parking_state_update(&lot);
    CHECK(scan("FLOOD1", RFID_GATE_IN, clock_s()) == RFID_SCAN_ENTERED);
    CHECK(scan("FLOOD1", RFID_GATE_OUT, clock_s()) == RFID_SCAN_EXITED);

    /* A silent sensor whose last report was "all bays taken" does not block entry. */
    parking_state_t full = {.slots = {1, 1, 1, 1, 1}};
    parking_state_update(&full);
    fake_time_us += (int64_t)SEC(10);
    CHECK(scan("STALE1", RFID_GATE_IN, clock_s()) == RFID_SCAN_ENTERED);
    CHECK(scan("STALE1", RFID_GATE_OUT, clock_s()) == RFID_SCAN_EXITED);
    parking_state_update(&lot);
}

static void test_revenue_stats(void)
{
    /* 10:00 local time (UTC+7) on some day. */
    const uint64_t base = 19700ULL * 86400ULL - 7 * 3600 + 10 * 3600;
    rfid_day_stats_t t0, w0, t, w;
    rfid_session_set_clock(base);
    rfid_session_get_stats(&t0, &w0);

    CHECK(scan("REV1", RFID_GATE_IN, base) == RFID_SCAN_ENTERED);
    CHECK(scan("REV1", RFID_GATE_OUT, base + 7200) == RFID_SCAN_EXITED); /* 2 h = 10 000 VND */
    CHECK(scan("REV2", RFID_GATE_IN, base + 60) == RFID_SCAN_ENTERED);
    CHECK(scan("REV2", RFID_GATE_IN, base + 61) == RFID_SCAN_INVALID_GATE); /* rejections do not count */
    rfid_session_get_stats(&t, &w);
    CHECK(t.entries == t0.entries + 2 && t.exits == t0.exits + 1 && t.revenue_vnd == t0.revenue_vnd + 10000);
    CHECK(w.revenue_vnd == w0.revenue_vnd + 10000);

    /* Next day: today starts empty, the week still has yesterday. REV2 leaves after 1 day. */
    rfid_session_set_clock(base + 86400);
    CHECK(scan("REV2", RFID_GATE_OUT, base + 86400) == RFID_SCAN_EXITED); /* 24 h = 120 000 VND */
    rfid_session_get_stats(&t, &w);
    CHECK(t.entries == 0 && t.exits == 1 && t.revenue_vnd == 120000);
    CHECK(w.revenue_vnd == w0.revenue_vnd + 130000 && w.exits == w0.exits + 2);

    /* Stats are part of the saved state. */
    rfid_store_t saved;
    rfid_session_snapshot(&saved);
    uint32_t saved_revenue = 0;
    for (int i = 0; i < RFID_STATS_DAYS; ++i)
        saved_revenue += saved.days[i].revenue_vnd;
    CHECK(saved_revenue >= 130000);

    /* Seven days after the first day, it has left the weekly window. */
    rfid_session_set_clock(base + 7 * 86400);
    rfid_session_get_stats(&t, &w);
    CHECK(t.revenue_vnd == 0 && w.revenue_vnd == 120000);
    rfid_session_set_clock(base + 8 * 86400);
    rfid_session_get_stats(&t, &w);
    CHECK(w.revenue_vnd == 0 && w.entries == 0);
}

static void test_qr_payment(void)
{
    rfid_payment_t pay;
    rfid_notice_t n;
    rfid_receipt_t r;
    rfid_log_entry_t log[1];
    rfid_day_stats_t before, after;
    rfid_session_enable_payment(180);

    /* Exit scan starts a payment: barrier stays closed, the car is still inside. */
    CHECK(scan("PAY1", RFID_GATE_IN, clock_s()) == RFID_SCAN_ENTERED);
    const uint64_t t0 = clock_s();
    rfid_session_get_stats(&before, NULL);
    rfid_event_t e = card("PAY1", RFID_GATE_OUT);
    e.seq = 4242;
    CHECK(rfid_session_scan(&e, SEC(t0 + 3700)) == RFID_SCAN_AWAITING_PAYMENT);
    rfid_session_get_payment(&pay);
    CHECK(pay.active && !pay.expired && strcmp(pay.card_id, "PAY1") == 0);
    CHECK(pay.fee_vnd == 10000 && pay.duration_s == 3700);
    CHECK(strlen(pay.code) == RFID_PAY_CODE_LEN && strncmp(pay.code, "PK", 2) == 0);
    bool code_ok = true;
    for (const char *c = pay.code; *c; ++c)
        code_ok &= strchr("ABCDEFGHJKMNPQRSTUVWXYZ23456789", *c) != NULL || (c - pay.code) < 2;
    CHECK(code_ok);
    char code[RFID_PAY_CODE_LEN + 1];
    strcpy(code, pay.code);

    /* Scanning again shows the same QR (same code and fee) later on. */
    CHECK(scan("PAY1", RFID_GATE_OUT, t0 + 3900) == RFID_SCAN_AWAITING_PAYMENT);
    rfid_session_get_payment(&pay);
    CHECK(strcmp(pay.code, code) == 0 && pay.fee_vnd == 10000);

    /* Another car at the exit must wait. */
    CHECK(scan("PAY2", RFID_GATE_IN, t0) == RFID_SCAN_ENTERED);
    CHECK(scan("PAY2", RFID_GATE_OUT, t0 + 3901) == RFID_SCAN_PAY_BUSY);
    fake_time_us = (int64_t)(SEC(t0 + 3901) - (uint64_t)(parking_clock_now_us() - fake_time_us));
    rfid_session_get_notice(&n);
    CHECK(n.valid && n.result == RFID_SCAN_PAY_BUSY);

    /* A transfer with another code does nothing; the right one completes the exit. */
    CHECK(!rfid_session_payment_done("PKXXXXXX"));
    CHECK(rfid_session_payment_done(code));
    rfid_session_get_payment(&pay);
    CHECK(!pay.active);
    rfid_session_get_receipt(&r);
    CHECK(r.valid && strcmp(r.card_id, "PAY1") == 0 && r.fee_vnd == 10000);
    rfid_session_get_log(log, 1);
    CHECK(log[0].result == RFID_SCAN_EXITED && strcmp(log[0].card_id, "PAY1") == 0 && log[0].fee_vnd == 10000);
    rfid_session_get_stats(&after, NULL);
    CHECK(after.revenue_vnd == before.revenue_vnd + 10000 && after.exits == before.exits + 1);
    CHECK(!rfid_session_payment_done(code)); /* a second transfer does not exit twice */

    /* No transfer in 180 s: the TFT says pay at the booth, but a late transfer still counts. */
    const uint64_t t1 = clock_s();
    CHECK(scan("PAY2", RFID_GATE_OUT, t1) == RFID_SCAN_AWAITING_PAYMENT);
    rfid_session_get_payment(&pay);
    strcpy(code, pay.code);
    fake_time_us += (int64_t)SEC(181);
    rfid_session_get_payment(&pay);
    CHECK(pay.active && pay.expired);
    rfid_session_get_notice(&n);
    CHECK(n.valid && n.result == RFID_SCAN_PAY_TIMEOUT && strcmp(n.card_id, "PAY2") == 0);
    /* Once it has expired, another car may start paying; PAY2 then pays cash at the booth. */
    CHECK(scan("PAY3", RFID_GATE_IN, clock_s()) == RFID_SCAN_ENTERED);
    CHECK(rfid_session_pay_cash("PAY2"));
    CHECK(!rfid_session_pay_cash("PAY2") && !rfid_session_pay_cash("NOBODY"));
    rfid_session_get_log(log, 1);
    CHECK(strcmp(log[0].card_id, "PAY2") == 0 && log[0].result == RFID_SCAN_EXITED);

    /* Cash for a car that never scanned out charges the fee up to now. */
    fake_time_us += (int64_t)SEC(7200);
    CHECK(rfid_session_pay_cash("PAY3"));
    rfid_session_get_receipt(&r);
    CHECK(r.valid && strcmp(r.card_id, "PAY3") == 0 && r.fee_vnd == 10000);

    /* Payment off again: exits open at once, as before. */
    rfid_session_enable_payment(0);
    CHECK(scan("PAY4", RFID_GATE_IN, clock_s()) == RFID_SCAN_ENTERED);
    CHECK(scan("PAY4", RFID_GATE_OUT, clock_s()) == RFID_SCAN_EXITED);
}

int main(void)
{
    test_fee();
    test_entry_exit_and_receipt();
    test_capacity_and_active_list();
    test_event_log();
    test_rejection_notice();
    test_ir_cross_check();
    test_persistence_and_clock();
    test_offline_and_flood();
    test_retry_dedupe();
    test_revenue_stats();
    test_qr_payment();
    return TEST_REPORT("rfid_session");
}
