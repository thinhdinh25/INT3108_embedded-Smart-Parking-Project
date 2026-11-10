#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gate_hold.h"
#include "sdkconfig.h"
#include "servo.h"
#include <stdint.h>

/* The display would drive this pin against the sensor's output. */
#if CONFIG_PARKING_LCD_BL_GPIO == CONFIG_PARKING_GATE_IN_IR_GPIO || \
    CONFIG_PARKING_LCD_BL_GPIO == CONFIG_PARKING_GATE_OUT_IR_GPIO
#error "The TFT backlight and a gate IR sensor are on the same GPIO"
#endif

#define SERVO_IN_GPIO 0
#define SERVO_OUT_GPIO 1
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_FREQUENCY_HZ 50
#define SERVO_PERIOD_US 20000
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2400
#define GATE_POLL_US 50000 /* how often a barrier due to close re-checks its IR beam */

typedef struct
{
    ledc_channel_t channel;
    int gpio;
    int ir_gpio;
    bool is_out_gate;
    bool open;
    int64_t close_at_us; /* 0 = no automatic close pending */
    esp_timer_handle_t close_timer;
    gate_hold_t hold;
    bool hold_logged;
} gate_t;

static const char *TAG = "SERVO";
static gate_t gates[] = {
    [SERVO_GATE_IN] = {.channel = LEDC_CHANNEL_0, .gpio = SERVO_IN_GPIO,
                       .ir_gpio = CONFIG_PARKING_GATE_IN_IR_GPIO, .is_out_gate = false},
    [SERVO_GATE_OUT] = {.channel = LEDC_CHANNEL_1, .gpio = SERVO_OUT_GPIO,
                        .ir_gpio = CONFIG_PARKING_GATE_OUT_IR_GPIO, .is_out_gate = true},
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

/* IR modules are active-low: LOW = something in the beam. */
static bool gate_car_present(const gate_t *gate)
{
    return gpio_get_level(gate->ir_gpio) == 0;
}

/* Caller holds servo_mutex. */
static void gate_cancel_auto_close(gate_t *gate)
{
    gate->close_at_us = 0;
    gate_hold_reset(&gate->hold);
    gate->hold_logged = false;
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
        const bool car = gate_car_present(gate);
        esp_err_t err;
        if (gate_hold_may_close(&gate->hold, car, esp_timer_get_time()))
        {
            gate->close_at_us = 0;
            gate->hold_logged = false;
            err = gate_set(gate, false);
        }
        else
        {
            if (car && !gate->hold_logged)
            {
                gate->hold_logged = true;
                ESP_LOGI(TAG, "Car in %s gate, holding the barrier open",
                         gate->is_out_gate ? "exit" : "entry");
            }
            err = esp_timer_start_once(gate->close_timer, GATE_POLL_US);
        }
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
        const gpio_config_t ir = {
            .pin_bit_mask = 1ULL << gate->ir_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&ir);
        if (err != ESP_OK)
            return err;

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
    if (!open && gates[which].open && gate_car_present(&gates[which]))
    {
        /* Even the operator cannot bring a barrier down on a car. */
        err = ESP_ERR_NOT_ALLOWED;
    }
    else if (!(fire_alarm_active && !open))
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
            gate_hold_reset(&gate->hold);
            gate->hold_logged = false;
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
        gate_t *gate = &gates[i];
        gate_cancel_auto_close(gate);
        esp_err_t gate_err;
        if (active)
            gate_err = gate_set(gate, true);
        else
        {
            /* Fire over: people may still be leaving, so close through the IR check. */
            gate->close_at_us = esp_timer_get_time();
            gate_err = esp_timer_start_once(gate->close_timer, GATE_POLL_US);
        }
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
