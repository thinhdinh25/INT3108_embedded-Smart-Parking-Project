#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "http_server.h"
#include "../parking/parking_state.h"
#include "../../barrier/servo.h"

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");
extern const unsigned char style_css_start[] asm("_binary_style_css_start");
extern const unsigned char style_css_end[] asm("_binary_style_css_end");

static const char *TAG = "HTTP_SERVER";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t style_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, (const char *)style_css_start,
                           style_css_end - style_css_start);
}

static esp_err_t parking_state_get_handler(httpd_req_t *req)
{
    parking_state_t state = {0};
    int free_count = 0;
    parking_state_get(&state, &free_count);
    char response[192];
    int temperature_tenths = (int)(state.temperature_c * 10.0f + (state.temperature_c >= 0.0f ? 0.5f : -0.5f));
    int water_tenths = (int)(state.water_mm * 10.0f + 0.5f);
    int length = snprintf(response, sizeof(response),
                          "{\"received\":%s,\"slots\":[%d,%d,%d,%d,%d],\"free\":%d,\"total\":5,\"temperature_c\":%s%d.%d,\"water_mm\":%d.%d,\"fire\":%s}",
                          state.received ? "true" : "false", state.slots[0], state.slots[1],
                          state.slots[2], state.slots[3], state.slots[4], free_count,
                          temperature_tenths < 0 ? "-" : "", abs(temperature_tenths) / 10,
                          abs(temperature_tenths % 10),
                          water_tenths / 10, water_tenths % 10,
                          state.fire ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, length);
}

static esp_err_t barrier_in_open_handler(httpd_req_t *req)
{
    esp_err_t err = servo_set_in_open(true);
    if (err != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Servo command failed");
    }
    return httpd_resp_sendstr(req, "OPEN");
}

static esp_err_t barrier_in_close_handler(httpd_req_t *req)
{
    esp_err_t err = servo_set_in_open(false);
    if (err != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Servo command failed");
    }
    return httpd_resp_sendstr(req, "CLOSED");
}

static esp_err_t barrier_out_open_handler(httpd_req_t *req)
{
    esp_err_t err = servo_set_out_open(true);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Servo command failed");
    return httpd_resp_sendstr(req, "OPEN");
}

static esp_err_t barrier_out_close_handler(httpd_req_t *req)
{
    esp_err_t err = servo_set_out_open(false);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Servo command failed");
    return httpd_resp_sendstr(req, "CLOSED");
}

static esp_err_t barrier_all_handler(httpd_req_t *req)
{
    bool open = strstr(req->uri, "/open") != NULL;
    esp_err_t err = servo_set_all_open(open);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Servo command failed");
    return httpd_resp_sendstr(req, open ? "ALL OPEN" : "ALL CLOSED");
}

static esp_err_t barrier_state_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, servo_is_in_open()
        ? (servo_is_out_open() ? "{\"in_open\":true,\"out_open\":true}" : "{\"in_open\":true,\"out_open\":false}")
        : (servo_is_out_open() ? "{\"in_open\":false,\"out_open\":true}" : "{\"in_open\":false,\"out_open\":false}"));
}

esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    httpd_handle_t server = NULL;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return err;
    }

    const httpd_uri_t routes[] = {
        {
            .uri = "/api/barrier",
            .method = HTTP_GET,
            .handler = barrier_state_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/api/barrier/in/open",
            .method = HTTP_POST,
            .handler = barrier_in_open_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/api/barrier/in/close",
            .method = HTTP_POST,
            .handler = barrier_in_close_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/api/barrier/out/open",
            .method = HTTP_POST,
            .handler = barrier_out_open_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/api/barrier/out/close",
            .method = HTTP_POST,
            .handler = barrier_out_close_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/api/barrier/all/open",
            .method = HTTP_POST,
            .handler = barrier_all_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/api/barrier/all/close",
            .method = HTTP_POST,
            .handler = barrier_all_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/api/parking",
            .method = HTTP_GET,
            .handler = parking_state_get_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/style.css",
            .method = HTTP_GET,
            .handler = style_get_handler,
            .user_ctx = NULL,
        },
        {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL,
        },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
    {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register HTTP route %s", routes[i].uri);
            httpd_stop(server);
            return err;
        }
    }

    ESP_LOGI(TAG, "HTTP server listening on port %d", config.server_port);
    return ESP_OK;
}
