#pragma once

// Pin assignments from Waveshare's ESP32-S3-RLCD-4.2 factory example.
// These constants describe the hardware; no peripheral is initialized here.
#define BOARD_NAME "Waveshare ESP32-S3-RLCD-4.2"
#define BOARD_FLASH_SIZE_BYTES (16U * 1024U * 1024U)
#define BOARD_PSRAM_SIZE_BYTES (8U * 1024U * 1024U)

#define BOARD_RLCD_WIDTH 300U
#define BOARD_RLCD_HEIGHT 400U
#define BOARD_RLCD_MOSI_GPIO 12
#define BOARD_RLCD_SCK_GPIO 11
#define BOARD_RLCD_DC_GPIO 5
#define BOARD_RLCD_CS_GPIO 40
#define BOARD_RLCD_RST_GPIO 41
#define BOARD_RLCD_TE_GPIO 6

#define BOARD_I2C_SDA_GPIO 13
#define BOARD_I2C_SCL_GPIO 14
