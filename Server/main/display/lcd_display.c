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
#include "../parking/rfid_session.h"
#include "lcd_display.h"
#include "kaoruko_gif.h"
#include "claude_gif.h"

#define LCD_SPI_HOST SPI2_HOST
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define PIXEL_BYTES 2
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

/* Kaoruko picture-in-picture area below the header on the right. */
#define GIF_X 144
#define GIF_Y 40
#define GIF_WIDTH KAORUKO_WIDTH
#define GIF_HEIGHT KAORUKO_HEIGHT
#define GIF_PIXEL_COUNT (GIF_WIDTH * GIF_HEIGHT)
#define GIF_BUFFERS 2
#define CLAUDE_Y 194

static const char *TAG = "LCD_DISPLAY";
static esp_lcd_panel_handle_t panel;
static uint16_t *framebuffer;
static uint16_t *gif_framebuffer[GIF_BUFFERS];
static uint8_t gif_background_mask[(GIF_PIXEL_COUNT + 7) / 8];
static int gif_buffer_index = 0;
static SemaphoreHandle_t lcd_mutex;
static const int width = LCD_WIDTH;
static const int height = LCD_HEIGHT;

static const uint16_t COLOR_BG = 0x0000;
static const uint16_t COLOR_PANEL = 0x0841;
static const uint16_t COLOR_LINE = 0x4208;
static const uint16_t COLOR_WHITE = 0xFFFF;
static const uint16_t COLOR_MUTED = 0xD69A;
static const uint16_t COLOR_TEAL = 0x03D9;
static const uint16_t COLOR_GREEN = 0x07E0;
static const uint16_t COLOR_RED = 0xF800;
static const uint16_t COLOR_DARK_RED = 0x4000;
static const uint16_t COLOR_ORANGE = 0xFD20;
static const uint16_t COLOR_YELLOW = 0xFFE0;

