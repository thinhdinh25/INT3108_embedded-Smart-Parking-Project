/* Runs the real SensorClient sketch against fake pins/clock/UDP: debounce, change-triggered
 * sends, the 1 s heartbeat, water scaling, and that the server accepts every payload. */
#include "Arduino.h"
#include "../SensorClient/SensorClient.ino"

extern "C" {
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

static bool last_packet(parking_state_t *out)
{
    if (fake::udp_sent.empty())
        return false;
    memset(out, 0, sizeof(*out));
    return parking_update_parse_json(fake::udp_sent.back().c_str(), out);
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
        CHECK(parking_update_parse_json(p.c_str(), &s));
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

    return TEST_REPORT("sensor_client");
}
