// Panel commands and pixel packing adapted from the supplied Waveshare
// 02_ESP-IDF/10_FactoryProgram/components/port_bsp/display_bsp.cpp.
#include "rlcd.h"

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"

#define FRAMEBUFFER_BYTES (BOARD_RLCD_WIDTH * BOARD_RLCD_HEIGHT / 8)

_Static_assert(BOARD_RLCD_WIDTH == 400 && BOARD_RLCD_HEIGHT == 300,
               "This pixel mapping requires 400 x 300 landscape orientation");

static spi_device_handle_t panel;
static uint8_t *framebuffer;

typedef struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[10];
    uint16_t delay_ms;
} panel_command_t;

// Preserve the LOCAL demo's values (not those of a newer online revision).
static const panel_command_t init_commands[] = {
    {0xD6, 2, {0x17, 0x02}, 0},
    {0xD1, 1, {0x01}, 0},
    {0xC0, 2, {0x11, 0x04}, 0},
    {0xC1, 4, {0x41, 0x41, 0x41, 0x41}, 0},
    {0xC2, 4, {0x19, 0x19, 0x19, 0x19}, 0},
    {0xC4, 4, {0x41, 0x41, 0x41, 0x41}, 0},
    {0xC5, 4, {0x19, 0x19, 0x19, 0x19}, 0},
    {0xD8, 2, {0xA6, 0xE9}, 0},
    {0xB2, 1, {0x05}, 0},
    {0xB3, 10, {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 0},
    {0xB4, 8, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 0},
    {0x62, 3, {0x32, 0x03, 0x1F}, 0},
    {0xB7, 1, {0x13}, 0},
    {0xB0, 1, {0x64}, 0},
    {0x11, 0, {0}, 200},
    {0xC9, 1, {0x00}, 0},
    {0x36, 1, {0x48}, 0},
    {0x3A, 1, {0x11}, 0},
    {0xB9, 1, {0x20}, 0},
    {0xB8, 1, {0x29}, 0},
    {0x21, 0, {0}, 0},
    {0x2A, 2, {0x12, 0x2A}, 0},
    {0x2B, 2, {0x00, 0xC7}, 0},
    {0x35, 1, {0x00}, 0},
    {0xD0, 1, {0xFF}, 0},
    {0x38, 0, {0}, 0},
    {0x29, 0, {0}, 0},
};

static esp_err_t transfer(int data_mode, const uint8_t *data, size_t length)
{
    esp_err_t err = gpio_set_level(BOARD_RLCD_DC_GPIO, data_mode);
    if (err != ESP_OK) {
        return err;
    }
    spi_transaction_t transaction = {.length = length * 8};
    if (length <= sizeof(transaction.tx_data)) {
        transaction.flags = SPI_TRANS_USE_TXDATA;
        memcpy(transaction.tx_data, data, length);
    } else {
        transaction.tx_buffer = data;
    }
    return spi_device_polling_transmit(panel, &transaction);
}

static esp_err_t command(uint8_t cmd, const uint8_t *data, size_t length)
{
    esp_err_t err = transfer(0, &cmd, 1);
    if (err != ESP_OK || length == 0) {
        return err;
    }
    return transfer(1, data, length);
}

esp_err_t rlcd_init(void)
{
    if (panel != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    framebuffer = heap_caps_malloc(FRAMEBUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (framebuffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const gpio_config_t pin_config = {
        .pin_bit_mask = (1ULL << BOARD_RLCD_DC_GPIO) | (1ULL << BOARD_RLCD_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&pin_config);
    if (err != ESP_OK) {
        goto free_buffer;
    }
    const spi_bus_config_t bus_config = {
        .mosi_io_num = BOARD_RLCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_RLCD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FRAMEBUFFER_BYTES,
    };
    err = spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        goto free_buffer;
    }
    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = BOARD_RLCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = BOARD_RLCD_CS_GPIO,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI3_HOST, &device_config, &panel);
    if (err != ESP_OK) {
        goto free_bus;
    }

    gpio_set_level(BOARD_RLCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50) + 1);
    gpio_set_level(BOARD_RLCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20) + 1);
    gpio_set_level(BOARD_RLCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50) + 1);

    for (size_t i = 0; i < sizeof(init_commands) / sizeof(init_commands[0]); ++i) {
        const panel_command_t *entry = &init_commands[i];
        err = command(entry->command, entry->data, entry->length);
        if (err != ESP_OK) {
            goto free_device;
        }
        if (entry->delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(entry->delay_ms) + 1);
        }
    }
    rlcd_clear();
    return ESP_OK;

