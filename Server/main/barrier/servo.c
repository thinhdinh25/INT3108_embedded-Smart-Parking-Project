#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "servo.h"
#include <stdint.h>

#define SERVO_GPIO 0
#define SERVO_GPIO_2 1
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_CHANNEL LEDC_CHANNEL_0
#define SERVO_CHANNEL_2 LEDC_CHANNEL_1
#define SERVO_FREQUENCY_HZ 50
#define SERVO_PERIOD_US 20000
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2400

static const char *TAG = "SERVO";
static bool barrier_open;

static esp_err_t servo_set_channel_angle(ledc_channel_t channel, int angle)
{
    const uint32_t pulse_us = SERVO_MIN_PULSE_US +
        (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * (uint32_t)angle / 180;
    const uint32_t duty = pulse_us * ((1U << 14) - 1) / SERVO_PERIOD_US;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (err != ESP_OK)
        return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

static esp_err_t servo_set_angle(int angle)
{
    esp_err_t err = servo_set_channel_angle(SERVO_CHANNEL, angle);
    if (err != ESP_OK)
        return err;
    return servo_set_channel_angle(SERVO_CHANNEL_2, angle);
}

esp_err_t servo_start(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = SERVO_TIMER,
        .freq_hz = SERVO_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK)
        return err;

    const ledc_channel_config_t channel = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK)
        return err;

    const ledc_channel_config_t channel_2 = {
        .gpio_num = SERVO_GPIO_2,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_CHANNEL_2,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_2);
    if (err != ESP_OK)
        return err;

    nvs_handle_t nvs;
    err = nvs_open("barrier", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;

    uint8_t saved_state = 0;
    err = nvs_get_u8(nvs, "open", &saved_state);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;
    nvs_close(nvs);
    if (err != ESP_OK)
        return err;

    barrier_open = saved_state != 0;
    return servo_set_angle(barrier_open ? 90 : 0);
}

esp_err_t servo_set_open(bool open)
{
    if (barrier_open == open)
        return ESP_OK;

    esp_err_t err = servo_set_angle(open ? 90 : 0);
    if (err != ESP_OK)
        return err;

    barrier_open = open;
    nvs_handle_t nvs;
    err = nvs_open("barrier", NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        err = nvs_set_u8(nvs, "open", open ? 1 : 0);
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Barrier moved, but saving its state failed: %s", esp_err_to_name(err));
    return ESP_OK;
}

bool servo_is_open(void)
{
    return barrier_open;
}
