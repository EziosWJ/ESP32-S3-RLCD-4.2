#include "battery.h"

#include <stddef.h>
#include <stdint.h>

#include "board_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "esp_log.h"
#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "battery";

// ---- Open-circuit voltage (OCV) lookup table -----------------------------
// Local-convention OCV curve for a single Li-ion cell, mirrored by
// tests/test_battery_soc.py. If any value in this table changes, that test
// must be updated in lock step; the two are checked against each other.
typedef struct {
    uint32_t mv;
    unsigned percent;
} ocv_point_t;

static const ocv_point_t ocv_table[] = {
    {3300U,   0U},
    {3450U,   8U},
    {3550U,  15U},
    {3650U,  28U},
    {3720U,  40U},
    {3780U,  50U},
    {3850U,  62U},
    {3920U,  73U},
    {4000U,  84U},
    {4080U,  93U},
    {4200U, 100U},
};
#define OCV_TABLE_LEN (sizeof(ocv_table) / sizeof(ocv_table[0]))

// Hysteresis used to leave FULL: the pack must drop clearly below
// BOARD_BATTERY_FULL_REST_MV. The frozen header does not expose this second
// threshold, so it lives here next to the state machine.
#define FULL_EXIT_MV 4000U

// 32 samples at the 2 s call cadence cover the 60 s SOC rest window.
#define SOC_RING_SIZE 32U

typedef struct {
    int64_t t_us;
    uint32_t mv;
    bool detected;
    bool resting;
} sample_t;

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration;
#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t pm_lock;
#endif

static sample_t ring[SOC_RING_SIZE];
static unsigned ring_head;
static unsigned ring_count;

static battery_power_state_t state = BATTERY_POWER_UNKNOWN;
static bool percent_published_valid;
static unsigned published_percent;
static int64_t last_publish_us;
static bool full_rest_active;
static int64_t full_rest_start_us;
static bool rise_anchor_valid;
static int64_t rise_anchor_us;
static uint32_t rise_anchor_mv;

// Linear OCV interpolation; below the first entry -> 0, above the last -> 100.
static unsigned ocv_to_percent(uint32_t mv)
{
    if (mv <= ocv_table[0].mv) {
        return 0U;
    }
    if (mv >= ocv_table[OCV_TABLE_LEN - 1U].mv) {
        return 100U;
    }
    for (unsigned i = 1U; i < OCV_TABLE_LEN; ++i) {
        if (mv <= ocv_table[i].mv) {
            const ocv_point_t hi = ocv_table[i];
            const ocv_point_t lo = ocv_table[i - 1U];
            const uint32_t span_mv = hi.mv - lo.mv;
            const uint32_t rise_mv = mv - lo.mv;
            const unsigned span_pct = hi.percent - lo.percent;
            const unsigned pct = lo.percent +
                (span_pct * rise_mv + span_mv / 2U) / span_mv;
            return pct > 100U ? 100U : pct;
        }
    }
    return 100U;
}

// Apply the calibrated divider: pack_mv = pin_mv * RATIO_MILLI / 1000 + OFFSET_MV.
// int64 avoids overflow; the ratio division is rounded half up.
static uint32_t convert_pack_mv(int32_t pin_mv)
{
    int64_t pack = ((int64_t)pin_mv * BOARD_BATTERY_DIVIDER_RATIO_MILLI + 500) / 1000;
    pack += BOARD_BATTERY_MV_OFFSET;
    if (pack < 0) {
        pack = 0;
    }
    return (uint32_t)pack;
}

static void ring_push(int64_t now_us, uint32_t mv, bool detected, bool resting)
{
    sample_t *slot = &ring[ring_head];
    slot->t_us = now_us;
    slot->mv = mv;
    slot->detected = detected;
    slot->resting = resting;
    ring_head = (ring_head + 1U) % SOC_RING_SIZE;
    if (ring_count < SOC_RING_SIZE) {
        ++ring_count;
    }
}

