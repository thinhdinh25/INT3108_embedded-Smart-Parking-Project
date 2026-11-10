#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "payment.h"

uint16_t vietqr_crc16(const char *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= (uint16_t)((uint8_t)data[i] << 8);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Appends one EMVCo field "<id><2-digit length><value>"; false if it does not fit. */
static bool put_field(char *out, size_t out_size, size_t *len, const char *id, const char *value)
{
    size_t value_len = strlen(value);
    if (value_len > 99)
        return false;
    int n = snprintf(out + *len, out_size - *len, "%s%02u%s", id, (unsigned)value_len, value);
    if (n < 0 || (size_t)n >= out_size - *len)
        return false;
    *len += (size_t)n;
    return true;
}

static bool all_chars(const char *s, size_t min, size_t max, int (*ok)(int))
{
    size_t n = strlen(s);
    if (n < min || n > max)
        return false;
    for (; *s; ++s)
        if (!ok((unsigned char)*s))
            return false;
    return true;
}

size_t vietqr_payload(char *out, size_t out_size, const char *bin, const char *account,
                      uint32_t amount_vnd, const char *note)
{
    if (!out || !bin || !account || !note || amount_vnd == 0 || !all_chars(bin, 6, 6, isdigit) ||
        !all_chars(account, 1, 19, isalnum) || !all_chars(note, 1, 25, isalnum))
        return 0;

    char bank[48], account_info[80], amount[12], extra[32];
    size_t n = 0;
    if (!put_field(bank, sizeof(bank), &n, "00", bin) ||
        !put_field(bank, sizeof(bank), &n, "01", account))
        return 0;
    n = 0;
    if (!put_field(account_info, sizeof(account_info), &n, "00", "A000000727") || /* NAPAS */
        !put_field(account_info, sizeof(account_info), &n, "01", bank) ||
        !put_field(account_info, sizeof(account_info), &n, "02", "QRIBFTTA"))      /* to account */
        return 0;
    n = 0;
    if (!put_field(extra, sizeof(extra), &n, "08", note))
        return 0;
    snprintf(amount, sizeof(amount), "%lu", (unsigned long)amount_vnd);

    size_t len = 0;
    if (!put_field(out, out_size, &len, "00", "01") ||          /* payload format */
        !put_field(out, out_size, &len, "01", "12") ||          /* dynamic: one payment */
        !put_field(out, out_size, &len, "38", account_info) ||
        !put_field(out, out_size, &len, "53", "704") ||         /* VND */
        !put_field(out, out_size, &len, "54", amount) ||
        !put_field(out, out_size, &len, "58", "VN") ||
        !put_field(out, out_size, &len, "62", extra) ||
        out_size - len < 9)                                     /* "6304" + 4 hex + NUL */
        return 0;
    memcpy(out + len, "6304", 4);
    len += 4;
    snprintf(out + len, out_size - len, "%04X", vietqr_crc16(out, len));
    return len + 4;
}

static const char *find_nocase(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; ++haystack)
    {
        size_t i = 0;
        while (i < n && haystack[i] && tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == n)
            return haystack;
    }
    return NULL;
}

bool sepay_has_payment(const char *body, const char *code, uint32_t amount_vnd)
{
    if (!body || !code || !*code)
        return false;
    for (const char *hit = find_nocase(body, code); hit; hit = find_nocase(hit + 1, code))
    {
        /* The transaction object around the hit: from the last '{' before it to the next '}'. */
        const char *start = hit;
        while (start > body && *start != '{')
            --start;
        const char *end = strchr(hit, '}');
        const char *field = strstr(start, "\"amount_in\":\"");
        if (*start != '{' || !end || !field || field > end)
            continue;
        double amount = strtod(field + strlen("\"amount_in\":\""), NULL);
        if (amount > (double)amount_vnd - 0.5 && amount < (double)amount_vnd + 0.5)
            return true;
    }
    return false;
}

/* True if [start, end) contains `needle`. */
static bool span_has(const char *start, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = start; p + n <= end; ++p)
        if (memcmp(p, needle, n) == 0)
            return true;
    return false;
}

bool sepay_has_transfer_to(const char *body, const char *account, uint32_t amount_vnd)
{
    if (!body || !account || !*account || amount_vnd == 0 || strlen(account) > 32)
        return false;
    char sub_field[64], main_field[64];
    snprintf(sub_field, sizeof(sub_field), "\"sub_account\":\"%s\"", account);
    snprintf(main_field, sizeof(main_field), "\"account_number\":\"%s\"", account);
    const char *key = "\"amount_in\":\"";
    for (const char *field = strstr(body, key); field; field = strstr(field + 1, key))
    {
        /* Transaction objects are flat, so the enclosing braces bound this one. */
        const char *start = field;
        while (start > body && *start != '{')
            --start;
        const char *end = strchr(field, '}');
        if (*start != '{' || !end)
            continue;
        double amount = strtod(field + strlen(key), NULL);
        if ((span_has(start, end, sub_field) || span_has(start, end, main_field)) &&
            amount > (double)amount_vnd - 0.5 && amount < (double)amount_vnd + 0.5)
            return true;
    }
    return false;
}

bool sepay_latest_id(const char *body, uint32_t *id)
{
    bool found = false;
    uint32_t best = 0;
    for (const char *p = body ? strstr(body, "\"id\":\"") : NULL; p; p = strstr(p + 1, "\"id\":\""))
    {
        unsigned long v = strtoul(p + strlen("\"id\":\""), NULL, 10);
        if (v > best)
            best = (uint32_t)v;
        found = true;
    }
    if (found && id)
        *id = best;
    return found;
}
