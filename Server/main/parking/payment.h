#ifndef PAYMENT_H
#define PAYMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* VietQR (NAPAS, EMVCo merchant-presented) payload for a bank transfer of `amount_vnd` to
 * `account` at the bank with 6-digit `bin`, with `note` as the transfer content.
 * Returns the payload length, or 0 if an argument is invalid or `out` is too small. */
size_t vietqr_payload(char *out, size_t out_size, const char *bin, const char *account,
                      uint32_t amount_vnd, const char *note);

/* CRC16-CCITT (poly 0x1021, init 0xFFFF), the checksum VietQR uses in field 63. */
uint16_t vietqr_crc16(const char *data, size_t len);

/* SePay /userapi/transactions/list response: true if a transaction whose content contains
 * `code` (any letter case) brought in at least `amount_vnd`. */
bool sepay_has_payment(const char *body, const char *code, uint32_t amount_vnd);

/* Largest transaction "id" in the response, so later polls only look at newer ones. */
bool sepay_latest_id(const char *body, uint32_t *id);

#endif
