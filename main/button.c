#include "button.h"

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "board_config.h"

static bool raw_pressed, stable_pressed, long_sent;
static int64_t changed_at, pressed_at;

esp_err_t button_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    raw_pressed = stable_pressed = long_sent = false;
    changed_at = esp_timer_get_time() / 1000;
    return gpio_config(&config);
}

button_event_t button_poll(void)
{
    const int64_t now = esp_timer_get_time() / 1000;
    const bool pressed = gpio_get_level(BOARD_KEY_GPIO) == 0;
    if (pressed != raw_pressed) {
        raw_pressed = pressed;
        changed_at = now;
    }
    if (raw_pressed != stable_pressed && now - changed_at >= BOARD_KEY_DEBOUNCE_MS) {
        stable_pressed = raw_pressed;
        if (stable_pressed) {
            pressed_at = now;
            long_sent = false;
        } else if (!long_sent) {
            // A long hold released between polls still produces only a long event.
            return now - pressed_at >= BOARD_WIFI_SETUP_HOLD_MS ?
                   BUTTON_LONG_PRESS : BUTTON_SHORT_PRESS;
        }
    }
    if (stable_pressed && raw_pressed && !long_sent &&
        now - pressed_at >= BOARD_WIFI_SETUP_HOLD_MS) {
        long_sent = true;
        return BUTTON_LONG_PRESS;
    }
    return BUTTON_NONE;
}
