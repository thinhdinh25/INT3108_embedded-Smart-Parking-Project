#ifndef SEPAY_CLIENT_H
#define SEPAY_CLIENT_H

#include "esp_err.h"

/* Turns QR payment on when a router, account and SePay token are configured, and polls
 * SePay for the matching transfer while a payment is pending. */
esp_err_t sepay_client_start(void);

#endif
