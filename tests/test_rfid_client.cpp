/* Runs the real RFIDClient sketch against fake RC522 readers, DHT11, clock and UDP:
 * UID formatting, entry/exit gate tagging, the 2 s re-read cooldown, retries until the
 * server ACKs, periodic temperature, and that the server accepts every signed packet. */
#include "Arduino.h"
#include "../RFIDClient/RFIDClient.ino"

#include <set>

extern "C" {
#include "packet_auth.h"
#include "parking_update.h"
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

/* Card scans the server would accept, once per sequence number (retries are copies). */
static std::vector<std::pair<uint32_t, rfid_event_t>> scans()
{
    std::vector<std::pair<uint32_t, rfid_event_t>> out;
    std::set<uint32_t> seen;
    for (const std::string &p : fake::udp_sent)
    {
        uint32_t seq;
        rfid_event_t e;
        memset(&e, 0, sizeof(e));
        const char *json = packet_auth_open(p.c_str(), p.size(), UDP_KEY, &seq);
        if (json && rfid_event_parse_json(json, &e) && seen.insert(seq).second)
            out.push_back({seq, e});
    }
    return out;
}

static bool last_event(rfid_event_t *out)
{
    auto all = scans();
    if (all.empty())
        return false;
    *out = all.back().second;
    return true;
}

static int copies_of(uint32_t seq)
{
    int n = 0;
    for (const std::string &p : fake::udp_sent)
    {
        uint32_t s;
        n += packet_auth_open(p.c_str(), p.size(), UDP_KEY, &s) && s == seq;
    }
    return n;
}

/* The server's reply, signed with the same key. */
static void server_ack(uint32_t seq)
{
    char ack[32];
    size_t len = packet_auth_seal(UDP_KEY, seq, "ACK", ack, sizeof(ack));
    fake::udp_inbox.push_back(std::string(ack, len));
}

static void ack_all()
{
    for (auto &scan : scans())
        server_ack(scan.first);
    loop();
}

int main()
{
    fake::now_ms = 10000;
    setup();
    loop(); /* first loop reads the DHT11 (28.0 C) and sends it on its own */
    CHECK(scans().empty());
    float temperature = 0;
    const std::string &climate = fake::udp_sent.back();
    const char *json = packet_auth_open(climate.c_str(), climate.size(), UDP_KEY, nullptr);
    CHECK(json && climate_parse_json(json, &temperature) && temperature == 28.0f);

    /* Card on the entry reader: upper-case hex UID, gate "in", current temperature. */
    rfid_event_t e;
    present(0, {0xA1, 0x0B, 0x03, 0xD4});
    loop();
    CHECK(scans().size() == 1);
    CHECK(fake::udp_sent.back().find("\"card_id\":\"A10B03D4\"") != std::string::npos);
    CHECK(last_event(&e));
    ack_all();
    CHECK(strcmp(e.card_id, "A10B03D4") == 0 && e.gate == RFID_GATE_IN && e.temperature_c == 28.0f);

    /* Holding the card on the reader does not resend it within the 2 s cooldown... */
    for (int i = 0; i < 100; ++i)
        loop(); /* each loop takes ~8 ms of fake time */
    CHECK(scans().size() == 1);
    /* ...and the same card on the exit reader is also blocked inside the cooldown. */
    remove_cards();
    present(1, {0xA1, 0x0B, 0x03, 0xD4});
    loop();
    CHECK(scans().size() == 1);

    /* After the cooldown, the exit reader reports gate "out". */
    delay(2000);
    loop();
    CHECK(scans().size() == 2);
    CHECK(last_event(&e) && e.gate == RFID_GATE_OUT && strcmp(e.card_id, "A10B03D4") == 0);

    /* A different card is reported immediately. */
    remove_cards();
    present(0, {0x01, 0x02, 0x03, 0x04});
    loop();
    CHECK(scans().size() == 3);
    CHECK(last_event(&e) && strcmp(e.card_id, "01020304") == 0 && e.gate == RFID_GATE_IN);

    /* Only the reader whose antenna is on can see a card: one scan, one packet. */
    ack_all(); /* earlier scans would otherwise be retried into this check */
    remove_cards();
    delay(2500);
    fake::udp_sent.clear();
    present(0, {0x11, 0x22, 0x33, 0x44});
    loop();
    CHECK(scans().size() == 1);

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

    /* An unanswered scan is resent every 300 ms with the same sequence number... */
    ack_all();
    remove_cards();
    delay(2500);
    fake::udp_sent.clear();
    present(0, {0x99, 0x98, 0x97, 0x96});
    loop();
    remove_cards();
    const uint32_t seq = scans().back().first;
    CHECK(copies_of(seq) == 1);
    delay(300);
    loop();
    CHECK(copies_of(seq) == 2);
    /* ...an ACK signed with another key is ignored... */
    uint8_t wrong_key[16] = {0};
    char forged[32];
    size_t forged_len = packet_auth_seal(wrong_key, seq, "ACK", forged, sizeof(forged));
    fake::udp_inbox.push_back(std::string(forged, forged_len));
    delay(300);
    loop();
    CHECK(copies_of(seq) == 3);
    /* ...and the server's real ACK stops the retries. */
    server_ack(seq);
    for (int i = 0; i < 10; ++i)
    {
        loop();
        delay(300);
    }
    CHECK(copies_of(seq) == 3);

    /* If the server never answers, the client gives up after 6 sends. */
    delay(2500);
    present(1, {0x99, 0x98, 0x97, 0x96});
    loop();
    remove_cards();
    const uint32_t lost = scans().back().first;
    for (int i = 0; i < 20; ++i)
    {
        delay(300);
        loop();
    }
    CHECK(copies_of(lost) == 6);

    return TEST_REPORT("rfid_client");
}
