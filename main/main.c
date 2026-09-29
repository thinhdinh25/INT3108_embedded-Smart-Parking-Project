#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include "server.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    // Start Wi-Fi AP + Web Server
    ESP_ERROR_CHECK(server_start());

    ESP_LOGI(TAG, "Application started");
}