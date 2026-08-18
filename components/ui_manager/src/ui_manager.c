#include "ui_manager.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "osal.h"
#include "status_ui.h"
#include "ui_key_input.h"

/* A full 160x80 RGB565 flush takes about 21 ms at the proven 10 MHz LCD SPI
 * rate.  SPI0 is now dedicated to the LCD, so a 10 Hz status cadence leaves
 * ample core0 headroom while keeping live values visibly responsive. */
#define UI_MANAGER_REFRESH_PERIOD_MS 100u

static uint32_t s_last_refresh_ms;
static bool s_dirty;
static ui_key_input_t s_key_input;
static ui_manager_key_status_t s_key_status;

_Static_assert(UI_MANAGER_KEY_COUNT == UI_KEY_INPUT_COUNT,
               "UI key status and input key counts must match");

static uint32_t ui_manager_read_key_mask(void)
{
    uint32_t mask = 0u;
    for (uint32_t key = 0u; key < (uint32_t)BOARD_KEY_COUNT; key++) {
        if (board_key_is_pressed((board_key_t)key)) {
            mask |= 1u << key;
        }
    }
    return mask;
}

static uint32_t ui_manager_stable_key_mask(void)
{
    uint32_t mask = 0u;
    for (uint32_t key = 0u; key < UI_MANAGER_KEY_COUNT; key++) {
        if (s_key_input.key[key].stable_pressed) {
            mask |= 1u << key;
        }
    }
    return mask;
}

static void ui_manager_record_key_events(ui_manager_key_status_t *status,
                                         uint32_t event_mask,
                                         ui_manager_key_event_type_t event_type)
{
    for (uint32_t key = 0u; key < UI_MANAGER_KEY_COUNT; key++) {
        if ((event_mask & (1u << key)) == 0u) {
            continue;
        }

        status->event_sequence++;
        status->last_event_key = key + 1u;
        status->last_event_type = event_type;

        switch (event_type) {
        case UI_MANAGER_KEY_EVENT_SHORT:
            status->short_count[key]++;
            break;
        case UI_MANAGER_KEY_EVENT_LONG:
            status->long_count[key]++;
            break;
        case UI_MANAGER_KEY_EVENT_REPEAT:
            status->repeat_count[key]++;
            break;
        default:
            break;
        }
    }
}

static void ui_manager_publish_key_status(uint32_t raw_mask, ui_key_events_t events)
{
    ui_manager_key_status_t status;

    osal_critical_enter();
    status = s_key_status;
    osal_critical_exit();

    status.raw_mask = raw_mask;
    status.stable_mask = ui_manager_stable_key_mask();
    ui_manager_record_key_events(&status,
                                 events.pressed_mask,
                                 UI_MANAGER_KEY_EVENT_PRESS);
    ui_manager_record_key_events(&status,
                                 events.released_mask,
                                 UI_MANAGER_KEY_EVENT_RELEASE);
    ui_manager_record_key_events(&status,
                                 events.short_press_mask,
                                 UI_MANAGER_KEY_EVENT_SHORT);
    ui_manager_record_key_events(&status,
                                 events.long_press_mask,
                                 UI_MANAGER_KEY_EVENT_LONG);
    ui_manager_record_key_events(&status,
                                 events.repeat_mask,
                                 UI_MANAGER_KEY_EVENT_REPEAT);

    osal_critical_enter();
    s_key_status = status;
    osal_critical_exit();
}

static void ui_manager_dispatch_keys(ui_key_events_t events)
{
    const uint32_t key_left = 1u << (uint32_t)BOARD_KEY_LEFT;
    const uint32_t key_center = 1u << (uint32_t)BOARD_KEY_CENTER;
    const uint32_t key_right = 1u << (uint32_t)BOARD_KEY_RIGHT;

    /* Dispatch the primary action on the debounced press edge.  Waiting for
     * short_press_mask means waiting for release, which makes the panel feel
     * unresponsive for as long as the operator holds a key. */
    if ((events.pressed_mask & key_left) != 0u ||
        (events.repeat_mask & key_left) != 0u) {
        status_ui_key_previous();
        s_dirty = true;
    }
    if ((events.pressed_mask & key_center) != 0u) {
        status_ui_key_select();
        s_dirty = true;
    }
    if ((events.pressed_mask & key_right) != 0u ||
        (events.repeat_mask & key_right) != 0u) {
        status_ui_key_next();
        s_dirty = true;
    }
}

bool ui_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();
    const uint32_t pressed_mask = ui_manager_read_key_mask();

    if (!status_ui_init()) {
        return false;
    }

    s_last_refresh_ms = now_ms;
    s_dirty = true;
    ui_key_input_init(&s_key_input, pressed_mask, now_ms);
    memset(&s_key_status, 0, sizeof(s_key_status));
    s_key_status.raw_mask = pressed_mask;
    s_key_status.stable_mask = pressed_mask;
    return true;
}

void ui_manager_mark_dirty(void)
{
    s_dirty = true;
}

void ui_manager_get_key_status(ui_manager_key_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_key_status;
    osal_critical_exit();
}

void ui_manager_service(void)
{
    const uint32_t now_ms = board_uptime_ms();
    const uint32_t raw_key_mask = ui_manager_read_key_mask();
    const ui_key_events_t key_events =
        ui_key_input_update(&s_key_input, raw_key_mask, now_ms);
    ui_manager_publish_key_status(raw_key_mask, key_events);
    ui_manager_dispatch_keys(key_events);

    /* LCD is on dedicated SPI0 while the TF card is on SPI1.  SD work no
     * longer blocks display refresh; the resource arbiter still protects the
     * LCD itself inside status_ui_render(). */

    if ((uint32_t)(now_ms - s_last_refresh_ms) >= UI_MANAGER_REFRESH_PERIOD_MS) {
        s_dirty = true;
    }
    if (status_ui_needs_render()) {
        s_dirty = true;
    }

    if (!s_dirty) {
        return;
    }

    if (status_ui_render()) {
        s_dirty = status_ui_needs_render();
        s_last_refresh_ms = now_ms;
    }
}
