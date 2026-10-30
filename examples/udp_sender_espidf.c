/*
 * ESP-IDF example: call send_parking_update() after Wi-Fi connects to
 * ESP32-Parking. Add lwip to the component's REQUIRES list and copy
 * Server/main/parking/packet_auth.{c,h} into the component: the server drops unsigned packets.
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "packet_auth.h"

#define PARKING_SERVER_BROADCAST "192.168.4.255"
#define PARKING_SERVER_PORT 3333

static const char *TAG = "PARKING_UDP_CLIENT";

/* Must match PARKING_UDP_KEY on the server ("ParkingKey-Demo!" by default). */
static const uint8_t UDP_KEY[16] = {0x50, 0x61, 0x72, 0x6b, 0x69, 0x6e, 0x67, 0x4b,
                                    0x65, 0x79, 0x2d, 0x44, 0x65, 0x6d, 0x6f, 0x21};
/* shortcut: restarts at 1 on boot, so after a reboot the server ignores it until it passes the
 * old value; keep it in NVS (as the ESP8266 clients keep theirs in EEPROM) for real use. */
static uint32_t seq;

/* slots[i] is 1 when occupied and 0 when free. */
int send_parking_update(const int slots[5], float water_mm, bool fire)
{
    char payload[128];
    int water_tenths = (int)(water_mm * 10.0f + 0.5f);
    int payload_len = snprintf(payload, sizeof(payload),
                               "{\"type\":\"parking_update\",\"slots\":[%d,%d,%d,%d,%d],\"water_mm\":%d.%d,\"fire\":%s}",
                               slots[0], slots[1], slots[2], slots[3], slots[4],
                               water_tenths / 10, water_tenths % 10,
                               fire ? "true" : "false");
    if (payload_len < 0 || payload_len >= sizeof(payload))
        return -1;
    char packet[sizeof(payload) + PACKET_AUTH_HEADER_LEN];
    size_t packet_len = packet_auth_seal(UDP_KEY, ++seq, payload, packet, sizeof(packet));

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

    int sent = sendto(sock, packet, packet_len, 0,
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
 *     ESP_ERROR_CHECK(send_parking_update(slots, 0.2f, false) == 0 ? ESP_OK : ESP_FAIL);
 *
 * For a reusable component, put this function in its own .c/.h pair and add
 * `lwip` and `log` to that component's REQUIRES list.
 */
