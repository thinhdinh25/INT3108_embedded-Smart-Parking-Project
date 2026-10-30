/* Runs the real SensorClient sketch against fake pins/clock/UDP: debounce, change-triggered
 * sends, the 1 s heartbeat, water scaling, and that the server accepts every payload. */
#include "Arduino.h"
#include "../SensorClient/SensorClient.ino"

extern "C" {
#include "packet_auth.h"
#include "parking_update.h"
}
#include "test.h"

extern "C" int64_t fake_time_us;
int64_t fake_time_us;

static const int SLOT_PINS[5] = {16, 4, 14, 13, 12}; /* S1..S5 */
static const int FLAME_PIN = 5;

/* Run loop() in 10 ms steps for `ms` milliseconds. */
static void run_for(unsigned long ms)
{
    for (unsigned long t = 0; t < ms; t += 10)
    {
        loop();
        delay(10);
    }
}

/* What the server does: check the tag with the shared key, then parse the JSON. */
static bool server_accepts(const std::string &packet, parking_state_t *out, uint32_t *seq = nullptr)
{
    memset(out, 0, sizeof(*out));
    const char *json = packet_auth_open(packet.c_str(), packet.size(), UDP_KEY, seq);
    return json && parking_update_parse_json(json, out);
}

static bool last_packet(parking_state_t *out)
{
    return !fake::udp_sent.empty() && server_accepts(fake::udp_sent.back(), out);
}

int main()
{
    for (int pin : SLOT_PINS)
        fake::pin_level[pin] = HIGH; /* IR is active-low: HIGH = free */
    fake::pin_level[FLAME_PIN] = LOW;  /* this flame module reads HIGH when it sees fire */
    fake::adc_value = 20;              /* dry */
    fake::now_ms = 1000;
    setup();

    /* First loop sends a full snapshot that the server parser accepts. */
    loop();
    parking_state_t s;
    CHECK(fake::udp_sent.size() == 1);
    CHECK(last_packet(&s));
    CHECK(s.slots[0] == 0 && s.slots[4] == 0 && !s.fire && s.water_mm == 0.0f);

    /* Heartbeat: with nothing changing, exactly one packet per second
     * (loops run at t = 1000..4000 ms, so sends happen at 2000, 3000 and 4000). */
    fake::udp_sent.clear();
    run_for(3010);
    CHECK(fake::udp_sent.size() == 3);

    /* A car on S1 is reported as soon as the 50 ms debounce passes, not at the next heartbeat. */
    fake::udp_sent.clear();
    loop();
    delay(10);
    fake::pin_level[SLOT_PINS[0]] = LOW;
    run_for(40);
    CHECK(fake::udp_sent.empty()); /* still inside the debounce window */
    run_for(30);
    CHECK(fake::udp_sent.size() == 1);
    CHECK(last_packet(&s) && s.slots[0] == 1 && s.slots[1] == 0);

    /* A 30 ms glitch on S5 is filtered out. */
    fake::udp_sent.clear();
    fake::pin_level[SLOT_PINS[4]] = LOW;
    run_for(30);
    fake::pin_level[SLOT_PINS[4]] = HIGH;
    run_for(500);
    for (const std::string &p : fake::udp_sent)
    {
        CHECK(server_accepts(p, &s));
        CHECK(s.slots[4] == 0);
    }

    /* Slot order: S5 maps to GPIO12. */
    fake::pin_level[SLOT_PINS[4]] = LOW;
    run_for(100);
    CHECK(last_packet(&s) && s.slots[4] == 1 && s.slots[0] == 1);

    /* Fire is sent immediately after debounce, then cleared again. */
    fake::udp_sent.clear();
    fake::pin_level[FLAME_PIN] = HIGH;
    run_for(70);
    CHECK(last_packet(&s) && s.fire);
    fake::pin_level[FLAME_PIN] = LOW;
    run_for(70);
    CHECK(last_packet(&s) && !s.fire);

    /* Water level: linear from WATER_DRY_ADC (0 mm) to WATER_MAX_ADC (40 mm), clamped. */
    fake::adc_value = 700;
    run_for(1300);
    CHECK(last_packet(&s) && s.water_mm == 40.0f);
    fake::adc_value = 360;
    run_for(1300);
    CHECK(last_packet(&s) && s.water_mm == 20.0f);
    fake::adc_value = 1023;
    run_for(1300);
    CHECK(last_packet(&s) && s.water_mm == 40.0f);

    /* Without Wi-Fi nothing is sent; sending resumes when it reconnects. */
    fake::udp_sent.clear();
    WiFi.connected = false;
    run_for(2000);
    CHECK(fake::udp_sent.empty());
    WiFi.connected = true;
    run_for(1100);
    CHECK(!fake::udp_sent.empty());

    /* Every packet is signed with a sequence number the server has not seen before. */
    fake::udp_sent.clear();
    run_for(3010);
    uint32_t prev = 0, seq = 0;
    bool increasing = fake::udp_sent.size() == 3;
    for (const std::string &p : fake::udp_sent)
    {
        increasing &= server_accepts(p, &s, &seq) && seq > prev;
        prev = seq;
    }
    CHECK(increasing);
    CHECK(fake::udp_sent.back().compare(0, 1, "{") != 0); /* never plain JSON */

    /* After a reboot the numbers still go up (reserved in flash), so the server keeps
     * accepting them while a recorded old packet stays stale. */
    const int commits = fake::eeprom_commits;
    setup();
    fake::udp_sent.clear();
    run_for(1010);
    CHECK(!fake::udp_sent.empty() && server_accepts(fake::udp_sent.back(), &s, &seq) && seq > prev);
    CHECK(fake::eeprom_commits == commits + 1); /* one flash write per boot, not per packet */

    return TEST_REPORT("sensor_client");
}
