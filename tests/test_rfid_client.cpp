/* Runs the real RFIDClient sketch against fake RC522 readers, DHT11, clock and UDP:
 * UID formatting, entry/exit gate tagging, the 2 s re-read cooldown, and that the server
 * accepts every payload it sends. */
#include "Arduino.h"
#include "../RFIDClient/RFIDClient.ino"

extern "C" {
#include "rfid_session.h"
}
#include "test.h"

extern "C" int64_t fake_time_us;
int64_t fake_time_us;

static void present(int reader, std::initializer_list<byte> uid)
{
    mfrc522[reader].card_present = true;
    mfrc522[reader].card_uid_size = (byte)uid.size();
    std::copy(uid.begin(), uid.end(), mfrc522[reader].card_uid);
}

static void remove_cards()
{
    mfrc522[0].card_present = false;
    mfrc522[1].card_present = false;
}

static bool last_event(rfid_event_t *out)
{
    if (fake::udp_sent.empty())
        return false;
    memset(out, 0, sizeof(*out));
    return rfid_event_parse_json(fake::udp_sent.back().c_str(), out);
}

int main()
{
    fake::now_ms = 10000;
    setup();
    loop(); /* first loop reads the DHT11 (28.0 C) */
    CHECK(fake::udp_sent.empty());

    /* Card on the entry reader: upper-case hex UID, gate "in", current temperature. */
    rfid_event_t e;
    present(0, {0xA1, 0x0B, 0x03, 0xD4});
    loop();
    CHECK(fake::udp_sent.size() == 1);
    CHECK(fake::udp_sent.back().find("\"card_id\":\"A10B03D4\"") != std::string::npos);
    CHECK(last_event(&e));
    CHECK(strcmp(e.card_id, "A10B03D4") == 0 && e.gate == RFID_GATE_IN && e.temperature_c == 28.0f);

    /* Holding the card on the reader does not resend it within the 2 s cooldown... */
    for (int i = 0; i < 100; ++i)
        loop(); /* each loop takes ~8 ms of fake time */
    CHECK(fake::udp_sent.size() == 1);
    /* ...and the same card on the exit reader is also blocked inside the cooldown. */
    remove_cards();
    present(1, {0xA1, 0x0B, 0x03, 0xD4});
    loop();
    CHECK(fake::udp_sent.size() == 1);

    /* After the cooldown, the exit reader reports gate "out". */
    delay(2000);
    loop();
    CHECK(fake::udp_sent.size() == 2);
    CHECK(last_event(&e) && e.gate == RFID_GATE_OUT && strcmp(e.card_id, "A10B03D4") == 0);

    /* A different card is reported immediately. */
    remove_cards();
    present(0, {0x01, 0x02, 0x03, 0x04});
    loop();
    CHECK(fake::udp_sent.size() == 3);
    CHECK(last_event(&e) && strcmp(e.card_id, "01020304") == 0 && e.gate == RFID_GATE_IN);

    /* Only the reader whose antenna is on can see a card: one scan, one packet. */
    remove_cards();
    delay(2500);
    fake::udp_sent.clear();
    present(0, {0x11, 0x22, 0x33, 0x44});
    loop();
    CHECK(fake::udp_sent.size() == 1);

    /* Longest MIFARE UID (10 bytes) becomes 20 hex characters that the server accepts. */
    remove_cards();
    present(0, {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x77, 0x66});
    loop();
    CHECK(last_event(&e) && strcmp(e.card_id, "FFEEDDCCBBAA99887766") == 0);

    /* A failed DHT11 read keeps the last good temperature. */
    remove_cards();
    fake::dht_temperature = NAN;
    delay(5000);
    loop();
    delay(2500);
    present(0, {0x55, 0x66, 0x77, 0x88});
    loop();
    CHECK(last_event(&e) && e.temperature_c == 28.0f);

    /* A new reading is used once the 5 s interval passes. */
    remove_cards();
    fake::dht_temperature = 31.0f;
    delay(5000);
    loop();
    delay(2500);
    present(1, {0x55, 0x66, 0x77, 0x88});
    loop();
    CHECK(last_event(&e) && e.temperature_c == 31.0f && e.gate == RFID_GATE_OUT);

    return TEST_REPORT("rfid_client");
}
