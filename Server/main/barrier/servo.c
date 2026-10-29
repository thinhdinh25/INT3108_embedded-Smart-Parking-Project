#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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
#define AUTO_CLOSE_CHECK_MS 50

static const char *TAG = "SERVO";
static bool in_open;
static bool out_open;
static bool fire_alarm_active;
static SemaphoreHandle_t servo_mutex;
static int64_t in_auto_close_at_us;
static int64_t out_auto_close_at_us;

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

static int servo_angle_for_state(bool is_out_gate, bool open)
{
    /* The two gates are mounted in opposite orientations. */
    if (is_out_gate)
        return open ? 0 : 90;
    return open ? 90 : 0;
}

static esp_err_t servo_set_state(ledc_channel_t channel, const char *key,
                                 bool *state, bool is_out_gate, bool open)
{
    if (*state == open)
        return ESP_OK;

    esp_err_t err = servo_set_angle(channel, servo_angle_for_state(is_out_gate, open));
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

static void servo_auto_close_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(AUTO_CLOSE_CHECK_MS));
        int64_t now_us = esp_timer_get_time();
        xSemaphoreTake(servo_mutex, portMAX_DELAY);

        if (in_auto_close_at_us != 0 && now_us >= in_auto_close_at_us)
        {
            in_auto_close_at_us = 0;
            esp_err_t err = servo_set_state(SERVO_IN_CHANNEL, "in_open", &in_open,
                                            false, false);
            if (err != ESP_OK)
                ESP_LOGE(TAG, "Failed to auto-close entry barrier: %s", esp_err_to_name(err));
        }
        if (out_auto_close_at_us != 0 && now_us >= out_auto_close_at_us)
        {
            out_auto_close_at_us = 0;
            esp_err_t err = servo_set_state(SERVO_OUT_CHANNEL, "out_open", &out_open,
                                            true, false);
            if (err != ESP_OK)
                ESP_LOGE(TAG, "Failed to auto-close exit barrier: %s", esp_err_to_name(err));
        }

        xSemaphoreGive(servo_mutex);
    }
}

esp_err_t servo_start(void)
{
    servo_mutex = xSemaphoreCreateMutex();
    if (servo_mutex == NULL)
        return ESP_ERR_NO_MEM;

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

    /* Default both gates to closed on startup. */
    in_open = false;
    out_open = false;
    fire_alarm_active = false;
    in_auto_close_at_us = 0;
    out_auto_close_at_us = 0;

    err = servo_set_angle(SERVO_IN_CHANNEL, servo_angle_for_state(false, in_open));
    if (err != ESP_OK)
        return err;
    err = servo_set_angle(SERVO_OUT_CHANNEL, servo_angle_for_state(true, out_open));
    if (err != ESP_OK)
        return err;

    nvs_handle_t nvs;
    err = nvs_open("barrier", NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        err = nvs_set_u8(nvs, "in_open", 0);
        if (err == ESP_OK)
            err = nvs_set_u8(nvs, "out_open", 0);
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed to save default closed state to NVS: %s", esp_err_to_name(err));

    if (xTaskCreate(servo_auto_close_task, "servo_auto_close", 3072, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t servo_set_in_open(bool open)
{
    if (servo_mutex == NULL)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    if (fire_alarm_active && !open)
    {
        xSemaphoreGive(servo_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    in_auto_close_at_us = 0;
    esp_err_t err = servo_set_state(SERVO_IN_CHANNEL, "in_open", &in_open, false, open);
    xSemaphoreGive(servo_mutex);
    return err;
}

esp_err_t servo_set_out_open(bool open)
{
    if (servo_mutex == NULL)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    if (fire_alarm_active && !open)
    {
        xSemaphoreGive(servo_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    out_auto_close_at_us = 0;
    esp_err_t err = servo_set_state(SERVO_OUT_CHANNEL, "out_open", &out_open, true, open);
    xSemaphoreGive(servo_mutex);
    return err;
}

esp_err_t servo_set_all_open(bool open)
{
    esp_err_t in_err = servo_set_in_open(open);
    esp_err_t out_err = servo_set_out_open(open);
    return in_err != ESP_OK ? in_err : out_err;
}

esp_err_t servo_open_for(servo_gate_t gate, uint32_t duration_ms)
{
    if (servo_mutex == NULL || duration_ms == 0)
        return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    if (fire_alarm_active)
    {
        xSemaphoreGive(servo_mutex);
        return ESP_OK;
    }
    int64_t deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    esp_err_t err;
    if (gate == SERVO_GATE_IN)
    {
        err = servo_set_state(SERVO_IN_CHANNEL, "in_open", &in_open, false, true);
        if (err == ESP_OK)
            in_auto_close_at_us = deadline_us;
    }
    else if (gate == SERVO_GATE_OUT)
    {
        err = servo_set_state(SERVO_OUT_CHANNEL, "out_open", &out_open, true, true);
        if (err == ESP_OK)
            out_auto_close_at_us = deadline_us;
    }
    else
        err = ESP_ERR_INVALID_ARG;

    xSemaphoreGive(servo_mutex);
    return err;
}

esp_err_t servo_set_fire_alarm(bool active)
{
    if (servo_mutex == NULL)
        return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    if (fire_alarm_active == active && !active)
    {
        xSemaphoreGive(servo_mutex);
        return ESP_OK;
    }

    fire_alarm_active = active;
    in_auto_close_at_us = 0;
    out_auto_close_at_us = 0;
    esp_err_t in_err = servo_set_state(SERVO_IN_CHANNEL, "in_open", &in_open,
                                       false, active);
    esp_err_t out_err = servo_set_state(SERVO_OUT_CHANNEL, "out_open", &out_open,
                                        true, active);
    xSemaphoreGive(servo_mutex);
    return in_err != ESP_OK ? in_err : out_err;
}

bool servo_is_in_open(void)
{
    if (servo_mutex == NULL)
        return in_open;
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    bool open = in_open;
    xSemaphoreGive(servo_mutex);
    return open;
}

bool servo_is_out_open(void)
{
    if (servo_mutex == NULL)
        return out_open;
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    bool open = out_open;
    xSemaphoreGive(servo_mutex);
    return open;
}
