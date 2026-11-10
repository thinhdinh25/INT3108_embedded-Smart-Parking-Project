#include "driver/gpio.h"

#include "buzzer.h"

#define BUZZER_GPIO GPIO_NUM_9

static volatile bool alarm_on;
static volatile bool warning_on;

/* Active-low buzzer: LOW sounds. */
static void buzzer_apply(void)
{
    gpio_set_level(BUZZER_GPIO, alarm_on || warning_on ? 0 : 1);
}

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

void buzzer_set_alarm(bool alarm)
{
    alarm_on = alarm;
    buzzer_apply();
}

void buzzer_set_warning(bool warning)
{
    warning_on = warning;
    buzzer_apply();
}
