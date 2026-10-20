#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "servo.h"
#include <stdint.h>

#define SERVO_IN_GPIO 0
#define SERVO_OUT_GPIO 1
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_IN_CHANNEL LEDC_CHANNEL_0
#define SERVO_OUT_CHANNEL LEDC_CHANNEL_1
#define SERVO_FREQUENCY_HZ 50
#define SERVO_PERIOD_US 20000
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2400

static const char *TAG = "SERVO";
static bool in_open;
static bool out_open;

static esp_err_t servo_set_angle(ledc_channel_t channel, int angle)
{
    const uint32_t pulse_us = SERVO_MIN_PULSE_US +
        (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * (uint32_t)angle / 180;
    const uint32_t duty = pulse_us * ((1U << 14) - 1) / SERVO_PERIOD_US;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (err != ESP_OK)
        return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

static esp_err_t servo_set_state(ledc_channel_t channel, const char *key, bool *state, bool open)
{
    if (*state == open)
        return ESP_OK;

    esp_err_t err = servo_set_angle(channel, open ? 90 : 0);
    if (err != ESP_OK)
        return err;

    *state = open;
    nvs_handle_t nvs;
    err = nvs_open("barrier", NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        err = nvs_set_u8(nvs, key, open ? 1 : 0);
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Servo moved, but saving %s state failed: %s", key, esp_err_to_name(err));
    return ESP_OK;
}

static esp_err_t load_state(nvs_handle_t nvs, const char *key, bool *state)
{
    uint8_t saved = 0;
    esp_err_t err = nvs_get_u8(nvs, key, &saved);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;
    if (err == ESP_OK)
        *state = saved != 0;
    return err;
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

    const ledc_channel_config_t channels[] = {
        {.gpio_num = SERVO_IN_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = SERVO_IN_CHANNEL,
         .intr_type = LEDC_INTR_DISABLE, .timer_sel = SERVO_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = SERVO_OUT_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = SERVO_OUT_CHANNEL,
         .intr_type = LEDC_INTR_DISABLE, .timer_sel = SERVO_TIMER, .duty = 0, .hpoint = 0},
    };
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i)
    {
        err = ledc_channel_config(&channels[i]);
        if (err != ESP_OK)
            return err;
    }

    nvs_handle_t nvs;
    err = nvs_open("barrier", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    err = load_state(nvs, "in_open", &in_open);
    if (err == ESP_OK)
        err = load_state(nvs, "out_open", &out_open);
    nvs_close(nvs);
    if (err != ESP_OK)
        return err;

    err = servo_set_angle(SERVO_IN_CHANNEL, in_open ? 90 : 0);
    if (err != ESP_OK)
        return err;
    return servo_set_angle(SERVO_OUT_CHANNEL, out_open ? 90 : 0);
}

esp_err_t servo_set_in_open(bool open)
{
    return servo_set_state(SERVO_IN_CHANNEL, "in_open", &in_open, open);
}

esp_err_t servo_set_out_open(bool open)
{
    return servo_set_state(SERVO_OUT_CHANNEL, "out_open", &out_open, open);
}

esp_err_t servo_set_all_open(bool open)
{
    esp_err_t in_err = servo_set_in_open(open);
    esp_err_t out_err = servo_set_out_open(open);
    return in_err != ESP_OK ? in_err : out_err;
}

bool servo_is_in_open(void)
{
    return in_open;
}

bool servo_is_out_open(void)
{
    return out_open;
}
