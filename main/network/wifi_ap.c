#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "wifi_ap.h"

#define WIFI_AP_SSID       "ESP32-Parking"
#define WIFI_AP_PASSWORD   "12345678"
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_MAX_CONN   4

static const char *TAG = "WIFI_AP";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base != WIFI_EVENT)
    {
        return;
    }

    switch (event_id)
    {
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "Access point started: SSID=%s, IP=192.168.4.1",
                     WIFI_AP_SSID);
            break;

        case WIFI_EVENT_AP_STACONNECTED:
        {
            const wifi_event_ap_staconnected_t *event = event_data;
            ESP_LOGI(TAG, "Device connected: %02X:%02X:%02X:%02X:%02X:%02X",
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            const wifi_event_ap_stadisconnected_t *event = event_data;
            ESP_LOGI(TAG, "Device disconnected: %02X:%02X:%02X:%02X:%02X:%02X",
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);
            break;
        }

        default:
            break;
    }
}

esp_err_t wifi_ap_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        return err;
    }

    if (esp_netif_create_default_wifi_ap() == NULL)
    {
        ESP_LOGE(TAG, "Failed to create Wi-Fi AP interface");
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK)
    {
        return err;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "SoftAP initialized");
    return ESP_OK;
}
