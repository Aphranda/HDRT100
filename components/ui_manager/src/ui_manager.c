#include "ui_manager.h"

#include <stdint.h>

#include "board.h"
#include "status_ui.h"
#include "storage_manager.h"

#define UI_MANAGER_REFRESH_PERIOD_MS 250u
#define UI_MANAGER_KEY_DEBOUNCE_MS 35u

static uint32_t s_last_refresh_ms;
static uint32_t s_last_key_change_ms;
static bool s_dirty;
static bool s_key_sample;
static bool s_key_stable;

bool ui_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    if (!status_ui_init()) {
        return false;
    }

    s_last_refresh_ms = now_ms;
    s_last_key_change_ms = now_ms;
    s_dirty = true;
    s_key_sample = false;
    s_key_stable = false;
    return true;
}

void ui_manager_mark_dirty(void)
{
    s_dirty = true;
}

void ui_manager_service(void)
{
    const uint32_t now_ms = board_uptime_ms();
    const bool key_sample = board_key2_is_pressed();
    storage_manager_vector_t storage;

    storage_manager_get_vector(&storage);
    if (storage.current_job_state == STORAGE_MANAGER_JOB_STATE_QUEUED ||
        storage.current_job_state == STORAGE_MANAGER_JOB_STATE_RUNNING) {
        return;
    }
    if (storage.card_present &&
        storage.fs_mounted &&
        storage.log_pending_bytes > 0u) {
        return;
    }

    if (key_sample != s_key_sample) {
        s_key_sample = key_sample;
        s_last_key_change_ms = now_ms;
    }

    if ((uint32_t)(now_ms - s_last_key_change_ms) >= UI_MANAGER_KEY_DEBOUNCE_MS &&
        s_key_stable != s_key_sample) {
        s_key_stable = s_key_sample;
        if (s_key_stable) {
            status_ui_key_next();
            s_dirty = true;
        }
    }

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