// Lowest resting (detected, non-charging) sample inside the rest window.
static bool ring_rest_min(int64_t now_us, uint32_t *out_mv)
{
    const int64_t cutoff_us = now_us -
        (int64_t)BOARD_BATTERY_SOC_REST_WINDOW_MS * 1000;
    uint32_t min_mv = UINT32_MAX;
    bool found = false;
    for (unsigned i = 0U; i < ring_count; ++i) {
        const sample_t *sample = &ring[i];
        if (sample->resting && sample->t_us >= cutoff_us) {
            found = true;
            if (sample->mv < min_mv) {
                min_mv = sample->mv;
            }
        }
    }
    if (found) {
        *out_mv = min_mv;
    }
    return found;
}

// Hysteresis + per-minute rate limit against the last published percent.
static void update_published(int64_t now_us, unsigned raw)
{
    if (!percent_published_valid) {
        // First valid sample publishes the estimate directly, no gate.
        published_percent = raw;
        percent_published_valid = true;
        last_publish_us = now_us;
        return;
    }

    unsigned target = published_percent;
    if (raw > published_percent + BOARD_BATTERY_SOC_HYSTERESIS_PERCENT) {
        target = raw;
    } else if (raw + BOARD_BATTERY_SOC_HYSTERESIS_PERCENT < published_percent) {
        target = raw;
    }
    if (target == published_percent) {
        return;
    }

    const int64_t elapsed_min =
        (now_us - last_publish_us) / 60000000LL;
    const unsigned max_delta =
        (unsigned)BOARD_BATTERY_SOC_MAX_CHANGE_PER_MIN * (unsigned)elapsed_min;
    const int diff = (int)target - (int)published_percent;
    unsigned step;
    if (diff > 0) {
        const unsigned rise = (unsigned)diff;
        step = rise < max_delta ? rise : max_delta;
    } else {
        const unsigned drop = (unsigned)(-diff);
        step = drop < max_delta ? drop : max_delta;
    }
    if (step == 0U) {
        return;
    }
    if (diff > 0) {
        published_percent += step;
    } else {
        published_percent -= step;
    }
    last_publish_us = now_us;
}

esp_err_t battery_init(void)
{
    if (adc && calibration) {
        return ESP_OK;
    }
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    esp_err_t err = adc_oneshot_new_unit(&unit, &adc);
    if (err != ESP_OK) {
        return err;
    }
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
#ifdef CONFIG_PM_ENABLE
    if (err == ESP_OK) {
        const esp_err_t lock_err =
            esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "batt", &pm_lock);
        if (lock_err != ESP_OK) {
            pm_lock = NULL;
            ESP_LOGW(TAG, "PM lock unavailable (%s); sampling runs unlocked",
                     esp_err_to_name(lock_err));
        }
    }
#endif
    if (err != ESP_OK) {
        if (adc) {
            adc_oneshot_del_unit(adc);
            adc = NULL;
        }
        calibration = NULL;
    }
    return err;
}

esp_err_t battery_read(battery_reading_t *reading)
{
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }
    *reading = (battery_reading_t){0};
    if (!adc || !calibration) {
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_acquire(pm_lock);
    }
#endif

    uint32_t sum_mv = 0U;
    esp_err_t err = ESP_OK;
    for (unsigned i = 0U; i < BOARD_BATTERY_SAMPLE_COUNT; ++i) {
        int raw = 0;
        int mv = 0;
        err = adc_oneshot_read(adc, BOARD_BATTERY_ADC_CHANNEL, &raw);
        if (err != ESP_OK) {
            break;
        }
        err = adc_cali_raw_to_voltage(calibration, raw, &mv);
        if (err != ESP_OK) {
            break;
        }
        if (mv < 0) {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        sum_mv += (uint32_t)mv;
    }

#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_release(pm_lock);
    }
