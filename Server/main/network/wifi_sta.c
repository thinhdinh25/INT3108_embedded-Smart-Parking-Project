#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#include "wifi_sta.h"

/* Set in menuconfig ("Parking network and security"); real values stay in the git-ignored sdkconfig. */
#define WIFI_STA_SSID     CONFIG_PARKING_WIFI_SSID
#define WIFI_STA_PASSWORD CONFIG_PARKING_WIFI_PASSWORD

static const char *TAG = "WIFI_STA";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi station started, connecting to SSID=%s...",
                         WIFI_STA_SSID);
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to hotspot SSID=%s", WIFI_STA_SSID);
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
            {
                const wifi_event_sta_disconnected_t *event = event_data;
                ESP_LOGW(TAG, "Disconnected from SSID=%s (reason=%d), retrying...",
                         WIFI_STA_SSID, event->reason);
                esp_wifi_connect();
                break;
            }

            default:
                break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Web interface: http://" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_sta_start(void)
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

    if (esp_netif_create_default_wifi_sta() == NULL)
    {
        ESP_LOGE(TAG, "Failed to create Wi-Fi STA interface");
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

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK)
    {
        return err;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_STA_SSID,
            .password = WIFI_STA_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to disable Wi-Fi power save: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Wi-Fi station initialized");
    return ESP_OK;
}
