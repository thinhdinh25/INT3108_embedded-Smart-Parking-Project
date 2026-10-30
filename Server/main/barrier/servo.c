#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "servo.h"
#include <stdint.h>

#define SERVO_IN_GPIO 0
#define SERVO_OUT_GPIO 1
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_FREQUENCY_HZ 50
#define SERVO_PERIOD_US 20000
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2400

typedef struct
{
    ledc_channel_t channel;
    int gpio;
    bool is_out_gate;
    bool open;
    int64_t close_at_us; /* 0 = no automatic close pending */
    esp_timer_handle_t close_timer;
} gate_t;

static const char *TAG = "SERVO";
static gate_t gates[] = {
    [SERVO_GATE_IN] = {.channel = LEDC_CHANNEL_0, .gpio = SERVO_IN_GPIO, .is_out_gate = false},
    [SERVO_GATE_OUT] = {.channel = LEDC_CHANNEL_1, .gpio = SERVO_OUT_GPIO, .is_out_gate = true},
};
static bool fire_alarm_active;
static SemaphoreHandle_t servo_mutex;

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

/* Caller holds servo_mutex. */
static esp_err_t gate_set(gate_t *gate, bool open)
{
    if (gate->open == open)
        return ESP_OK;
    esp_err_t err = servo_set_angle(gate->channel, servo_angle_for_state(gate->is_out_gate, open));
    if (err == ESP_OK)
        gate->open = open;
    return err;
}

/* Caller holds servo_mutex. */
static void gate_cancel_auto_close(gate_t *gate)
{
    gate->close_at_us = 0;
    esp_timer_stop(gate->close_timer); /* ESP_ERR_INVALID_STATE when idle is fine */
}

/* Runs in the esp_timer task when a gate's open period ends. */
static void gate_close_timer_cb(void *arg)
{
    gate_t *gate = arg;
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    /* A command that raced with this callback may have cancelled or extended the deadline. */
    if (gate->close_at_us != 0 && esp_timer_get_time() >= gate->close_at_us)
    {
        gate->close_at_us = 0;
        esp_err_t err = gate_set(gate, false);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to auto-close %s barrier: %s",
                     gate->is_out_gate ? "exit" : "entry", esp_err_to_name(err));
    }
    xSemaphoreGive(servo_mutex);
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

    for (size_t i = 0; i < sizeof(gates) / sizeof(gates[0]); ++i)
    {
        gate_t *gate = &gates[i];
        const ledc_channel_config_t channel = {
            .gpio_num = gate->gpio, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = gate->channel,
            .intr_type = LEDC_INTR_DISABLE, .timer_sel = SERVO_TIMER, .duty = 0, .hpoint = 0,
        };
        err = ledc_channel_config(&channel);
        if (err != ESP_OK)
            return err;

        /* Both gates start closed. */
        gate->open = false;
        gate->close_at_us = 0;
        err = servo_set_angle(gate->channel, servo_angle_for_state(gate->is_out_gate, false));
        if (err != ESP_OK)
            return err;

        const esp_timer_create_args_t timer_args = {
            .callback = gate_close_timer_cb,
            .arg = gate,
            .name = gate->is_out_gate ? "gate_out_close" : "gate_in_close",
        };
        err = esp_timer_create(&timer_args, &gate->close_timer);
        if (err != ESP_OK)
            return err;
    }
    fire_alarm_active = false;
    return ESP_OK;
}

static esp_err_t servo_set_open(servo_gate_t which, bool open)
{
    if (servo_mutex == NULL)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!(fire_alarm_active && !open))
    {
        /* Manual commands override any pending automatic close. */
        gate_cancel_auto_close(&gates[which]);
        err = gate_set(&gates[which], open);
    }
    xSemaphoreGive(servo_mutex);
    return err;
}

esp_err_t servo_set_in_open(bool open)
{
    return servo_set_open(SERVO_GATE_IN, open);
}

esp_err_t servo_set_out_open(bool open)
{
    return servo_set_open(SERVO_GATE_OUT, open);
}

esp_err_t servo_set_all_open(bool open)
{
    esp_err_t in_err = servo_set_in_open(open);
    esp_err_t out_err = servo_set_out_open(open);
    return in_err != ESP_OK ? in_err : out_err;
}

esp_err_t servo_open_for(servo_gate_t which, uint32_t duration_ms)
{
    if (servo_mutex == NULL || duration_ms == 0 ||
        (which != SERVO_GATE_IN && which != SERVO_GATE_OUT))
        return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!fire_alarm_active)
    {
        gate_t *gate = &gates[which];
        err = gate_set(gate, true);
        if (err == ESP_OK)
        {
            const uint64_t duration_us = (uint64_t)duration_ms * 1000;
            esp_timer_stop(gate->close_timer);
            gate->close_at_us = esp_timer_get_time() + (int64_t)duration_us;
            err = esp_timer_start_once(gate->close_timer, duration_us);
        }
    }
    xSemaphoreGive(servo_mutex);
    return err;
}

esp_err_t servo_set_fire_alarm(bool active)
{
    if (servo_mutex == NULL)
        return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(servo_mutex, portMAX_DELAY);
    /* Called for every parking update; nothing to do while no fire is reported. */
    if (fire_alarm_active == active && !active)
    {
        xSemaphoreGive(servo_mutex);
        return ESP_OK;
    }

    fire_alarm_active = active;
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < sizeof(gates) / sizeof(gates[0]); ++i)
    {
        gate_cancel_auto_close(&gates[i]);
        esp_err_t gate_err = gate_set(&gates[i], active);
        if (err == ESP_OK)
            err = gate_err;
    }
    xSemaphoreGive(servo_mutex);
    return err;
}

/* Single-word reads are atomic on the ESP32-C3, so no lock is needed here. */
bool servo_is_in_open(void)
{
    return gates[SERVO_GATE_IN].open;
}

bool servo_is_out_open(void)
{
    return gates[SERVO_GATE_OUT].open;
}
