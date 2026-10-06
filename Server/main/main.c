#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include "display/lcd_display.h"
#include "network/network_service.h"

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

    // Start the local screen before Wi-Fi consumes system memory.
    ESP_ERROR_CHECK(lcd_display_start());

    // Start Wi-Fi, HTTP, and UDP network services.
    ESP_ERROR_CHECK(network_service_start());

    ESP_LOGI(TAG, "Application started");
}
