#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

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
