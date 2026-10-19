#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "barrier_servo.h"

#define SERVO_PWM_FREQUENCY_HZ 50
#define SERVO_PWM_RESOLUTION LEDC_TIMER_14_BIT
#define SERVO_PWM_PERIOD_US 20000
#define SERVO_CLOSED_PULSE_US 500
#define SERVO_OPEN_PULSE_US 1500
#define SERVO_MOVE_INTERVAL_MS 3000

#define ENTRY_SERVO_GPIO 0
#define EXIT_SERVO_GPIO 1

static const char *TAG = "BARRIER_SERVO";

static const ledc_channel_t servo_channels[] = {
    LEDC_CHANNEL_0,
    LEDC_CHANNEL_1,
};

static void set_servo_pulse(ledc_channel_t channel, uint32_t pulse_us)
{
    const uint32_t max_duty = (1U << 14) - 1U;
    const uint32_t duty = (pulse_us * max_duty) / SERVO_PWM_PERIOD_US;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
}

static void set_both_servos(uint32_t pulse_us)
{
    for (size_t i = 0; i < sizeof(servo_channels) / sizeof(servo_channels[0]); ++i)
        set_servo_pulse(servo_channels[i], pulse_us);
}

static void servo_test_task(void *arg)
{
    (void)arg;
    bool open = false;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(SERVO_MOVE_INTERVAL_MS));
        open = !open;
        set_both_servos(open ? SERVO_OPEN_PULSE_US : SERVO_CLOSED_PULSE_US);
        ESP_LOGI(TAG, "Both barriers %s", open ? "open" : "closed");
    }
}

esp_err_t barrier_servo_start(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = SERVO_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK)
        return err;

    const int servo_gpios[] = {ENTRY_SERVO_GPIO, EXIT_SERVO_GPIO};
    for (size_t i = 0; i < sizeof(servo_gpios) / sizeof(servo_gpios[0]); ++i)
    {
        const ledc_channel_config_t channel_config = {
            .gpio_num = servo_gpios[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = servo_channels[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&channel_config);
        if (err != ESP_OK)
            return err;
    }

    /* Start closed; then alternate the barriers every three seconds. */
    set_both_servos(SERVO_CLOSED_PULSE_US);

    if (xTaskCreate(servo_test_task, "barrier_servo_test", 2048,
                    NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Unable to create servo test task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Servo test started on GPIO%d and GPIO%d",
             ENTRY_SERVO_GPIO, EXIT_SERVO_GPIO);
    return ESP_OK;
}
