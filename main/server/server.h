#ifndef SERVER_H
#define SERVER_H

#include "esp_err.h"

/**
 * Initialize Wi-Fi SoftAP and start HTTP server.
 *
 * ESP32 creates:
 *   SSID: ESP32-Parking
 *   Password: 12345678
 *   IP: 192.168.4.1
 */
esp_err_t server_start(void);

#endif // SERVER_H