#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "../parking/parking_state.h"
#include "udp_server.h"

#define UDP_BUFFER_SIZE 256

static const char *TAG = "UDP_SERVER";

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

/* Parse type, five binary slots, temperature_c and water_mm. */
static bool parse_parking_update(const char *payload, parking_state_t *state)
{
    const char *cursor = payload;
    bool has_type = false;
    bool has_slots = false;
    bool has_temperature = false;
    bool has_water = false;
    if (!consume_json_char(&cursor, '{'))
        return false;

    for (int field = 0; field < 4; ++field)
    {
        skip_json_whitespace(&cursor);
        bool is_type = false;
        bool is_slots = false;
        bool is_temperature = false;
        bool is_water = false;
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
            is_slots = true;
            cursor += 7;
            if (has_slots)
                return false;
            has_slots = true;
        }
        else if (strncmp(cursor, "\"temperature_c\"", 15) == 0)
        {
            is_temperature = true;
            cursor += 15;
            if (has_temperature)
                return false;
            has_temperature = true;
        }
        else if (strncmp(cursor, "\"water_mm\"", 10) == 0)
        {
            is_water = true;
            cursor += 10;
            if (has_water)
                return false;
            has_water = true;
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
        else if (is_slots)
        {
            if (!consume_json_char(&cursor, '['))
                return false;
            for (int i = 0; i < PARKING_SLOT_COUNT; ++i)
            {
                skip_json_whitespace(&cursor);
                if (*cursor != '0' && *cursor != '1')
                    return false;
                state->slots[i] = *cursor++ - '0';
                if (i < PARKING_SLOT_COUNT - 1 && !consume_json_char(&cursor, ','))
                    return false;
            }
            if (!consume_json_char(&cursor, ']'))
                return false;
        }
        else
        {
            skip_json_whitespace(&cursor);
            if ((*cursor < '0' || *cursor > '9') && *cursor != '-')
                return false;
            char *end = NULL;
            float value = strtof(cursor, &end);
            if (end == cursor)
                return false;
            cursor = end;
            if (is_temperature)
            {
                if (!(value >= -40.0f && value <= 125.0f))
                    return false;
                state->temperature_c = value;
            }
            else if (is_water)
            {
                if (!(value >= 0.0f && value <= 10000.0f))
                    return false;
                state->water_mm = value;
            }
        }

        if (field < 3 && !consume_json_char(&cursor, ','))
            return false;
    }

    if (!has_type || !has_slots || !has_temperature || !has_water ||
        !consume_json_char(&cursor, '}'))
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

            parking_state_t new_state = {0};
            if (!parse_parking_update(buffer, &new_state))
            {
                ESP_LOGW(TAG, "Ignoring invalid parking update");
                continue;
            }

            parking_state_update(&new_state);
            ESP_LOGI(TAG, "Parking update accepted (%d/5 free)",
                     (new_state.slots[0] == 0) + (new_state.slots[1] == 0) +
                     (new_state.slots[2] == 0) + (new_state.slots[3] == 0) +
                     (new_state.slots[4] == 0));
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
