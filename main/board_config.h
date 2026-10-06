#pragma once

// Pin assignments from Waveshare's ESP32-S3-RLCD-4.2 factory example.
#define BOARD_NAME "Waveshare ESP32-S3-RLCD-4.2"
#define BOARD_FLASH_SIZE_BYTES (16U * 1024U * 1024U)
#define BOARD_PSRAM_SIZE_BYTES (8U * 1024U * 1024U)

// Landscape orientation, matching the supplied factory demo's main.cpp.
#define BOARD_RLCD_WIDTH 400U
#define BOARD_RLCD_HEIGHT 300U
#define BOARD_RLCD_SPI_HZ (10 * 1000 * 1000)
#define BOARD_RLCD_MOSI_GPIO 12
#define BOARD_RLCD_SCK_GPIO 11
#define BOARD_RLCD_DC_GPIO 5
#define BOARD_RLCD_CS_GPIO 40
#define BOARD_RLCD_RST_GPIO 41
#define BOARD_RLCD_TE_GPIO 6

#define BOARD_I2C_SDA_GPIO 13
#define BOARD_I2C_SCL_GPIO 14
#define BOARD_I2C_HZ 400000U
#define BOARD_SHTC3_ADDRESS 0x70

// Match the factory demo: subtract 4 C from the converted temperature.
#define BOARD_TEMPERATURE_OFFSET_C (-4.0f)
#define BOARD_SAMPLE_INTERVAL_MS 2000U

// Factory adc_bsp.cpp: ADC1 channel 3 (GPIO 4), battery divider 1:3.
#define BOARD_BATTERY_ADC_CHANNEL 3
#define BOARD_BATTERY_DIVIDER_RATIO 3U
#define BOARD_BATTERY_SAMPLE_COUNT 16U
#define BOARD_BATTERY_EMPTY_MV 3000U
#define BOARD_BATTERY_FULL_MV 4120U
// Heuristic only: validate with USB power and an empty battery holder.
#define BOARD_BATTERY_DETECT_MIN_MV 2000U

// KEY: active-low GPIO 18, verified against the local factory button_bsp.c.
#define BOARD_KEY_GPIO 18
#define BOARD_KEY_DEBOUNCE_MS 40U
#define BOARD_UI_POLL_INTERVAL_MS 20U
#define BOARD_WIFI_SETUP_HOLD_MS 3000U
#define BOARD_WIFI_CONNECT_TIMEOUT_MS 30000U
#define BOARD_WIFI_RETRY_INTERVAL_MS 5000U
#define BOARD_WIFI_PORTAL_GRACE_MS 10000U

// POSIX time zone: CST-8 means UTC+8 (Beijing time), without DST.
#define BOARD_TIMEZONE "CST-8"
#define BOARD_NTP_SERVER "ntp.aliyun.com"

#define BOARD_HA_URL "http://192.168.31.41:8123"
#define BOARD_HA_POLL_INTERVAL_MS 15000U
#define BOARD_HA_TIMEOUT_MS 4000U
#define BOARD_HA_STALE_MS 45000U

// Only the read-only device snapshot route is used by the Codeck client.
#define BOARD_CODECK_SNAPSHOT_URL "https://codeck.wangj.de/api/device/v1/snapshot"
#define BOARD_CODECK_TIMEOUT_MS 20000
