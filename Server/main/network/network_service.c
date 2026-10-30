#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "sdkconfig.h"

#include "http/http_server.h"
#include "network_service.h"
#include "udp/udp_server.h"
#include "wifi_sta.h"

#define MDNS_HOSTNAME "parking"

static const char *TAG = "SERVER";

/* Re-announce parking.local whenever DHCP (re)assigns an address, including a new
 * address without a disconnect, so other devices never keep resolving a stale IP. */
static void mdns_ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *event = data;
    esp_err_t err = mdns_netif_action(event->esp_netif,
                                      MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "mDNS: " MDNS_HOSTNAME ".local -> " IPSTR "%s",
                 IP2STR(&event->ip_info.ip), event->ip_changed ? " (address changed)" : "");
    else
        ESP_LOGW(TAG, "mDNS announce failed: %s", esp_err_to_name(err));
}

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
    /* Registered after mdns_init so it runs after the component's own handler. */
    if (err == ESP_OK)
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, mdns_ip_event_handler, NULL);
    return err;
}

esp_err_t network_service_start(void)
{
    esp_err_t err = wifi_sta_start();
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
    ESP_LOGI(TAG, "Connecting to Wi-Fi hotspot: " CONFIG_PARKING_WIFI_SSID);
    ESP_LOGI(TAG, "Web interface: http://" MDNS_HOSTNAME ".local");

    return ESP_OK;
}
