#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "../parking/parking_state.h"
#include "../parking/parking_update.h"
#include "../parking/rfid_session.h"
#include "../../barrier/servo.h"
#include "../../buzzer/buzzer.h"
#include "esp_timer.h"
#include "udp_server.h"

#define UDP_BUFFER_SIZE 256

static const char *TAG = "UDP_SERVER";

static void udp_server_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (socket_fd < 0)
        {
            ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(UDP_SERVER_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        if (bind(socket_fd, (struct sockaddr *)&server_addr,
                 sizeof(server_addr)) < 0)
        {
            ESP_LOGE(TAG, "Unable to bind UDP port %d: errno %d",
                     UDP_SERVER_PORT, errno);
            close(socket_fd);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "UDP server listening on port %d", UDP_SERVER_PORT);

        for (;;)
        {
            char buffer[UDP_BUFFER_SIZE];
            struct sockaddr_in source_addr;
            socklen_t source_addr_len = sizeof(source_addr);
            int received = recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0,
                                    (struct sockaddr *)&source_addr,
                                    &source_addr_len);

            if (received < 0)
            {
                ESP_LOGE(TAG, "UDP receive failed: errno %d", errno);
                break;
            }

            buffer[received] = '\0';
            ESP_LOGI(TAG, "UDP request from %s:%u (%d bytes): %s",
                     inet_ntoa(source_addr.sin_addr),
                     ntohs(source_addr.sin_port), received, buffer);

            parking_state_t new_state = {0};
            if (parking_update_parse_json(buffer, &new_state))
            {
                parking_state_update(&new_state);
                buzzer_set_fire(new_state.fire);
                esp_err_t fire_err = servo_set_fire_alarm(new_state.fire);
                if (fire_err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to update fire barrier mode: %s", esp_err_to_name(fire_err));
                ESP_LOGI(TAG, "Parking update accepted (%d/5 free)",
                         (new_state.slots[0] == 0) + (new_state.slots[1] == 0) +
                         (new_state.slots[2] == 0) + (new_state.slots[3] == 0) +
                         (new_state.slots[4] == 0));
                continue;
            }

            rfid_event_t event = {0};
            if (!rfid_event_parse_json(buffer, &event))
            {
                ESP_LOGW(TAG, "Ignoring invalid or unknown UDP request");
                continue;
            }

            rfid_scan_result_t result = rfid_session_scan(&event, (uint64_t)esp_timer_get_time());
            if (result == RFID_SCAN_ENTERED)
            {
                ESP_LOGI(TAG, "RFID %s entered via IN reader", event.card_id);
                esp_err_t err = servo_open_for(SERVO_GATE_IN, 3000);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to open entry barrier: %s", esp_err_to_name(err));
            }
            else if (result == RFID_SCAN_EXITED)
            {
                ESP_LOGI(TAG, "RFID %s exited via OUT reader", event.card_id);
                esp_err_t err = servo_open_for(SERVO_GATE_OUT, 3000);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to open exit barrier: %s", esp_err_to_name(err));
            }
            else if (result == RFID_SCAN_INVALID_GATE)
            {
                ESP_LOGW(TAG, "RFID %s rejected: scanned on wrong reader (%s)",
                         event.card_id, event.gate == RFID_GATE_IN ? "IN" : "OUT");
            }
            else
                ESP_LOGW(TAG, "RFID active-session capacity reached for %s", event.card_id);
        }

        close(socket_fd);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t udp_server_start(void)
{
    if (xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create UDP server task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