static uint16_t rgb565_byte_swap(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

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
        framebuffer[y * width + x] = rgb565_byte_swap(color);
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

static bool is_near_white(uint16_t color)
{
    return ((color >> 11) & 0x1f) >= 29 &&
           ((color >> 5) & 0x3f) >= 60 &&
           (color & 0x1f) >= 29;
}

static bool is_background_pixel_marked(size_t index)
{
    return (gif_background_mask[index / 8] & (1U << (index % 8))) != 0;
}

static void mark_background_pixel(size_t index)
{
    gif_background_mask[index / 8] |= (uint8_t)(1U << (index % 8));
}

/* Remove the white matte around Kaoruko while preserving enclosed white details. */
static void remove_kaoruko_white_background(uint16_t *pixels)
{
    memset(gif_background_mask, 0, sizeof(gif_background_mask));

    for (int x = 0; x < GIF_WIDTH; ++x)
    {
        const size_t top = (size_t)x;
        const size_t bottom = (GIF_HEIGHT - 1) * GIF_WIDTH + x;
        if (is_near_white(pixels[top])) mark_background_pixel(top);
        if (is_near_white(pixels[bottom])) mark_background_pixel(bottom);
    }
    for (int y = 1; y < GIF_HEIGHT - 1; ++y)
    {
        const size_t left = (size_t)y * GIF_WIDTH;
        const size_t right = left + GIF_WIDTH - 1;
        if (is_near_white(pixels[left])) mark_background_pixel(left);
        if (is_near_white(pixels[right])) mark_background_pixel(right);
    }

    /* Propagate edge-connected white pixels using a small bitset, not a large queue. */
    bool changed;
    do
    {
        changed = false;
        for (int y = 1; y < GIF_HEIGHT - 1; ++y)
        {
            for (int x = 1; x < GIF_WIDTH - 1; ++x)
            {
                const size_t index = (size_t)y * GIF_WIDTH + x;
                if (!is_background_pixel_marked(index) && is_near_white(pixels[index]) &&
                    (is_background_pixel_marked(index - 1) ||
                     is_background_pixel_marked(index + 1) ||
                     is_background_pixel_marked(index - GIF_WIDTH) ||
                     is_background_pixel_marked(index + GIF_WIDTH)))
                {
                    mark_background_pixel(index);
                    changed = true;
                }
            }
        }
    } while (changed);

    for (size_t index = 0; index < GIF_PIXEL_COUNT; ++index)
        if (is_background_pixel_marked(index))
            pixels[index] = COLOR_BG;
}

static void draw_parking_screen(const parking_state_t *state, int free_count,
                                const rfid_receipt_t *receipt)
{
    fill_rect(0, 0, width, height, COLOR_BG);
    fill_rect(0, 0, width, 4, COLOR_TEAL);

    draw_text(12, 11, "PARKSENSE", 2, COLOR_WHITE);
    fill_rect(8, 32, width - 16, 1, COLOR_LINE);

    /* Main availability indicator. */
    draw_text(10, 41, "FREE SPACES", 1, COLOR_MUTED);
    char free_text[8];
    snprintf(free_text, sizeof(free_text), "%d/5", free_count);
    draw_text(10, 54, free_text, 3, COLOR_WHITE);

    /* Sensor values. */
    fill_rect(8, 87, 60, 40, COLOR_PANEL);
    fill_rect(75, 87, 60, 40, COLOR_PANEL);
    draw_text(12, 93, "TEMP C", 1, COLOR_MUTED);
    draw_text(78, 93, "WATER MM", 1, COLOR_MUTED);

    char temperature[12] = "--.-";
    char water[12] = "--.-";
    if (state->temperature_received)
        format_tenths(temperature, sizeof(temperature), state->temperature_c);
    if (state->received)
        format_tenths(water, sizeof(water), state->water_mm);
    draw_text(12, 108, temperature, 1, COLOR_WHITE);
    draw_text(79, 108, water, 1, COLOR_WHITE);

    /* Larger slot indicators span the full display width below the sensors. */
    draw_text(8, 134, "SPACE STATUS", 1, COLOR_MUTED);
    for (int i = 0; i < PARKING_SLOT_COUNT; ++i)
    {
        int x = 8 + i * 46;
        bool occupied = state->slots[i] != 0;

        fill_rect(x, 147, 43, 43, COLOR_PANEL);
        fill_rect(x, 147, 3, 43, occupied ? COLOR_RED : COLOR_GREEN);

        char slot_label[3] = {'S', (char)('1' + i), '\0'};
        draw_text(x + 4, 152, slot_label, 1, COLOR_MUTED);
        draw_text(x + 4, 172, occupied ? "BUSY" : "FREE", 1,
                  occupied ? COLOR_RED : COLOR_GREEN);
    }

    fill_rect(0, 194, width, 46, COLOR_PANEL);
    if (receipt->valid)
    {
        char line[RFID_CARD_ID_MAX + 5];
        char duration[16], fee[16];
        uint64_t total_minutes = ((uint64_t)receipt->duration_seconds + 59U) / 60U;
        uint64_t hours = total_minutes / 60U;
        uint64_t minutes = total_minutes % 60U;
        snprintf(line, sizeof(line), "ID %s", receipt->card_id);
        draw_text(8, 198, line, 1, COLOR_WHITE);
        draw_text(8, 211, "PARK TIME", 1, COLOR_MUTED);
        snprintf(duration, sizeof(duration), "%lluH %02lluM",
                 (unsigned long long)hours, (unsigned long long)minutes);
        int duration_scale = strlen(duration) <= 8 ? 2 : 1;
        draw_text(8, 223, duration, duration_scale, COLOR_WHITE);

        draw_text(126, 198, "FEE VND", 1, COLOR_MUTED);
        snprintf(fee, sizeof(fee), "%u", (unsigned)receipt->fee_vnd);
        int fee_scale = strlen(fee) <= 5 ? 3 : (strlen(fee) <= 8 ? 2 : 1);
        draw_text(126, fee_scale == 3 ? 209 : 216, fee, fee_scale, COLOR_TEAL);
    }

    ESP_ERROR_CHECK(lcd_draw_bitmap_locked(0, 0, width, height, framebuffer));
}

static void draw_flame(int center_x, int bottom_y, int frame, int scale)
{
    static const uint8_t widths[] = {7, 12, 15, 16, 14, 10, 7, 4};
    int sway = ((frame % 5) - 2) * scale;
    int flicker = (frame % 3) * scale;

    for (int row = 0; row < (int)(sizeof(widths) / sizeof(widths[0])); ++row)
    {
        int w = widths[row] * scale;
        int y = bottom_y - (row + 1) * 5 * scale + flicker;
        int x = center_x + sway - w / 2;
        fill_rect(x, y, w, 5 * scale, row < 3 ? COLOR_RED : COLOR_ORANGE);

        if (row >= 1 && row <= 5)
        {
            int inner_w = w / 2;
            fill_rect(center_x + sway / 2 - inner_w / 2,
                      y + 2 * scale, inner_w, 3 * scale,
                      row < 3 ? COLOR_ORANGE : COLOR_YELLOW);
        }
    }
}

static void draw_fire_alarm_screen(int frame)
{
    fill_rect(0, 0, width, height, COLOR_BG);
    fill_rect(0, 0, width, 8, COLOR_RED);
    fill_rect(0, height - 8, width, 8, COLOR_RED);
    fill_rect(8, 8, 4, height - 16, COLOR_RED);
    fill_rect(width - 12, 8, 4, height - 16, COLOR_RED);
    fill_rect(16, 16, width - 32, height - 32, COLOR_DARK_RED);
    draw_text(58, 25, "DANGER", 3, COLOR_WHITE);
    draw_flame(120 - ((frame % 5) - 2) * 3, 153, frame, 2);
    draw_flame(66 + ((frame % 5) - 2) * 2, 148, frame + 2, 1);
    draw_flame(174 - ((frame % 5) - 2) * 2, 148, frame + 3, 1);
    fill_rect(27, 162, width - 54, 2, COLOR_RED);
    draw_text(28, 176, "FIRE DETECTED", 2, COLOR_YELLOW);
    draw_text(20, 204, "EVACUATE NOW", 2, COLOR_WHITE);
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
    remove_kaoruko_white_background(dst);
    for (size_t i = 0; i < (size_t)GIF_WIDTH * GIF_HEIGHT; ++i)
        dst[i] = rgb565_byte_swap(dst[i]);

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

static void draw_claude_frame(int frame_index, int x, int previous_x,
                              bool clear_previous)
{
    if (frame_index < 0 || frame_index >= CLAUDE_FRAME_COUNT)
        return;

    if (clear_previous && previous_x != x)
    {
        uint16_t *background = gif_framebuffer[gif_buffer_index];
        for (size_t i = 0; i < (size_t)CLAUDE_WIDTH * CLAUDE_HEIGHT; ++i)
            background[i] = rgb565_byte_swap(COLOR_PANEL);

        esp_err_t clear_err = lcd_draw_bitmap_locked(
            previous_x,
            CLAUDE_Y,
            previous_x + CLAUDE_WIDTH,
            CLAUDE_Y + CLAUDE_HEIGHT,
            background);
        if (clear_err != ESP_OK)
            ESP_LOGE(TAG, "Claude clear failed: %s", esp_err_to_name(clear_err));
        gif_buffer_index ^= 1;
    }

    /* The decoder pre-fills the frame with the footer panel color. */
    uint16_t *dst = gif_framebuffer[gif_buffer_index];
    if (!claude_decode_frame(frame_index, dst,
                             (size_t)CLAUDE_WIDTH * CLAUDE_HEIGHT))
    {
        ESP_LOGE(TAG, "Claude frame %d decode failed", frame_index);
        return;
    }
    for (size_t i = 0; i < (size_t)CLAUDE_WIDTH * CLAUDE_HEIGHT; ++i)
        dst[i] = rgb565_byte_swap(dst[i]);

    esp_err_t err = lcd_draw_bitmap_locked(
        x,
        CLAUDE_Y,
        x + CLAUDE_WIDTH,
        CLAUDE_Y + CLAUDE_HEIGHT,
        dst);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Claude frame %d draw failed: %s",
                 frame_index, esp_err_to_name(err));

    gif_buffer_index ^= 1;
}

static void animation_task(void *arg)
{
    (void)arg;
    parking_state_t state = {0};
    int free_count = 0;
    rfid_receipt_t receipt = {0};
    const int min_claude_x = 8;
    const int max_claude_x = LCD_WIDTH - CLAUDE_WIDTH - 8;
    int claude_x = min_claude_x;
    int last_claude_x = min_claude_x;
    int claude_direction = 1;
    bool claude_visible = false;
    bool claude_frame_drawn = false;

    for (int frame = 0, claude_frame = 0;;)
    {
        parking_state_get(&state, &free_count);
        rfid_session_get_receipt(&receipt);
        if (!state.fire)
        {
            draw_kaoruko_frame(frame);
            if (!receipt.valid)
            {
                if (!claude_visible)
                {
                    claude_x = min_claude_x;
                    claude_direction = 1;
                    claude_visible = true;
                }

                claude_x += 2 * claude_direction;
                if (claude_x >= max_claude_x)
                {
                    claude_x = max_claude_x;
                    claude_direction = -1;
                }
                else if (claude_x <= min_claude_x)
                {
                    claude_x = min_claude_x;
                    claude_direction = 1;
                }

                draw_claude_frame(claude_frame, claude_x, last_claude_x,
                                  claude_frame_drawn);
                last_claude_x = claude_x;
                claude_frame_drawn = true;
            }
            else
            {
                claude_visible = false;
                claude_frame_drawn = false;
            }
        }
        else
        {
            claude_visible = false;
            claude_frame_drawn = false;
        }
        uint16_t frame_delay = (state.fire || receipt.valid)
                                   ? kaoruko_frame_delay_ms[frame]
                                   : claude_frame_delay_ms[claude_frame] / 2U;
        vTaskDelay(pdMS_TO_TICKS(frame_delay));
        frame = (frame + 1) % KAORUKO_FRAME_COUNT;
        claude_frame = (claude_frame + 1) % CLAUDE_FRAME_COUNT;
    }
}

static void display_task(void *arg)
{
    (void)arg;
    parking_state_t state = {0};
    int free_count = 0;
    bool first_draw = true;
    bool last_received = false;
    bool last_fire = false;
    int fire_frame = 0;
    rfid_receipt_t receipt = {0};
    rfid_receipt_t last_receipt = {0};
    int last_slots[PARKING_SLOT_COUNT] = {-1, -1, -1, -1, -1};
    float last_temperature = -1000.0f;
    float last_water = -1000.0f;

    for (;;)
    {
        parking_state_get(&state, &free_count);
        rfid_session_get_receipt(&receipt);

        if (state.fire || first_draw ||
            state.received != last_received ||
            state.fire != last_fire ||
            memcmp(state.slots, last_slots, sizeof(last_slots)) != 0 ||
            state.temperature_c != last_temperature ||
            state.water_mm != last_water ||
            memcmp(&receipt, &last_receipt, sizeof(receipt)) != 0)
        {
            if (state.received && state.fire)
            {
                draw_fire_alarm_screen(fire_frame);
                fire_frame = (fire_frame + 1) % 15;
            }
            else
                draw_parking_screen(&state, free_count, &receipt);

            memcpy(last_slots, state.slots, sizeof(last_slots));
            last_received = state.received;
            last_fire = state.fire;
            last_temperature = state.temperature_c;
            last_water = state.water_mm;
            memcpy(&last_receipt, &receipt, sizeof(receipt));
            first_draw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(state.fire ? 120 : 500));
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
