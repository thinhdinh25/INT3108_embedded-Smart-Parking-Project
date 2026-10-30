#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include "esp_err.h"

/**
 * Initialize the Wi-Fi station (client) and start the HTTP and UDP services.
 *
 * ESP32 connects to the RFID Client hotspot and answers mDNS as parking.local:
 *   SSID: ESP32-Parking
 *   Password: 12345678
 * UDP requests are received on port 3333.
 */
esp_err_t network_service_start(void);

#endif // NETWORK_SERVICE_H
