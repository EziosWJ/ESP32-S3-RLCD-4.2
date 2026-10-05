// Protocol reference: supplied Waveshare 10_FactoryProgram/i2c_equipment.*.
#include "shtc3.h"

#include <stdint.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"

#define SHTC3_WAKEUP 0x3517
#define SHTC3_MEASURE 0x7866  // Temperature first, normal mode, no clock stretching.
#define SHTC3_SLEEP 0xB098
#define I2C_TIMEOUT_MS 100

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t sensor;

static esp_err_t send_command(uint16_t command)
{
    const uint8_t data[] = {(uint8_t)(command >> 8), (uint8_t)command};
    return i2c_master_transmit(sensor, data, sizeof(data), I2C_TIMEOUT_MS);
}

static uint8_t crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t shtc3_init(void)
{
    if (sensor != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_SHTC3_ADDRESS,
        .scl_speed_hz = BOARD_I2C_HZ,
    };
    err = i2c_master_bus_add_device(bus, &device_config, &sensor);
    if (err != ESP_OK) {
        i2c_del_master_bus(bus);
        bus = NULL;
    }
    return err;
}

esp_err_t shtc3_read(float *temperature_c, float *humidity_percent)
{
    if (temperature_c == NULL || humidity_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = send_command(SHTC3_WAKEUP);
    if (err != ESP_OK) {
        return err;
    }
    // At least one full scheduler tick, including at the default 100 Hz tick rate.
    vTaskDelay(pdMS_TO_TICKS(1) + 1);

    uint8_t data[6];
    err = send_command(SHTC3_MEASURE);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20) + 1);
        err = i2c_master_receive(sensor, data, sizeof(data), I2C_TIMEOUT_MS);
    }
    // Return to sleep after successful and failed measurements alike.
    const esp_err_t sleep_err = send_command(SHTC3_SLEEP);
    if (err != ESP_OK) {
        return err;
    }
    if (sleep_err != ESP_OK) {
        return sleep_err;
    }
    if (crc8(data, 2) != data[2] || crc8(data + 3, 2) != data[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint16_t raw_temperature = ((uint16_t)data[0] << 8) | data[1];
    const uint16_t raw_humidity = ((uint16_t)data[3] << 8) | data[4];
    *temperature_c = -45.0f + 175.0f * raw_temperature / 65536.0f
                     + BOARD_TEMPERATURE_OFFSET_C;
    *humidity_percent = 100.0f * raw_humidity / 65536.0f;
    return ESP_OK;
}
