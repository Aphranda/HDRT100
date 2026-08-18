#include "ui_key_input.h"

#include <stddef.h>
#include <string.h>

void ui_key_input_init(ui_key_input_t *input, uint32_t pressed_mask, uint32_t now_ms)
{
    if (input == NULL) {
        return;
    }

    memset(input, 0, sizeof(*input));
    for (uint32_t key = 0u; key < UI_KEY_INPUT_COUNT; key++) {
        const bool pressed = (pressed_mask & (1u << key)) != 0u;
        input->key[key].raw_pressed = pressed;
        input->key[key].stable_pressed = pressed;
        input->key[key].raw_changed_ms = now_ms;
        input->key[key].pressed_ms = now_ms;
        input->key[key].last_repeat_ms = now_ms;
    }
}

ui_key_events_t ui_key_input_update(ui_key_input_t *input,
                                    uint32_t pressed_mask,
                                    uint32_t now_ms)
{
    ui_key_events_t events;
    memset(&events, 0, sizeof(events));
    if (input == NULL) {
        return events;
    }

    for (uint32_t key = 0u; key < UI_KEY_INPUT_COUNT; key++) {
        ui_key_input_state_t *state = &input->key[key];
        const uint32_t bit = 1u << key;
        const bool raw_pressed = (pressed_mask & bit) != 0u;

        if (raw_pressed != state->raw_pressed) {
            state->raw_pressed = raw_pressed;
            state->raw_changed_ms = now_ms;
        }

        if (state->stable_pressed != state->raw_pressed &&
            (uint32_t)(now_ms - state->raw_changed_ms) >= UI_KEY_INPUT_DEBOUNCE_MS) {
            state->stable_pressed = state->raw_pressed;
            if (state->stable_pressed) {
                state->pressed_ms = now_ms;
                state->last_repeat_ms = now_ms;
                state->long_emitted = false;
                events.pressed_mask |= bit;
            } else {
                events.released_mask |= bit;
                if (!state->long_emitted) {
                    events.short_press_mask |= bit;
                }
                state->long_emitted = false;
            }
        }

        if (!state->stable_pressed) {
            continue;
        }

        if (!state->long_emitted &&
            (uint32_t)(now_ms - state->pressed_ms) >= UI_KEY_INPUT_LONG_PRESS_MS) {
            state->long_emitted = true;
            state->last_repeat_ms = now_ms;
            events.long_press_mask |= bit;
        } else if (state->long_emitted &&
                   (uint32_t)(now_ms - state->last_repeat_ms) >= UI_KEY_INPUT_REPEAT_MS) {
            state->last_repeat_ms = now_ms;
            events.repeat_mask |= bit;
        }
    }

    return events;
}
