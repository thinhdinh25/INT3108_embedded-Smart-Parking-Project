#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "../parking/parking_state.h"
#include "lcd_display.h"
#include "kaoruko_gif.h"

#define LCD_SPI_HOST SPI2_HOST
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define PIXEL_BYTES 2
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

/* Kaoruko picture-in-picture area.
 * 144 + 96 = 240, so it fits exactly on the right side. */
#define GIF_X 144
#define GIF_Y 100
#define GIF_WIDTH KAORUKO_WIDTH
#define GIF_HEIGHT KAORUKO_HEIGHT
#define GIF_BUFFERS 2

static const char *TAG = "LCD_DISPLAY";
static esp_lcd_panel_handle_t panel;
static uint16_t *framebuffer;
static uint16_t *gif_framebuffer[GIF_BUFFERS];
static int gif_buffer_index = 0;
static SemaphoreHandle_t lcd_mutex;
static const int width = LCD_WIDTH;
static const int height = LCD_HEIGHT;

static const uint16_t COLOR_BG = 0x0843;
static const uint16_t COLOR_PANEL = 0x10A5;
static const uint16_t COLOR_LINE = 0x216B;
static const uint16_t COLOR_WHITE = 0xFFFF;
static const uint16_t COLOR_MUTED = 0x9D7B;
static const uint16_t COLOR_TEAL = 0x4F5A;
static const uint16_t COLOR_GREEN = 0x5EAA;
static const uint16_t COLOR_AMBER = 0xFD20;
static const uint16_t COLOR_RED = 0xF986;

/* Five vertical 5x7 columns for A-Z and 0-9. */
static const uint8_t glyphs[36][5] = {
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
};

static void pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && x < width && y >= 0 && y < height)
        framebuffer[y * width + x] = color;
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            pixel(col, row, color);
}

static void draw_char(int x, int y, char c, int scale, uint16_t color)
{
    const uint8_t *glyph = NULL;
    if (c >= 'A' && c <= 'Z') glyph = glyphs[(int)(c - 'A')];
    else if (c >= '0' && c <= '9') glyph = glyphs[26 + (int)(c - '0')];

    if (glyph != NULL)
    {
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (glyph[col] & (1U << row))
                    fill_rect(x + col * scale, y + row * scale, scale, scale, color);
    }
    else if (c == '/')
    {
        for (int i = 0; i < 7; ++i)
            fill_rect(x + (6 - i) * scale, y + i * scale, scale, scale, color);
    }
    else if (c == '.')
    {
        fill_rect(x + 2 * scale, y + 6 * scale, scale, scale, color);
    }
    else if (c == ':')
    {
        fill_rect(x + 2 * scale, y + 2 * scale, scale, scale, color);
        fill_rect(x + 2 * scale, y + 5 * scale, scale, scale, color);
    }
    else if (c == '-')
    {
        fill_rect(x + scale, y + 3 * scale, 3 * scale, scale, color);
    }
}

static void draw_text(int x, int y, const char *text, int scale, uint16_t color)
{
    for (size_t i = 0; text[i] != '\0'; ++i)
        draw_char(x + (int)i * 6 * scale, y, text[i], scale, color);
}

static void format_tenths(char *out, size_t out_size, float value)
{
    int tenths = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    int magnitude = abs(tenths);
    snprintf(out, out_size, "%s%d.%d", tenths < 0 ? "-" : "",
             magnitude / 10, magnitude % 10);
}

