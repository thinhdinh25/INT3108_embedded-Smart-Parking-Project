#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "server.h"

extern const unsigned char index_html_start[] asm("_binary_server_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_server_index_html_end");
extern const unsigned char style_css_start[] asm("_binary_server_style_css_start");
extern const unsigned char style_css_end[] asm("_binary_server_style_css_end");

// ============================================================
// Wi-Fi configuration
// ============================================================

#define WIFI_AP_SSID       "ESP32-Parking"
#define WIFI_AP_PASSWORD   "12345678"
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_MAX_CONN   4

// ============================================================
// Logging
// ============================================================

static const char *TAG = "SERVER";

// ============================================================
// HTTP GET "/" handler
// ============================================================

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

// ============================================================
// Start HTTP server
// ============================================================

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_handle_t server = NULL;

    esp_err_t err = httpd_start(&server, &config);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return err;
    }

    httpd_uri_t style_uri = {
        .uri      = "/style.css",
        .method   = HTTP_GET,
        .handler  = style_get_handler,
        .user_ctx = NULL
    };

    err = httpd_register_uri_handler(server, &style_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register stylesheet URI");
        httpd_stop(server);
        return err;
    }

    ESP_LOGI(TAG,
             "HTTP server started on port %d",
             config.server_port);

    // Register GET /
    httpd_uri_t root_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = root_get_handler,
        .user_ctx = NULL
    };

    err = httpd_register_uri_handler(server, &root_uri);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register root URI");

        httpd_stop(server);

        return err;
    }

    ESP_LOGI(TAG, "HTTP GET / registered");

    return ESP_OK;
}

// ============================================================
// Wi-Fi event handler
// ============================================================

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base != WIFI_EVENT)
    {
        return;
    }

    switch (event_id)
    {
        // ----------------------------------------------------
        // AP started
        // ----------------------------------------------------

        case WIFI_EVENT_AP_START:

            ESP_LOGI(TAG, "Wi-Fi Access Point started");

            ESP_LOGI(TAG,
                     "SSID: %s",
                     WIFI_AP_SSID);

            ESP_LOGI(TAG,
                     "Password: %s",
                     WIFI_AP_PASSWORD);

            ESP_LOGI(TAG,
                     "IP address: 192.168.4.1");

            break;

        // ----------------------------------------------------
        // Device connected
        // ----------------------------------------------------

        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event =
                (wifi_event_ap_staconnected_t *)event_data;

            ESP_LOGI(
                TAG,
                "Device connected: "
                "%02X:%02X:%02X:%02X:%02X:%02X",
                event->mac[0],
                event->mac[1],
                event->mac[2],
                event->mac[3],
                event->mac[4],
                event->mac[5]
            );

            break;
        }

        // ----------------------------------------------------
        // Device disconnected
        // ----------------------------------------------------

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event =
                (wifi_event_ap_stadisconnected_t *)event_data;

            ESP_LOGI(
                TAG,
                "Device disconnected: "
                "%02X:%02X:%02X:%02X:%02X:%02X",
                event->mac[0],
                event->mac[1],
                event->mac[2],
                event->mac[3],
                event->mac[4],
                event->mac[5]
            );

            break;
        }

        default:
            break;
    }
}

// ============================================================
// Initialize Wi-Fi SoftAP
// ============================================================

static esp_err_t start_wifi_ap(void)
{
    // Initialize TCP/IP stack
    esp_err_t err = esp_netif_init();

    if (err != ESP_OK)
    {
        return err;
    }

    // Create default event loop
    err = esp_event_loop_create_default();

    if (err != ESP_OK)
    {
        return err;
    }

    // Create default Wi-Fi AP network interface
    esp_netif_t *wifi_ap = esp_netif_create_default_wifi_ap();

    if (wifi_ap == NULL)
    {
        ESP_LOGE(TAG, "Failed to create Wi-Fi AP interface");
        return ESP_FAIL;
    }

    // Initialize Wi-Fi
    wifi_init_config_t wifi_init_cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&wifi_init_cfg);

    if (err != ESP_OK)
    {
        return err;
    }

    // Register event handler
    err = esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL
    );

    if (err != ESP_OK)
    {
        return err;
    }

    // --------------------------------------------------------
    // Configure SoftAP
    // --------------------------------------------------------

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),

            .channel = WIFI_AP_CHANNEL,

            .password = WIFI_AP_PASSWORD,

            .max_connection = WIFI_AP_MAX_CONN,

            .authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .required = false
            }
        }
    };

    // Set Wi-Fi mode = Access Point
    err = esp_wifi_set_mode(WIFI_MODE_AP);

    if (err != ESP_OK)
    {
        return err;
    }

    // Apply AP configuration
    err = esp_wifi_set_config(
        WIFI_IF_AP,
        &wifi_config
    );

    if (err != ESP_OK)
    {
        return err;
    }

    // Start Wi-Fi
    err = esp_wifi_start();

    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "SoftAP initialized successfully");

    return ESP_OK;
}

// ============================================================
// Public function
// ============================================================

esp_err_t server_start(void)
{
    esp_err_t err;

    // --------------------------------------------------------
    // Start Wi-Fi AP
    // --------------------------------------------------------

    err = start_wifi_ap();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Failed to initialize Wi-Fi AP: %s",
                 esp_err_to_name(err));

        return err;
    }

    // --------------------------------------------------------
    // Start HTTP server
    // --------------------------------------------------------

    err = start_http_server();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Failed to initialize HTTP server: %s",
                 esp_err_to_name(err));

        return err;
    }

    ESP_LOGI(TAG,
             "==========================================");

    ESP_LOGI(TAG,
             "Smart Parking Server is ready!");

    ESP_LOGI(TAG,
             "Wi-Fi: %s",
             WIFI_AP_SSID);

    ESP_LOGI(TAG,
             "Open: http://192.168.4.1");

    ESP_LOGI(TAG,
             "==========================================");

    return ESP_OK;
}
