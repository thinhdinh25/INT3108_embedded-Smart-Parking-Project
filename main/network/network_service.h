#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include "esp_err.h"

/**
 * Initialize the Wi-Fi SoftAP and start the HTTP and UDP services.
 *
 * ESP32 creates:
 *   SSID: ESP32-Parking
 *   Password: 12345678
 *   IP: 192.168.4.1
 * UDP requests are received on port 3333.
 */
esp_err_t network_service_start(void);

#endif // NETWORK_SERVICE_H