static esp_err_t lcd_draw_bitmap_locked(int x_start, int y_start,
                                        int x_end, int y_end,
                                        const void *pixels)
{
    if (xSemaphoreTake(lcd_mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    esp_err_t err = esp_lcd_panel_draw_bitmap(
        panel, x_start, y_start, x_end, y_end, pixels);

    xSemaphoreGive(lcd_mutex);
    return err;
}

static void draw_parking_screen(const parking_state_t *state, int free_count)
{
    fill_rect(0, 0, width, height, COLOR_BG);
    fill_rect(0, 0, width, 4, COLOR_TEAL);

    draw_text(12, 11, "PARKSENSE", 2, COLOR_WHITE);
    fill_rect(8, 32, width - 16, 1, COLOR_LINE);

    /* Main availability indicator. */
    draw_text(10, 41, "FREE SPACES", 1, COLOR_MUTED);
    char free_text[8];
    snprintf(free_text, sizeof(free_text), "%d/5", free_count);
    draw_text(10, 54, free_text, 3,
              free_count > 0 ? COLOR_GREEN : COLOR_RED);

    /* Network state. */
    fill_rect(150, 40, 78, 40, COLOR_PANEL);
    draw_text(158, 47, state->received ? "LIVE" : "WAIT", 1,
              state->received ? COLOR_GREEN : COLOR_AMBER);
    draw_text(158, 62, "UDP", 2, COLOR_WHITE);

    /* Sensor values. */
    fill_rect(8, 87, 60, 40, COLOR_PANEL);
    fill_rect(75, 87, 60, 40, COLOR_PANEL);
    draw_text(12, 93, "TEMP C", 1, COLOR_MUTED);
    draw_text(78, 93, "WATER MM", 1, COLOR_MUTED);

    char temperature[12] = "--.-";
    char water[12] = "--.-";
    if (state->received)
    {
        format_tenths(temperature, sizeof(temperature), state->temperature_c);
        format_tenths(water, sizeof(water), state->water_mm);
    }
    draw_text(12, 108, temperature, 1, COLOR_WHITE);
    draw_text(79, 108, water, 1, COLOR_WHITE);

    /* Five slot indicators fit in the left side, leaving the right side for animation. */
    draw_text(8, 134, "SPACE STATUS", 1, COLOR_MUTED);
    for (int i = 0; i < PARKING_SLOT_COUNT; ++i)
    {
        int x = 8 + i * 27;
        bool occupied = state->slots[i] != 0;

        fill_rect(x, 145, 25, 31, COLOR_PANEL);

        char slot_label[3] = {'S', (char)('1' + i), '\0'};
        draw_text(x + 1, 149, slot_label, 1, COLOR_MUTED);
        draw_text(x, 163, occupied ? "BUSY" : "FREE", 1,
                  occupied ? COLOR_AMBER : COLOR_GREEN);
    }

    /* Footer. The 96x96 animated area is drawn separately by animation_task(). */
    draw_text(8, 186, state->received ? "PARKING LIVE" : "WAITING UDP", 1,
              state->received ? COLOR_GREEN : COLOR_AMBER);

    ESP_ERROR_CHECK(lcd_draw_bitmap_locked(0, 0, width, height, framebuffer));
}

static void draw_kaoruko_frame(int frame_index)
{
    if (frame_index < 0 || frame_index >= KAORUKO_FRAME_COUNT)
        return;

    /* Two DMA buffers avoid modifying a buffer that may still be in the SPI DMA queue. */
    uint16_t *dst = gif_framebuffer[gif_buffer_index];
    if (!kaoruko_decode_frame(frame_index, dst,
                              (size_t)GIF_WIDTH * GIF_HEIGHT))
    {
        ESP_LOGE(TAG, "Kaoruko frame %d decode failed", frame_index);
        return;
    }

    esp_err_t err = lcd_draw_bitmap_locked(
        GIF_X,
        GIF_Y,
        GIF_X + GIF_WIDTH,
        GIF_Y + GIF_HEIGHT,
        dst);

    if (err != ESP_OK)
        ESP_LOGE(TAG, "Kaoruko frame %d draw failed: %s",
                 frame_index, esp_err_to_name(err));

    gif_buffer_index ^= 1;
}

static void animation_task(void *arg)
{
    (void)arg;

    for (int frame = 0;; frame = (frame + 1) % KAORUKO_FRAME_COUNT)
    {
        draw_kaoruko_frame(frame);
        vTaskDelay(pdMS_TO_TICKS(kaoruko_frame_delay_ms[frame]));
    }
}

static void display_task(void *arg)
{
    (void)arg;
    parking_state_t state = {0};
    int free_count = 0;
    bool first_draw = true;
    bool last_received = false;
    int last_slots[PARKING_SLOT_COUNT] = {-1, -1, -1, -1, -1};
    float last_temperature = -1000.0f;
    float last_water = -1000.0f;

    for (;;)
    {
        parking_state_get(&state, &free_count);

        if (first_draw ||
            state.received != last_received ||
            memcmp(state.slots, last_slots, sizeof(last_slots)) != 0 ||
            state.temperature_c != last_temperature ||
            state.water_mm != last_water)
        {
            draw_parking_screen(&state, free_count);

            memcpy(last_slots, state.slots, sizeof(last_slots));
            last_received = state.received;
            last_temperature = state.temperature_c;
            last_water = state.water_mm;
            first_draw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t lcd_display_start(void)
{
    if (CONFIG_PARKING_LCD_BL_GPIO >= 0)
    {
        gpio_config_t backlight = {
            .pin_bit_mask = 1ULL << CONFIG_PARKING_LCD_BL_GPIO,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG,
                            "Configure backlight GPIO failed");
        gpio_set_level(CONFIG_PARKING_LCD_BL_GPIO, 0);
    }

    spi_bus_config_t bus_config = {
        .sclk_io_num = CONFIG_PARKING_LCD_SCLK_GPIO,
        .mosi_io_num = CONFIG_PARKING_LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = width * height * PIXEL_BYTES,
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO),
        TAG, "Initialize SPI bus failed");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = CONFIG_PARKING_LCD_DC_GPIO,
        .cs_gpio_num = CONFIG_PARKING_LCD_CS_GPIO,
        .pclk_hz = LCD_SPI_CLOCK_HZ,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
            &io_config,
            &io),
        TAG, "Create LCD SPI interface failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_PARKING_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(io, &panel_config, &panel),
        TAG, "Create ST7789 panel failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_reset(panel), TAG, "Reset display failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(panel), TAG, "Initialize display failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_set_gap(panel,
                              CONFIG_PARKING_LCD_X_GAP,
                              CONFIG_PARKING_LCD_Y_GAP),
        TAG, "Set display offset failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_invert_color(panel, true),
        TAG, "Set display colors failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_disp_on_off(panel, true),
        TAG, "Turn display on failed");

    framebuffer = heap_caps_calloc(
        (size_t)width * height,
        sizeof(uint16_t),
        MALLOC_CAP_DMA);
    if (framebuffer == NULL)
    {
        ESP_LOGE(TAG, "Unable to allocate LCD framebuffer");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < GIF_BUFFERS; ++i)
    {
        gif_framebuffer[i] = heap_caps_malloc(
            (size_t)GIF_WIDTH * GIF_HEIGHT * sizeof(uint16_t),
            MALLOC_CAP_DMA);

        if (gif_framebuffer[i] == NULL)
        {
            ESP_LOGE(TAG, "Unable to allocate GIF DMA framebuffer %d", i);
            return ESP_ERR_NO_MEM;
        }
    }

    lcd_mutex = xSemaphoreCreateMutex();
    if (lcd_mutex == NULL)
    {
        ESP_LOGE(TAG, "Unable to create LCD mutex");
        return ESP_ERR_NO_MEM;
    }

    if (CONFIG_PARKING_LCD_BL_GPIO >= 0)
        gpio_set_level(CONFIG_PARKING_LCD_BL_GPIO, 1);

    if (xTaskCreate(display_task, "lcd_display", 4096, NULL, 4, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Unable to create display task");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(animation_task, "lcd_animation", 3072, NULL, 3, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Unable to create animation task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ST7789 display started at %dx%d with %d-frame Kaoruko animation",
             width, height, KAORUKO_FRAME_COUNT);
    return ESP_OK;
}
