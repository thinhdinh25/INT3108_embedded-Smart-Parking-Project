/*
 * ESP-IDF example: call send_parking_update() after Wi-Fi connects to
 * ESP32-Parking. Add lwip to the component's REQUIRES list.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define PARKING_SERVER_BROADCAST "192.168.4.255"
#define PARKING_SERVER_PORT 3333

static const char *TAG = "PARKING_UDP_CLIENT";

/* slots[i] is 1 when occupied and 0 when free. */
int send_parking_update(const int slots[5], float temperature_c, float water_mm)
{
    char payload[128];
    int temperature_tenths = (int)(temperature_c * 10.0f + (temperature_c >= 0.0f ? 0.5f : -0.5f));
    int water_tenths = (int)(water_mm * 10.0f + 0.5f);
    int temperature_magnitude = abs(temperature_tenths);
    int payload_len = snprintf(payload, sizeof(payload),
                               "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],\"temperature_c\":%s%d.%d,\"water_mm\":%d.%d}",
                               slots[0], slots[1], slots[2], slots[3], slots[4],
                               temperature_tenths < 0 ? "-" : "",
                               temperature_magnitude / 10, temperature_magnitude % 10,
                               water_tenths / 10, water_tenths % 10);
    if (payload_len < 0 || payload_len >= sizeof(payload))
        return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create UDP socket");
        return -1;
    }

    int broadcast_enabled = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enabled,
                   sizeof(broadcast_enabled)) < 0)
    {
        ESP_LOGE(TAG, "Unable to enable UDP broadcast");
        close(sock);
        return -1;
    }

    struct sockaddr_in destination = {
        .sin_family = AF_INET,
        .sin_port = htons(PARKING_SERVER_PORT),
    };
    destination.sin_addr.s_addr = inet_addr(PARKING_SERVER_BROADCAST);

    int sent = sendto(sock, payload, payload_len, 0,
                      (struct sockaddr *)&destination, sizeof(destination));
    if (sent < 0)
        ESP_LOGE(TAG, "UDP send failed");
    else
        ESP_LOGI(TAG, "Sent parking state (%d bytes): %s", sent, payload);

    close(sock);
    return sent < 0 ? -1 : 0;
}

/* Example call from your application after Wi-Fi is connected:
 *
 *     const int slots[5] = {1, 0, 0, 1, 0};
 *     ESP_ERROR_CHECK(send_parking_update(slots, 28.0f, 0.2f) == 0 ? ESP_OK : ESP_FAIL);
 *
 * For a reusable component, put this function in its own .c/.h pair and add
 * `lwip` and `log` to that component's REQUIRES list.
 */
