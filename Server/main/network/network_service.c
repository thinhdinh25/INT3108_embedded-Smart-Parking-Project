#include "esp_log.h"

#include "http/http_server.h"
#include "network_service.h"
#include "udp/udp_server.h"
#include "wifi_ap.h"

static const char *TAG = "SERVER";

esp_err_t network_service_start(void)
{
    esp_err_t err = wifi_ap_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi station: %s", esp_err_to_name(err));
        return err;
    }

    err = http_server_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    err = udp_server_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start UDP server: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Smart Parking Server is ready");
    ESP_LOGI(TAG, "Connecting to Wi-Fi hotspot: ESP32-Parking");

    return ESP_OK;
}
