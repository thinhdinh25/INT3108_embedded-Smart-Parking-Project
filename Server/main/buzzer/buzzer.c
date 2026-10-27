#include "driver/gpio.h"

#include "buzzer.h"

#define BUZZER_GPIO GPIO_NUM_9

esp_err_t buzzer_start(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK)
        return err;

    /* This buzzer is active-low: HIGH is silent during startup. */
    return gpio_set_level(BUZZER_GPIO, 1);
}

void buzzer_set_fire(bool fire_detected)
{
    /* Active-low buzzer: LOW sounds when fire is detected. */
    gpio_set_level(BUZZER_GPIO, fire_detected ? 0 : 1);
}
