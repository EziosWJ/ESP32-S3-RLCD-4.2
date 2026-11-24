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
#define BOARD_BATTERY_SAMPLE_COUNT 16U
// Calibration: pack_mv = pin_mv * RATIO_MILLI / 1000 + OFFSET_MV.
// Measure the pack with a DMM and adjust these two values on the real board;
// nothing else in the firmware assumes an uncalibrated divider.
#define BOARD_BATTERY_DIVIDER_RATIO_MILLI 3000
#define BOARD_BATTERY_MV_OFFSET 0
// Plausible pack window. Insertion is inferred from the voltage only; a pack
// sitting outside this window is reported as not detected.
#define BOARD_BATTERY_DETECT_MIN_MV 2500U
#define BOARD_BATTERY_DETECT_MAX_MV 4400U
// Charging: pack voltage at or above this value, or a rise of at least
// BOARD_BATTERY_CHARGE_RISE_MV inside BOARD_BATTERY_CHARGE_RISE_WINDOW_MS.
#define BOARD_BATTERY_CHARGE_DETECT_MV 4150U
#define BOARD_BATTERY_CHARGE_RISE_MV 40U
#define BOARD_BATTERY_CHARGE_RISE_WINDOW_MS 300000U
// Full: unplugged, resting at or above this voltage for the rest window.
#define BOARD_BATTERY_FULL_REST_MV 4050U
#define BOARD_BATTERY_FULL_REST_MS 600000U
// State of charge smoothing: resting minimum over the window, +/- hysteresis
// before the shown percent may move, and a rate limit per minute.
#define BOARD_BATTERY_SOC_REST_WINDOW_MS 60000U
#define BOARD_BATTERY_SOC_HYSTERESIS_PERCENT 3U
#define BOARD_BATTERY_SOC_MAX_CHANGE_PER_MIN 2U
// Display side: quantize before comparing, and require a stable period first.
#define BOARD_BATTERY_DISPLAY_VOLTAGE_STEP_MV 10U
#define BOARD_BATTERY_DISPLAY_DEBOUNCE_MS 200U

// KEY: active-low GPIO 18, verified against the local factory button_bsp.c.
#define BOARD_KEY_GPIO 18
// Factory button_bsp.c: BOOT is active-low GPIO0 (also the boot strap).
#define BOARD_BOOT_GPIO 0
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
// One request per poll for all entities; the worker never opens one TCP
// connection per entity in the steady state.
#define BOARD_HA_POLL_INTERVAL_MS 60000U
#define BOARD_HA_TIMEOUT_MS 8000U
#define BOARD_HA_STALE_MS 180000U

// Only the read-only device snapshot route is used by the Codeck client.
#define BOARD_CODECK_SNAPSHOT_URL "https://codeck.wangj.de/api/device/v1/snapshot"
#define BOARD_CODECK_TIMEOUT_MS 20000
// Poll only while the CODECK page is visible, on battery-friendly intervals.
#define BOARD_CODECK_POLL_INTERVAL_MS 300000U
#define BOARD_CODECK_RECONNECT_FAILURES 3U
#define BOARD_CODECK_RECONNECT_COOLDOWN_MS 300000U

// Power management: dynamic frequency scaling window and automatic light sleep.
// The maximum must match CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ.
#define BOARD_PM_MAX_FREQ_MHZ 80
#define BOARD_PM_MIN_FREQ_MHZ 40
#define BOARD_PM_LIGHT_SLEEP_ENABLE 1
// Light sleep is suspended while a USB host is attached, so the USB Serial/JTAG
// console stays usable; the firmware re-probes attachment at this interval.
#define BOARD_PM_USB_PROBE_INTERVAL_MS 10000U
