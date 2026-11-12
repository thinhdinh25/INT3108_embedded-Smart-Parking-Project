#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_netif.h"

/* Joins the shared Wi-Fi (station only) and reconnects whenever it drops. */
esp_err_t wifi_start(void);
esp_netif_t *wifi_sta_netif(void);

#endif
