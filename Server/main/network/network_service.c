#include "esp_log.h"
#include "mdns.h"

#include "http/http_server.h"
#include "network_service.h"
#include "udp/udp_server.h"
#include "wifi_ap.h"

#define MDNS_HOSTNAME "parking"

static const char *TAG = "SERVER";

/* Answer "parking.local" on the joined Wi-Fi so browsers need not know the DHCP address. */
static esp_err_t mdns_service_start(void)
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK)
        err = mdns_hostname_set(MDNS_HOSTNAME);
    if (err == ESP_OK)
        err = mdns_instance_name_set("Smart Parking Server");
    if (err == ESP_OK)
        err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    return err;
}

esp_err_t network_service_start(void)
{
    esp_err_t err = wifi_ap_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi station: %s", esp_err_to_name(err));
        return err;
    }

    /* The dashboard stays reachable by IP if mDNS fails, so do not stop the server. */
    err = mdns_service_start();
    if (err != ESP_OK)
        ESP_LOGW(TAG, "mDNS unavailable, use the IP address instead: %s", esp_err_to_name(err));

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
    ESP_LOGI(TAG, "Web interface: http://" MDNS_HOSTNAME ".local");

    return ESP_OK;
}
