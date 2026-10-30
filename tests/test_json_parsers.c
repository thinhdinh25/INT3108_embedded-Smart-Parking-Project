/* Server UDP message parsers: valid input is accepted, anything off-schema is rejected. */
#include <string.h>

#include "parking_update.h"
#include "rfid_session.h"
#include "test.h"

int64_t fake_time_us;

static bool parking(const char *json, parking_state_t *out)
{
    memset(out, 0, sizeof(*out));
    return parking_update_parse_json(json, out);
}

static bool rfid(const char *json, rfid_event_t *out)
{
    memset(out, 0, sizeof(*out));
    return rfid_event_parse_json(json, out);
}

static void test_parking_update_valid(void)
{
    parking_state_t s;
    CHECK(parking("{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":0.2,\"fire\":false}", &s));
    CHECK(s.slots[0] == 1 && s.slots[1] == 0 && s.slots[3] == 1 && s.slots[4] == 0);
    CHECK(s.water_mm > 0.19f && s.water_mm < 0.21f);
    CHECK(!s.fire);

    /* Any key order, whitespace everywhere, fire=true. */
    CHECK(parking(" {\n \"fire\" : true ,\t\"slots\" : [ 0 , 0 , 0 , 0 , 1 ] , \"water_mm\" : 12.5 , "
                  "\"type\" : \"parking_update\" } \r\n", &s));
    CHECK(s.fire && s.slots[4] == 1 && s.water_mm == 12.5f);

    /* "fire" is optional for older clients. */
    CHECK(parking("{\"type\":\"parking_update\",\"slots\":[0,0,0,0,0],\"water_mm\":3}", &s));
    CHECK(!s.fire);

    /* Range limits are inclusive. */
    CHECK(parking("{\"type\":\"parking_update\",\"slots\":[0,0,0,0,0],\"water_mm\":0}", &s));
    CHECK(parking("{\"type\":\"parking_update\",\"slots\":[0,0,0,0,0],\"water_mm\":10000}", &s));
}

static void test_parking_update_rejects(void)
{
    parking_state_t s;
    const char *bad[] = {
        "",
        "{}",
        "[]",
        "not json",
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1],\"water_mm\":0.2}",          /* 4 slots */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0,1],\"water_mm\":0.2}",      /* 6 slots */
        "{\"type\":\"parking_update\",\"slots\":[2,0,0,1,0],\"water_mm\":0.2}",        /* slot 2 */
        "{\"type\":\"parking_update\",\"slots\":[\"1\",0,0,1,0],\"water_mm\":0.2}",    /* string slot */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":-1}",         /* negative */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":10001}",      /* too high */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":\"1\"}",      /* string */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1,\"fire\":1}", /* not bool */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1,\"temperature_c\":28}",
        "{\"type\":\"parking_update\",\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1}",
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1,\"fire\":true,\"fire\":true}",
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1}xyz",       /* trailing data */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1,}",         /* trailing comma */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0]}",                         /* no water */
        "{\"slots\":[1,0,0,1,0],\"water_mm\":1}",                                       /* no type */
        "{\"type\":\"rfid_event\",\"slots\":[1,0,0,1,0],\"water_mm\":1}",               /* wrong type */
        "{\"type\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1",           /* unterminated */
        "{\"ty\\\"pe\":\"parking_update\",\"slots\":[1,0,0,1,0],\"water_mm\":1}",       /* escaped key */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        bool accepted = parking(bad[i], &s);
        CHECK(!accepted);
        if (accepted)
            printf("    accepted: %s\n", bad[i]);
    }
    CHECK(!parking_update_parse_json(NULL, &s));
    CHECK(!parking_update_parse_json("{}", NULL));
}

static void test_rfid_event(void)
{
    rfid_event_t e;
    CHECK(rfid("{\"type\":\"rfid_event\",\"card_id\":\"a1b2c3d4\",\"gate\":\"in\",\"temperature_c\":28.5}", &e));
    CHECK(strcmp(e.card_id, "A1B2C3D4") == 0); /* normalized to upper case */
    CHECK(e.gate == RFID_GATE_IN && e.temperature_c == 28.5f);

    CHECK(rfid(" { \"temperature_c\" : -40 , \"gate\" : \"OUT\" , \"card_id\" : \"X-1_2\" , "
               "\"type\" : \"rfid_event\" } ", &e));
    CHECK(e.gate == RFID_GATE_OUT && strcmp(e.card_id, "X-1_2") == 0 && e.temperature_c == -40.0f);

    /* 31 characters fit, 32 do not. */
    CHECK(rfid("{\"type\":\"rfid_event\",\"card_id\":\"0123456789ABCDEF0123456789ABCDE\",\"gate\":\"in\",\"temperature_c\":1}", &e));
    CHECK(strlen(e.card_id) == RFID_CARD_ID_MAX);

    const char *bad[] = {
        "{\"type\":\"rfid_event\",\"card_id\":\"0123456789ABCDEF0123456789ABCDEF\",\"gate\":\"in\",\"temperature_c\":1}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1 B2\",\"gate\":\"in\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\\\"B2\",\"gate\":\"in\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"card_id\":\"<b>\",\"gate\":\"in\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"card_id\":\"\",\"gate\":\"in\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"gate\":\"side\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"gate\":\"in\",\"temperature_c\":200}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"gate\":\"in\",\"temperature_c\":-41}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"gate\":\"in\",\"temperature_c\":nan}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"gate\":\"in\"}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"gate\":\"in\",\"temperature_c\":28}",
        "{\"type\":\"parking_update\",\"card_id\":\"A1\",\"gate\":\"in\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"card_id\":\"B2\",\"gate\":\"in\",\"temperature_c\":28}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"gate\":\"in\",\"temperature_c\":28,\"extra\":1}",
        "{\"type\":\"rfid_event\",\"card_id\":\"A1\",\"gate\":\"in\",\"temperature_c\":28} x",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        bool accepted = rfid(bad[i], &e);
        CHECK(!accepted);
        if (accepted)
            printf("    accepted: %s\n", bad[i]);
    }
}

int main(void)
{
    test_parking_update_valid();
    test_parking_update_rejects();
    test_rfid_event();
    return TEST_REPORT("json_parsers");
}
