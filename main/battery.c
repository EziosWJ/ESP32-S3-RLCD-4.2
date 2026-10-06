#include "battery.h"

#include <stddef.h>

#include "board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration;

esp_err_t battery_init(void)
{
    if (adc && calibration) return ESP_OK;
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    esp_err_t err = adc_oneshot_new_unit(&unit, &adc);
    if (err != ESP_OK) return err;
    const adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(adc, BOARD_BATTERY_ADC_CHANNEL, &channel);
    if (err == ESP_OK) {
        const adc_cali_curve_fitting_config_t config = {
            .unit_id = ADC_UNIT_1,
            .chan = BOARD_BATTERY_ADC_CHANNEL,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        err = adc_cali_create_scheme_curve_fitting(&config, &calibration);
    }
    if (err != ESP_OK) {
        adc_oneshot_del_unit(adc);
        adc = NULL;
    }
    return err;
}

esp_err_t battery_read(battery_reading_t *reading)
{
    if (!reading) return ESP_ERR_INVALID_ARG;
    *reading = (battery_reading_t){0};
    if (!adc || !calibration) return ESP_ERR_INVALID_STATE;

    uint32_t sum_mv = 0;
    for (unsigned i = 0; i < BOARD_BATTERY_SAMPLE_COUNT; ++i) {
        int raw, mv;
        esp_err_t err = adc_oneshot_read(adc, BOARD_BATTERY_ADC_CHANNEL, &raw);
        if (err != ESP_OK) return err;
        err = adc_cali_raw_to_voltage(calibration, raw, &mv);
        if (err != ESP_OK) return err;
        if (mv < 0) return ESP_ERR_INVALID_RESPONSE;
        sum_mv += (uint32_t)mv;
    }
    reading->voltage_mv = (sum_mv * BOARD_BATTERY_DIVIDER_RATIO +
                           BOARD_BATTERY_SAMPLE_COUNT / 2) / BOARD_BATTERY_SAMPLE_COUNT;
    reading->valid = true;
    reading->detected = reading->voltage_mv >= BOARD_BATTERY_DETECT_MIN_MV;
    // Match the factory example, with clamping and integer rounding.
    if (reading->voltage_mv >= BOARD_BATTERY_FULL_MV) {
        reading->percent = 100;
    } else if (reading->voltage_mv > BOARD_BATTERY_EMPTY_MV) {
        const unsigned range = BOARD_BATTERY_FULL_MV - BOARD_BATTERY_EMPTY_MV;
        reading->percent = ((reading->voltage_mv - BOARD_BATTERY_EMPTY_MV) * 100U +
                            range / 2) / range;
    }
    return ESP_OK;
}
