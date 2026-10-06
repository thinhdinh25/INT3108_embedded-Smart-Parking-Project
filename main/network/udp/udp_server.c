#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "udp_server.h"

#define UDP_BUFFER_SIZE 256
#define PARKING_SLOT_COUNT 5

static const char *TAG = "UDP_SERVER";
static int parking_slots[PARKING_SLOT_COUNT] = {0};
static bool parking_state_received;
static portMUX_TYPE parking_state_lock = portMUX_INITIALIZER_UNLOCKED;

bool udp_server_get_parking_state(int *slots, size_t capacity, int *free_count)
{
    if (slots == NULL || capacity < PARKING_SLOT_COUNT || free_count == NULL)
        return false;

    int free_slots = 0;
    bool received;
    portENTER_CRITICAL(&parking_state_lock);
    memcpy(slots, parking_slots, sizeof(parking_slots));
    received = parking_state_received;
    for (size_t i = 0; i < PARKING_SLOT_COUNT; ++i)
        free_slots += parking_slots[i] == 0;
    portEXIT_CRITICAL(&parking_state_lock);
    *free_count = free_slots;
    return received;
}

static void skip_json_whitespace(const char **cursor)
{
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\r' || **cursor == '\n')
        ++(*cursor);
}

static bool consume_json_char(const char **cursor, char expected)
{
    skip_json_whitespace(cursor);
    if (**cursor != expected)
        return false;
    ++(*cursor);
    return true;
}

static bool consume_json_string(const char **cursor, const char *expected)
{
    skip_json_whitespace(cursor);
    if (**cursor != '"')
        return false;
    ++(*cursor);
    while (*expected != '\0')
    {
        if (**cursor != *expected)
            return false;
        ++(*cursor);
        ++expected;
    }
    if (**cursor != '"')
        return false;
    ++(*cursor);
    return true;
}

/* Parse the protocol's two fields, allowing either field order and whitespace. */
static bool parse_parking_update(const char *payload, int *slots)
{
    const char *cursor = payload;
    bool has_type = false;
    bool has_slots = false;
    if (!consume_json_char(&cursor, '{'))
        return false;

    for (int field = 0; field < 2; ++field)
    {
        skip_json_whitespace(&cursor);
        bool is_type;
        if (strncmp(cursor, "\"type\"", 6) == 0)
        {
            is_type = true;
            cursor += 6;
            if (has_type)
                return false;
            has_type = true;
        }
        else if (strncmp(cursor, "\"slots\"", 7) == 0)
        {
            is_type = false;
            cursor += 7;
            if (has_slots)
                return false;
            has_slots = true;
        }
        else
            return false;

        if (!consume_json_char(&cursor, ':'))
            return false;
        if (is_type)
        {
            if (!consume_json_string(&cursor, "parking_update"))
                return false;
        }
        else
        {
            if (!consume_json_char(&cursor, '['))
                return false;
            for (int i = 0; i < PARKING_SLOT_COUNT; ++i)
            {
                skip_json_whitespace(&cursor);
                if (*cursor != '0' && *cursor != '1')
                    return false;
                slots[i] = *cursor++ - '0';
                if (i < PARKING_SLOT_COUNT - 1 && !consume_json_char(&cursor, ','))
                    return false;
            }
            if (!consume_json_char(&cursor, ']'))
                return false;
        }

        if (field == 0 && !consume_json_char(&cursor, ','))
            return false;
    }

    if (!has_type || !has_slots || !consume_json_char(&cursor, '}'))
        return false;
    skip_json_whitespace(&cursor);
    return *cursor == '\0';
}

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

            int new_slots[PARKING_SLOT_COUNT];
            if (!parse_parking_update(buffer, new_slots))
            {
                ESP_LOGW(TAG, "Ignoring invalid parking update");
                continue;
            }

            portENTER_CRITICAL(&parking_state_lock);
            memcpy(parking_slots, new_slots, sizeof(parking_slots));
            parking_state_received = true;
            portEXIT_CRITICAL(&parking_state_lock);
            ESP_LOGI(TAG, "Parking update accepted (%d/5 free)",
                     (new_slots[0] == 0) + (new_slots[1] == 0) +
                     (new_slots[2] == 0) + (new_slots[3] == 0) +
                     (new_slots[4] == 0));
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
