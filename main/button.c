#include "button.h"

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "board_config.h"

typedef struct {
    bool raw_pressed, stable_pressed, long_sent;
    int64_t changed_at, pressed_at;
} button_state_t;
static button_state_t key, boot;

esp_err_t button_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_KEY_GPIO) | (1ULL << BOARD_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    key = (button_state_t){.changed_at = esp_timer_get_time() / 1000};
    boot = key;
    return gpio_config(&config);
}

static button_event_t poll_one(button_state_t *state, int pin, bool is_boot)
{
    const int64_t now = esp_timer_get_time() / 1000;
    const bool pressed = gpio_get_level(pin) == 0;
    if (pressed != state->raw_pressed) {
        state->raw_pressed = pressed;
        state->changed_at = now;
    }
    if (state->raw_pressed != state->stable_pressed && now - state->changed_at >= BOARD_KEY_DEBOUNCE_MS) {
        state->stable_pressed = state->raw_pressed;
        if (state->stable_pressed) {
            state->pressed_at = now;
            state->long_sent = false;
        } else if (!state->long_sent) {
            // A long hold released between polls still produces only a long event.
            if (now - state->pressed_at >= BOARD_WIFI_SETUP_HOLD_MS)
                return is_boot ? BUTTON_NONE : BUTTON_LONG_PRESS;
            return is_boot ? BUTTON_BOOT_SHORT_PRESS : BUTTON_SHORT_PRESS;
        }
    }
    if (state->stable_pressed && state->raw_pressed && !state->long_sent &&
        now - state->pressed_at >= BOARD_WIFI_SETUP_HOLD_MS) {
        state->long_sent = true;
        return is_boot ? BUTTON_NONE : BUTTON_LONG_PRESS;
    }
    return BUTTON_NONE;
}

button_event_t button_poll(void)
{
    const button_event_t key_event = poll_one(&key, BOARD_KEY_GPIO, false);
    const button_event_t boot_event = poll_one(&boot, BOARD_BOOT_GPIO, true);
    return key_event != BUTTON_NONE ? key_event : boot_event;
}
