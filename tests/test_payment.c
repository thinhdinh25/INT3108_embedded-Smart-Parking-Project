/* VietQR payload and SePay response matching. */
#include <stdio.h>
#include <string.h>

#include "payment.h"
#include "test.h"

/* Reads one EMVCo field "<id><len><value>" at *p into value; advances *p. */
static bool next_field(const char **p, char id[3], char *value, size_t size)
{
    if (strlen(*p) < 4)
        return false;
    memcpy(id, *p, 2);
    id[2] = '\0';
    size_t len = (size_t)(((*p)[2] - '0') * 10 + ((*p)[3] - '0'));
    if (len >= size || strlen(*p + 4) < len)
        return false;
    memcpy(value, *p + 4, len);
    value[len] = '\0';
    *p += 4 + len;
    return true;
}

static void test_crc(void)
{
    CHECK(vietqr_crc16("123456789", 9) == 0x29B1); /* standard CRC-16/CCITT-FALSE check value */
    CHECK(vietqr_crc16("", 0) == 0xFFFF);
}

static void test_payload(void)
{
    char qr[256];
    size_t len = vietqr_payload(qr, sizeof(qr), "970418", "1234567890", 10000, "PK3F9A2B");
    CHECK(len == strlen(qr) && len > 0);

    /* Field by field, as a banking app reads it. */
    const char *p = qr;
    char id[3], v[128];
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "00") == 0 && strcmp(v, "01") == 0);
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "01") == 0 && strcmp(v, "12") == 0);
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "38") == 0);
    CHECK(strcmp(v, "0010A000000727" "0124" "0006970418" "01101234567890" "0208QRIBFTTA") == 0);
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "53") == 0 && strcmp(v, "704") == 0);
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "54") == 0 && strcmp(v, "10000") == 0);
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "58") == 0 && strcmp(v, "VN") == 0);
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "62") == 0 && strcmp(v, "0808PK3F9A2B") == 0);
    CHECK(next_field(&p, id, v, sizeof(v)) && strcmp(id, "63") == 0 && strlen(v) == 4 && *p == '\0');

    /* The CRC covers everything up to and including "6304". */
    char crc[5];
    snprintf(crc, sizeof(crc), "%04X", vietqr_crc16(qr, len - 4));
    CHECK(strcmp(qr + len - 4, crc) == 0);

    /* Published VietQR sample (Viblo), reproduced byte for byte including its CRC. */
    vietqr_payload(qr, sizeof(qr), "970423", "mynamebvh", 50000, "test");
    CHECK(strcmp(qr, "00020101021238530010A000000727012300069704230109mynamebvh0208QRIBFTTA"
                     "53037045405500005802VN62080804test6304AB76") == 0);

    /* Invalid input is refused instead of producing a QR that pays the wrong place. */
    CHECK(vietqr_payload(qr, sizeof(qr), "97041", "1234567890", 10000, "PK1") == 0);   /* BIN */
    CHECK(vietqr_payload(qr, sizeof(qr), "970418", "", 10000, "PK1") == 0);
    CHECK(vietqr_payload(qr, sizeof(qr), "970418", "12 34", 10000, "PK1") == 0);
    CHECK(vietqr_payload(qr, sizeof(qr), "970418", "1234567890", 0, "PK1") == 0);
    CHECK(vietqr_payload(qr, sizeof(qr), "970418", "1234567890", 10000, "PK 1") == 0); /* note */
    CHECK(vietqr_payload(qr, 40, "970418", "1234567890", 10000, "PK1") == 0);         /* too small */
}

static const char *SEPAY_BODY =
    "{\"status\":200,\"error\":null,\"messages\":{\"success\":true},\"transactions\":["
    "{\"id\":\"49690\",\"bank_brand_name\":\"BIDV\",\"account_number\":\"1234567890\","
    "\"transaction_date\":\"2026-11-03 10:01:02\",\"amount_out\":\"0.00\",\"amount_in\":\"10000.00\","
    "\"accumulated\":\"500000.00\",\"transaction_content\":\"NGUYEN VAN A chuyen tien pk3f9a2b FT2611\","
    "\"reference_number\":\"FT2611\",\"code\":null,\"sub_account\":null,\"bank_account_id\":\"19\"},"
    "{\"id\":\"49682\",\"bank_brand_name\":\"BIDV\",\"account_number\":\"1234567890\","
    "\"transaction_date\":\"2026-11-03 09:00:00\",\"amount_out\":\"0.00\",\"amount_in\":\"5000.00\","
    "\"accumulated\":\"490000.00\",\"transaction_content\":\"PK77AA01\","
    "\"reference_number\":\"FT2600\",\"code\":null,\"sub_account\":null,\"bank_account_id\":\"19\"}]}";

static void test_sepay(void)
{
    CHECK(sepay_has_payment(SEPAY_BODY, "PK3F9A2B", 10000));   /* bank app lower-cased the code */
    CHECK(sepay_has_payment(SEPAY_BODY, "PK3F9A2B", 9000));    /* paid more than asked */
    CHECK(!sepay_has_payment(SEPAY_BODY, "PK3F9A2B", 15000));  /* paid too little */
    CHECK(!sepay_has_payment(SEPAY_BODY, "PK77AA01", 10000));  /* that one only paid 5 000 */
    CHECK(sepay_has_payment(SEPAY_BODY, "PK77AA01", 5000));
    CHECK(!sepay_has_payment(SEPAY_BODY, "PK000000", 1));      /* unknown code */
    CHECK(!sepay_has_payment("{\"transactions\":[]}", "PK3F9A2B", 1));
    CHECK(!sepay_has_payment(NULL, "PK3F9A2B", 1) && !sepay_has_payment(SEPAY_BODY, "", 1));

    /* MoMo replaces the note: matched by the receiving VA and the exact fee instead. */
    const char *momo =
        "{\"status\":200,\"messages\":{\"success\":true},\"transactions\":["
        "{\"id\":\"95776524\",\"account_number\":\"8813315396\",\"sub_account\":\"96247V4VYM\","
        "\"amount_in\":\"10000.00\",\"transaction_content\":\"154908206647 0918400370 chuyen tien qua MoMo\"},"
        "{\"id\":\"95776500\",\"account_number\":\"8813315396\",\"sub_account\":null,"
        "\"amount_in\":\"5000.00\",\"transaction_content\":\"other\"}]}";
    CHECK(!sepay_has_payment(momo, "PK3F9A2B", 10000));
    CHECK(sepay_has_transfer_to(momo, "96247V4VYM", 10000));
    CHECK(!sepay_has_transfer_to(momo, "96247V4VYM", 5000));   /* that 5 000 went to the main account */
    CHECK(sepay_has_transfer_to(momo, "8813315396", 5000));
    CHECK(!sepay_has_transfer_to(momo, "96247V4VYM", 15000));  /* not the exact fee */
    CHECK(!sepay_has_transfer_to(momo, "96247V4VYM", 9000));
    CHECK(!sepay_has_transfer_to(momo, "OTHERVA", 10000));
    CHECK(!sepay_has_transfer_to(momo, "", 10000) && !sepay_has_transfer_to(NULL, "96247V4VYM", 10000));

    uint32_t id = 0;
    CHECK(sepay_latest_id(SEPAY_BODY, &id) && id == 49690);
    CHECK(!sepay_latest_id("{\"transactions\":[]}", &id));
}

int main(void)
{
    test_crc();
    test_payload();
    test_sepay();
    return TEST_REPORT("payment");
}
