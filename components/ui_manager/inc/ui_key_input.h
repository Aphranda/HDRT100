#ifndef UI_KEY_INPUT_H
#define UI_KEY_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define UI_KEY_INPUT_COUNT 3u
#define UI_KEY_INPUT_DEBOUNCE_MS 8u
#define UI_KEY_INPUT_LONG_PRESS_MS 700u
#define UI_KEY_INPUT_REPEAT_MS 250u

typedef struct {
    bool raw_pressed;
    bool stable_pressed;
    bool long_emitted;
    uint32_t raw_changed_ms;
    uint32_t pressed_ms;
    uint32_t last_repeat_ms;
} ui_key_input_state_t;

typedef struct {
    ui_key_input_state_t key[UI_KEY_INPUT_COUNT];
} ui_key_input_t;

typedef struct {
    uint32_t pressed_mask;
    uint32_t released_mask;
    uint32_t short_press_mask;
    uint32_t long_press_mask;
    uint32_t repeat_mask;
} ui_key_events_t;

void ui_key_input_init(ui_key_input_t *input, uint32_t pressed_mask, uint32_t now_ms);
ui_key_events_t ui_key_input_update(ui_key_input_t *input,
                                    uint32_t pressed_mask,
                                    uint32_t now_ms);

#endif
