#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

/* Joins the shared Wi-Fi (station only) and reconnects whenever it drops. */
esp_err_t wifi_start(void);

#endif
