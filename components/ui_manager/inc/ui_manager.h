#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define UI_MANAGER_KEY_COUNT 3u

typedef enum {
    UI_MANAGER_KEY_EVENT_NONE = 0,
    UI_MANAGER_KEY_EVENT_PRESS = 1,
    UI_MANAGER_KEY_EVENT_RELEASE = 2,
    UI_MANAGER_KEY_EVENT_SHORT = 3,
    UI_MANAGER_KEY_EVENT_LONG = 4,
    UI_MANAGER_KEY_EVENT_REPEAT = 5,
} ui_manager_key_event_type_t;

typedef struct {
    uint32_t raw_mask;
    uint32_t stable_mask;
    uint32_t event_sequence;
    uint32_t last_event_key;
    ui_manager_key_event_type_t last_event_type;
    uint32_t short_count[UI_MANAGER_KEY_COUNT];
    uint32_t long_count[UI_MANAGER_KEY_COUNT];
    uint32_t repeat_count[UI_MANAGER_KEY_COUNT];
} ui_manager_key_status_t;

bool ui_manager_init(void);
void ui_manager_mark_dirty(void);
void ui_manager_service(void);
void ui_manager_get_key_status(ui_manager_key_status_t *status);

#endif