#endif

    if (err != ESP_OK) {
        return err;
    }

    const uint32_t avg_pin_mv =
        (sum_mv + BOARD_BATTERY_SAMPLE_COUNT / 2U) / BOARD_BATTERY_SAMPLE_COUNT;
    const uint32_t pack_mv = convert_pack_mv((int32_t)avg_pin_mv);
    const int64_t now_us = esp_timer_get_time();

    reading->voltage_mv = pack_mv;
    reading->valid = true;

    const bool detected = pack_mv >= BOARD_BATTERY_DETECT_MIN_MV &&
                          pack_mv <= BOARD_BATTERY_DETECT_MAX_MV;
    reading->detected = detected;

    if (!detected) {
        state = BATTERY_POWER_UNKNOWN;
        percent_published_valid = false;
        full_rest_active = false;
        full_rest_start_us = 0;
        rise_anchor_valid = false;
        ring_push(now_us, pack_mv, false, false);
        reading->percent = 0U;
        reading->percent_valid = false;
        reading->power_state = BATTERY_POWER_UNKNOWN;
        return ESP_OK;
    }

    // Rise detector: track the lowest sample of the active rise window.
    // Once the anchor ages out past the window it restarts from the current
    // sample; a rise of CHARGE_RISE_MV over the window flags charging.
    if (!rise_anchor_valid || pack_mv < rise_anchor_mv) {
        rise_anchor_valid = true;
        rise_anchor_us = now_us;
        rise_anchor_mv = pack_mv;
    } else if (now_us - rise_anchor_us >
               (int64_t)BOARD_BATTERY_CHARGE_RISE_WINDOW_MS * 1000) {
        rise_anchor_us = now_us;
        rise_anchor_mv = pack_mv;
    }
    const bool rising = rise_anchor_valid &&
        (pack_mv - rise_anchor_mv >= BOARD_BATTERY_CHARGE_RISE_MV);
    const bool charging = pack_mv >= BOARD_BATTERY_CHARGE_DETECT_MV || rising;

    // Leave FULL only after the pack drops below the exit hysteresis level.
    if (state == BATTERY_POWER_FULL && pack_mv < FULL_EXIT_MV) {
        state = BATTERY_POWER_DISCHARGING;
        full_rest_active = false;
        full_rest_start_us = 0;
    }

    if (charging) {
        state = BATTERY_POWER_CHARGING;
        full_rest_active = false;
        full_rest_start_us = 0;
        percent_published_valid = false;
    } else if (state != BATTERY_POWER_FULL) {
        if (state == BATTERY_POWER_CHARGING || state == BATTERY_POWER_UNKNOWN) {
            state = BATTERY_POWER_DISCHARGING;
        }
        // Accumulate a sustained high-rest run before declaring FULL.
        if (pack_mv >= BOARD_BATTERY_FULL_REST_MV) {
            if (!full_rest_active) {
                full_rest_active = true;
                full_rest_start_us = now_us;
            } else if (now_us - full_rest_start_us >=
                       (int64_t)BOARD_BATTERY_FULL_REST_MS * 1000) {
                state = BATTERY_POWER_FULL;
                published_percent = 100U;
                percent_published_valid = true;
                last_publish_us = now_us;
            }
        } else {
            full_rest_start_us = 0;
        }
    }

    ring_push(now_us, pack_mv, detected, !charging);

    switch (state) {
    case BATTERY_POWER_CHARGING:
        reading->power_state = BATTERY_POWER_CHARGING;
        reading->percent_valid = false;
        reading->percent = published_percent;  // stale; invalid per contract
        break;
    case BATTERY_POWER_FULL:
        reading->power_state = BATTERY_POWER_FULL;
        reading->percent_valid = true;
        reading->percent = 100U;
        break;
    case BATTERY_POWER_DISCHARGING: {
        reading->power_state = BATTERY_POWER_DISCHARGING;
        uint32_t rest_mv;
        if (ring_rest_min(now_us, &rest_mv)) {
            update_published(now_us, ocv_to_percent(rest_mv));
        }
        reading->percent_valid = percent_published_valid;
        reading->percent = published_percent;
        break;
    }
    default:
        reading->power_state = BATTERY_POWER_UNKNOWN;
        reading->percent_valid = false;
        reading->percent = 0U;
        break;
    }
    return ESP_OK;
}