free_device:
    spi_bus_remove_device(panel);
    panel = NULL;
free_bus:
    spi_bus_free(SPI3_HOST);
free_buffer:
    heap_caps_free(framebuffer);
    framebuffer = NULL;
    return err;
}

void rlcd_clear(void)
{
    if (framebuffer != NULL) {
        memset(framebuffer, 0xff, FRAMEBUFFER_BYTES);
    }
}

static void black_pixel(int x, int y)
{
    if (framebuffer == NULL || x < 0 || y < 0
        || (unsigned)x >= BOARD_RLCD_WIDTH || (unsigned)y >= BOARD_RLCD_HEIGHT) {
        return;
    }
    // Demo landscape packing: 2 columns x 4 inverted rows per byte.
    const unsigned inverted_y = BOARD_RLCD_HEIGHT - 1 - y;
    const size_t index = (unsigned)x / 2 * (BOARD_RLCD_HEIGHT / 4) + inverted_y / 4;
    const unsigned bit = 7 - ((inverted_y % 4) * 2 + (unsigned)x % 2);
    framebuffer[index] &= (uint8_t)~(1U << bit);
}

// Small built-in 5 x 7 uppercase font; no external graphics library required.
typedef struct {
    char character;
    uint8_t rows[7];
} glyph_t;

static const glyph_t glyphs[] = {
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'J', {7, 2, 2, 2, 2, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'.', {0, 0, 0, 0, 0, 6, 6}},
    {':', {0, 6, 6, 0, 6, 6, 0}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'%', {25, 25, 2, 4, 8, 19, 19}},
};

void rlcd_text(int x, int y, const char *text, unsigned scale)
{
    if (text == NULL || scale == 0 || scale > 8) {
        return;
    }
    for (; *text != '\0'; ++text, x += 6 * (int)scale) {
        const glyph_t *glyph = NULL;
        for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
            if (glyphs[i].character == *text) {
                glyph = &glyphs[i];
                break;
            }
        }
        if (glyph == NULL) {
            continue;  // Space or unsupported character.
        }
        for (unsigned row = 0; row < 7; ++row) {
            for (unsigned col = 0; col < 5; ++col) {
                if ((glyph->rows[row] & (1U << (4 - col))) == 0) {
                    continue;
                }
                for (unsigned dy = 0; dy < scale; ++dy) {
                    for (unsigned dx = 0; dx < scale; ++dx) {
                        black_pixel(x + (int)(col * scale + dx), y + (int)(row * scale + dy));
                    }
                }
            }
        }
    }
}

void rlcd_hline(int x, int y, int width)
{
    for (int i = 0; i < width; ++i) {
        black_pixel(x + i, y);
    }
}

void rlcd_rect(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    rlcd_hline(x, y, width);
    rlcd_hline(x, y + height - 1, width);
    for (int row = 1; row < height - 1; ++row) {
        black_pixel(x, y + row);
        black_pixel(x + width - 1, y + row);
    }
}

esp_err_t rlcd_flush(void)
{
    if (panel == NULL || framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t columns[] = {0x12, 0x2A};
    const uint8_t pages[] = {0x00, 0xC7};
    esp_err_t err = command(0x2A, columns, sizeof(columns));
    if (err == ESP_OK) {
        err = command(0x2B, pages, sizeof(pages));
    }
    if (err == ESP_OK) {
        err = command(0x2C, NULL, 0);
    }
    if (err == ESP_OK) {
        err = transfer(1, framebuffer, FRAMEBUFFER_BYTES);
    }
    return err;
}
