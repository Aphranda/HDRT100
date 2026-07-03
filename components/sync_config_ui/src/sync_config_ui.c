#include "sync_config_ui.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "lcd_st7789.h"
#include "osal.h"
#include "ota_ao.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "storage_manager.h"
#include "sync_trigger.h"
#include "u8g2.h"
#include "u8g2_port.h"

#define UI_WIDTH 240u
#define UI_HEIGHT 135u
#define UI_U8G2_HEIGHT 136u
#define UI_MONO_BUFFER_SIZE ((UI_WIDTH * UI_U8G2_HEIGHT) / 8u)
#define UI_FLUSH_PIXELS 120u
#define UI_CARD_Y 22u
#define UI_CARD_H 92u
#define UI_FOOTER_Y 124u
#define UI_TAB_ANIM_STEPS 4u

typedef enum {
    UI_PAGE_OVERVIEW = 0,
    UI_PAGE_TRIGGER,
    UI_PAGE_OTA,
    UI_PAGE_SD,
    UI_PAGE_COUNT,
} ui_page_t;

typedef struct {
    sync_trigger_summary_t trigger;
    ota_vector_t ota;
    storage_manager_vector_t storage;
    resource_arbiter_snapshot_t arbiter;
    bool fault_active;
    uint32_t uptime_ms;
} ui_snapshot_t;

typedef struct {
    u8g2_t u8g2;
    uint8_t mono_buffer[UI_MONO_BUFFER_SIZE];
    uint16_t line_buffer[UI_FLUSH_PIXELS];
    ui_page_t page;
    ui_page_t target_page;
    ui_page_t previous_page;
    uint32_t frame;
    uint8_t tab_from_first;
    uint8_t tab_to_first;
    uint8_t tab_anim;
    bool boot_splash_active;
    bool initialized;
} sync_config_ui_t;

static sync_config_ui_t s_ui;

extern const uint8_t u8g2_font_5x8_tr[];
extern const uint8_t u8g2_font_6x10_tf[];
extern const uint8_t u8g2_font_6x13B_tf[];

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((uint16_t)(r & 0xF8u) << 8) |
           (uint16_t)((uint16_t)(g & 0xFCu) << 3) |
           (uint16_t)(b >> 3);
}

static const char *bool_to_on_off(bool value)
{
    return value ? "ON" : "OFF";
}

static const char *bool_to_run_stop(bool value)
{
    return value ? "RUN" : "STOP";
}

static const char *ui_page_to_label(ui_page_t page)
{
    switch (page) {
    case UI_PAGE_OVERVIEW:
        return "OVR";
    case UI_PAGE_TRIGGER:
        return "TRG";
    case UI_PAGE_OTA:
        return "OTA";
    case UI_PAGE_SD:
        return "SD";
    default:
        return "UNK";
    }
}

static const char *trigger_mode_to_short(trig_mode_t mode)
{
    switch (mode) {
    case TRIG_MODE_IDLE:
        return "IDLE";
    case TRIG_MODE_SEQ_STEP:
        return "SEQ";
    case TRIG_MODE_ENC_COUNT:
        return "ENC";
    default:
        return "MODE?";
    }
}

static const char *trigger_state_to_short(trig_state_t state)
{
    switch (state) {
    case TRIG_STATE_IDLE:
        return "IDLE";
    case TRIG_STATE_SEQ_CONFIGURED:
    case TRIG_STATE_ENC_CONFIGURED:
        return "CFG";
    case TRIG_STATE_SEQ_ARMED:
    case TRIG_STATE_ENC_ARMED:
        return "ARM";
    case TRIG_STATE_FAULT:
        return "FAULT";
    default:
        return "UNK";
    }
}

static const char *edge_to_short(trig_edge_t edge)
{
    return edge == TRIG_EDGE_FALLING ? "FALL" : "RISE";
}

static const char *arbiter_mode_to_short(resource_arbiter_mode_t mode)
{
    switch (mode) {
    case RESOURCE_ARBITER_MODE_BOOT:
        return "BOOT";
    case RESOURCE_ARBITER_MODE_RUN:
        return "RUN";
    case RESOURCE_ARBITER_MODE_OTA:
        return "OTA";
    case RESOURCE_ARBITER_MODE_FAULT:
        return "FAULT";
    default:
        return "UNK";
    }
}

static const char *ota_state_to_short_label(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_IDLE:
        return "IDLE";
    case OTA_STATE_CHECK_PERMISSION:
        return "PERMIT";
    case OTA_STATE_ERASE_SLOT:
        return "ERASE";
    case OTA_STATE_RECEIVING:
        return "RX";
    case OTA_STATE_VERIFYING:
        return "VERIFY";
    case OTA_STATE_MARK_PENDING:
        return "MARK";
    case OTA_STATE_READY_TO_REBOOT:
        return "REBOOT";
    case OTA_STATE_PENDING_CONFIRM:
        return "PEND";
    case OTA_STATE_COMMITTED:
        return "COMMIT";
    case OTA_STATE_FAILED:
        return "FAIL";
    case OTA_STATE_ABORTED:
        return "ABORT";
    default:
        return "UNK";
    }
}

static const char *ota_result_to_short_label(ota_result_t result)
{
    switch (result) {
    case OTA_RESULT_NONE:
        return "NONE";
    case OTA_RESULT_ACCEPTED:
        return "ACCEPT";
    case OTA_RESULT_IMAGE_STAGED:
        return "STAGED";
    case OTA_RESULT_ABORTED:
        return "ABORT";
    case OTA_RESULT_FAILED:
        return "FAIL";
    case OTA_RESULT_COMMITTED:
        return "COMMIT";
    default:
        return "UNK";
    }
}

static const char *ota_boot_result_to_short_label(uint32_t result)
{
    switch (result) {
    case OTA_BOOT_RESULT_NONE:
        return "NONE";
    case OTA_BOOT_RESULT_APPLIED:
        return "APPLY";
    case OTA_BOOT_RESULT_NO_PENDING:
        return "NOPEND";
    case OTA_BOOT_RESULT_MAX_ATTEMPTS:
        return "MAXTRY";
    case OTA_BOOT_RESULT_STAGE_VALIDATE_FAILED:
        return "STGVAL";
    case OTA_BOOT_RESULT_COPY_FAILED:
        return "COPY";
    case OTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED:
        return "ACTVAL";
    default:
        return "UNK";
    }
}

static void copy_compact_error(char *buffer, size_t buffer_size, uint32_t error_code)
{
    const char *error = ota_error_to_string(error_code);

    if (strcmp(error, "NONE") == 0) {
        snprintf(buffer, buffer_size, "NONE");
    } else if (strcmp(error, "QUEUE_FULL") == 0) {
        snprintf(buffer, buffer_size, "QFULL");
    } else if (strcmp(error, "BAD_HEADER") == 0) {
        snprintf(buffer, buffer_size, "BHDR");
    } else if (strcmp(error, "IMAGE_TOO_LARGE") == 0) {
        snprintf(buffer, buffer_size, "TOOBIG");
    } else if (strcmp(error, "VERSION_REJECTED") == 0) {
        snprintf(buffer, buffer_size, "VER");
    } else if (strcmp(error, "VECTOR") == 0) {
        snprintf(buffer, buffer_size, "VECTOR");
    } else if (strcmp(error, "CRC") == 0) {
        snprintf(buffer, buffer_size, "CRC");
    } else {
        snprintf(buffer, buffer_size, "%.6s", error);
    }
}

static void format_freq_hz(char *buffer, size_t buffer_size, uint32_t hz)
{
    const uint32_t whole_mhz = hz / 1000000u;
    const uint32_t frac_mhz = (hz % 1000000u) / 100000u;
    const uint32_t whole_khz = hz / 1000u;
    const uint32_t frac_khz = (hz % 1000u) / 100u;

    if (hz >= 1000000u) {
        snprintf(buffer, buffer_size, "%lu.%luM",
                 (unsigned long)whole_mhz,
                 (unsigned long)frac_mhz);
    } else if (hz >= 1000u) {
        snprintf(buffer, buffer_size, "%lu.%luk",
                 (unsigned long)whole_khz,
                 (unsigned long)frac_khz);
    } else {
        snprintf(buffer, buffer_size, "%luHz", (unsigned long)hz);
    }
}

static void format_duration_us(char *buffer, size_t buffer_size, uint32_t duration_us)
{
    if (duration_us >= 1000u) {
        const uint32_t whole_ms = duration_us / 1000u;
        const uint32_t frac_ms = (duration_us % 1000u) / 100u;
        snprintf(buffer, buffer_size, "%lu.%lums",
                 (unsigned long)whole_ms,
                 (unsigned long)frac_ms);
        return;
    }

    snprintf(buffer, buffer_size, "%luus", (unsigned long)duration_us);
}

static void format_progress(char *buffer, size_t buffer_size, uint32_t progress_permille)
{
    snprintf(buffer, buffer_size, "%lu.%lu%%",
             (unsigned long)(progress_permille / 10u),
             (unsigned long)(progress_permille % 10u));
}

static void format_size_compact(char *buffer, size_t buffer_size, uint32_t size_bytes)
{
    if (size_bytes >= (1024u * 1024u)) {
        const uint32_t whole_mb = size_bytes / (1024u * 1024u);
        const uint32_t frac_mb = (size_bytes % (1024u * 1024u)) / (1024u * 100u);
        snprintf(buffer, buffer_size, "%lu.%luM",
                 (unsigned long)whole_mb,
                 (unsigned long)frac_mb);
        return;
    }

    if (size_bytes >= 1024u) {
        const uint32_t whole_kb = size_bytes / 1024u;
        const uint32_t frac_kb = (size_bytes % 1024u) / 100u;
        snprintf(buffer, buffer_size, "%lu.%luK",
                 (unsigned long)whole_kb,
                 (unsigned long)frac_kb);
        return;
    }

    snprintf(buffer, buffer_size, "%luB", (unsigned long)size_bytes);
}

static void format_kib_compact(char *buffer, size_t buffer_size, uint32_t size_kib)
{
    if (size_kib >= (1024u * 1024u)) {
        const uint32_t whole_gib = size_kib / (1024u * 1024u);
        const uint32_t frac_gib = (size_kib % (1024u * 1024u)) / (1024u * 102u);
        snprintf(buffer, buffer_size, "%lu.%luGiB",
                 (unsigned long)whole_gib,
                 (unsigned long)frac_gib);
        return;
    }

    if (size_kib >= 1024u) {
        const uint32_t whole_mib = size_kib / 1024u;
        const uint32_t frac_mib = (size_kib % 1024u) / 102u;
        snprintf(buffer, buffer_size, "%lu.%luMiB",
                 (unsigned long)whole_mib,
                 (unsigned long)frac_mib);
        return;
    }

    snprintf(buffer, buffer_size, "%luKiB", (unsigned long)size_kib);
}

static void format_rx_progress(char *buffer, size_t buffer_size, uint32_t received_size, uint32_t expected_size)
{
    char rx_buffer[12];
    char exp_buffer[12];

    format_size_compact(rx_buffer, sizeof(rx_buffer), received_size);
    format_size_compact(exp_buffer, sizeof(exp_buffer), expected_size);
    snprintf(buffer, buffer_size, "%s/%s", rx_buffer, exp_buffer);
}

static void format_uptime(char *buffer, size_t buffer_size, uint32_t uptime_ms)
{
    const uint32_t total_seconds = uptime_ms / 1000u;
    const uint32_t hours = total_seconds / 3600u;
    const uint32_t minutes = (total_seconds / 60u) % 60u;
    const uint32_t seconds = total_seconds % 60u;

    if (hours > 0u) {
        snprintf(buffer, buffer_size, "%02lu:%02lu:%02lu",
                 (unsigned long)hours,
                 (unsigned long)minutes,
                 (unsigned long)seconds);
        return;
    }

    snprintf(buffer, buffer_size, "%02lu:%02lu",
             (unsigned long)minutes,
             (unsigned long)seconds);
}

static void format_short_build(char *buffer, size_t buffer_size)
{
    const size_t build_len = strlen(g_project_build_id);

    if (build_len <= 8u) {
        snprintf(buffer, buffer_size, "%s", g_project_build_id);
        return;
    }

    snprintf(buffer, buffer_size, "%.8s", g_project_build_id);
}

static const char *ota_slot_to_short_label(uint32_t slot)
{
    switch ((ota_slot_t)slot) {
    case OTA_SLOT_A:
        return "A";
    case OTA_SLOT_B:
        return "B";
    case OTA_SLOT_NONE:
    default:
        return "-";
    }
}

static void format_resource_summary(char *buffer, size_t buffer_size, uint32_t resources)
{
    if (resources == 0u) {
        snprintf(buffer, buffer_size, "FREE");
        return;
    }

    if ((resources & RESOURCE_ARBITER_RESOURCE_FLASH) != 0u) {
        snprintf(buffer, buffer_size, "FLASH");
        return;
    }

    if ((resources & RESOURCE_ARBITER_RESOURCE_SPI0) != 0u &&
        (resources & RESOURCE_ARBITER_RESOURCE_LCD) != 0u) {
        snprintf(buffer, buffer_size, "SPI0+LCD");
        return;
    }

    if ((resources & RESOURCE_ARBITER_RESOURCE_SPI0) != 0u &&
        (resources & RESOURCE_ARBITER_RESOURCE_SD) != 0u) {
        snprintf(buffer, buffer_size, "SPI0+SD");
        return;
    }

    snprintf(buffer, buffer_size, "0x%03lX", (unsigned long)resources);
}

static void capture_snapshot(ui_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    sync_trigger_get_summary(&snapshot->trigger);
    ota_ao_get_vector(&snapshot->ota);
    storage_manager_get_vector(&snapshot->storage);
    resource_arbiter_get_snapshot(&snapshot->arbiter);
    snapshot->fault_active = diagnostics_has_fault();
    snapshot->uptime_ms = board_uptime_ms();
}

static void draw_card(u8g2_t *u8g2, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const char *title)
{
    u8g2_DrawFrame(u8g2, x, y, w, h);
    u8g2_DrawHLine(u8g2, (u8g2_uint_t)(x + 1u), (u8g2_uint_t)(y + 12u), (u8g2_uint_t)(w - 2u));
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, (u8g2_uint_t)(x + 4u), (u8g2_uint_t)(y + 9u), title);
}

static void copy_fit_text(u8g2_t *u8g2, char *buffer, size_t buffer_size, const char *text, u8g2_uint_t max_width)
{
    size_t source_len;

    if (buffer_size == 0u) {
        return;
    }

    if (max_width == 0u || text == NULL) {
        buffer[0] = '\0';
        return;
    }

    snprintf(buffer, buffer_size, "%s", text);
    if (u8g2_GetStrWidth(u8g2, buffer) <= max_width) {
        return;
    }

    source_len = strlen(buffer);
    while (source_len > 0u) {
        source_len--;
        if (source_len == 0u) {
            buffer[0] = '\0';
        } else {
            buffer[source_len - 1u] = '~';
            buffer[source_len] = '\0';
        }

        if (u8g2_GetStrWidth(u8g2, buffer) <= max_width) {
            return;
        }
    }

    buffer[0] = '\0';
}

static void draw_fit_str(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t max_width, const char *text)
{
    char fit_buffer[32];

    copy_fit_text(u8g2, fit_buffer, sizeof(fit_buffer), text, max_width);
    if (fit_buffer[0] != '\0') {
        u8g2_DrawStr(u8g2, x, y, fit_buffer);
    }
}

static void draw_kv_line(u8g2_t *u8g2,
                         uint8_t x,
                         uint8_t y,
                         uint8_t w,
                         const char *label,
                         const char *value)
{
    char fit_buffer[32];
    const u8g2_uint_t right = (u8g2_uint_t)(x + w - 4u);
    u8g2_uint_t value_left;
    u8g2_uint_t value_width;

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, x, y, label);

    value_left = (u8g2_uint_t)(x + u8g2_GetStrWidth(u8g2, label) + 4u);
    if (value_left >= right) {
        return;
    }

    copy_fit_text(u8g2, fit_buffer, sizeof(fit_buffer), value, (u8g2_uint_t)(right - value_left));
    value_width = u8g2_GetStrWidth(u8g2, fit_buffer);
    if (fit_buffer[0] != '\0') {
        u8g2_DrawStr(u8g2, (u8g2_uint_t)(right - value_width), y, fit_buffer);
    }
}

static void draw_progress_bar(u8g2_t *u8g2, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint32_t permille)
{
    uint8_t fill;

    if (permille > 1000u) {
        permille = 1000u;
    }

    fill = (uint8_t)(((uint32_t)(w - 2u) * permille) / 1000u);
    u8g2_DrawFrame(u8g2, x, y, w, h);
    if (fill > 0u) {
        u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + 1u), (u8g2_uint_t)(y + 1u), fill, (u8g2_uint_t)(h - 2u));
    }
}

static void draw_activity_bar(u8g2_t *u8g2, uint8_t x, uint8_t y, uint8_t w, uint32_t value, uint32_t scale)
{
    uint32_t permille = 0u;

    if (scale > 0u) {
        permille = (value >= scale) ? 1000u : ((value * 1000u) / scale);
    }
    draw_progress_bar(u8g2, x, y, w, 5u, permille);
}

static void draw_status_dot(u8g2_t *u8g2, uint8_t x, uint8_t y, bool active, bool blink)
{
    if (active || blink) {
        u8g2_DrawDisc(u8g2, x, y, active ? 3u : 2u, U8G2_DRAW_ALL);
    } else {
        u8g2_DrawCircle(u8g2, x, y, 3u, U8G2_DRAW_ALL);
    }
}

static uint8_t tab_first_for_page(uint8_t active_page, uint8_t page_count, uint8_t visible_count)
{
    if (page_count <= visible_count || active_page == 0u) {
        return 0u;
    }
    if (active_page >= (uint8_t)(page_count - 1u)) {
        return (uint8_t)(page_count - visible_count);
    }
    return (uint8_t)(active_page - 1u);
}

static void draw_page_tabs(u8g2_t *u8g2)
{
    enum {
        TAB_VISIBLE_COUNT = 3u,
        TAB_X = 81u,
        TAB_Y = 1u,
        TAB_SLOT_W = 31u,
        TAB_H = 11u,
        TAB_INDICATOR_W = 18u,
    };
    static const ui_page_t pages[] = {
        UI_PAGE_OVERVIEW,
        UI_PAGE_TRIGGER,
        UI_PAGE_OTA,
        UI_PAGE_SD,
    };
    const uint8_t active_page = (uint8_t)(s_ui.tab_anim > 0u ? s_ui.target_page : s_ui.page);
    const uint8_t previous_page = (uint8_t)s_ui.previous_page;
    const uint8_t page_count = (uint8_t)(sizeof(pages) / sizeof(pages[0]));
    const uint8_t visible_count = TAB_VISIBLE_COUNT;
    const uint8_t target_first = tab_first_for_page(active_page, page_count, visible_count);
    const uint8_t from_first = s_ui.tab_anim > 0u ? s_ui.tab_from_first : target_first;
    const uint8_t to_first = s_ui.tab_anim > 0u ? s_ui.tab_to_first : target_first;
    const int16_t first_delta = (int16_t)to_first - (int16_t)from_first;
    const uint8_t progress = s_ui.tab_anim > 0u ? (uint8_t)(UI_TAB_ANIM_STEPS - s_ui.tab_anim + 1u) :
                                                   UI_TAB_ANIM_STEPS;
    const int16_t scroll_offset = (int16_t)(-first_delta * (int16_t)TAB_SLOT_W * (int16_t)progress /
                                            (int16_t)UI_TAB_ANIM_STEPS);
    const uint8_t draw_first = from_first < to_first ? from_first : to_first;
    uint8_t draw_last = (uint8_t)((from_first > to_first ? from_first : to_first) + visible_count);
    uint8_t active_slot = active_page >= to_first ? (uint8_t)(active_page - to_first) : 0u;
    uint8_t previous_slot = previous_page >= from_first ? (uint8_t)(previous_page - from_first) : 0u;
    int16_t active_x;
    int16_t indicator_x;

    if (draw_last > page_count) {
        draw_last = page_count;
    }
    if (active_slot >= visible_count) {
        active_slot = (uint8_t)(visible_count - 1u);
    }
    if (previous_slot >= visible_count) {
        previous_slot = (uint8_t)(visible_count - 1u);
    }
    if (s_ui.tab_anim > 0u) {
        const int16_t start_x = (int16_t)(TAB_X + 1u + (previous_slot * TAB_SLOT_W));
        const int16_t end_x = (int16_t)(TAB_X + 1u + (active_slot * TAB_SLOT_W));
        active_x = (int16_t)(start_x + (((end_x - start_x) * (int16_t)progress) /
                                        (int16_t)UI_TAB_ANIM_STEPS));
    } else {
        active_x = (int16_t)(TAB_X + 1u + (active_slot * TAB_SLOT_W));
    }
    indicator_x = (int16_t)(active_x + ((TAB_SLOT_W - TAB_INDICATOR_W) / 2u));

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawRFrame(u8g2,
                    TAB_X,
                    TAB_Y,
                    (u8g2_uint_t)(TAB_SLOT_W * visible_count),
                    TAB_H,
                    2u);
    u8g2_SetClipWindow(u8g2,
                       TAB_X,
                       TAB_Y,
                       (u8g2_uint_t)(TAB_X + (TAB_SLOT_W * visible_count)),
                       (u8g2_uint_t)(TAB_Y + TAB_H + 1u));

    for (uint8_t page_index = draw_first; page_index < draw_last; page_index++) {
        const int16_t x = (int16_t)(TAB_X + 1u +
                                    (((int16_t)page_index - (int16_t)from_first) * (int16_t)TAB_SLOT_W) +
                                    scroll_offset);
        const ui_page_t page = pages[page_index];
        const uint8_t label_width = (uint8_t)u8g2_GetStrWidth(u8g2, ui_page_to_label(page));
        const int16_t label_x = x + (int16_t)((TAB_SLOT_W - label_width) / 2u);
        const bool visible = x >= (int16_t)(TAB_X - 2u) &&
                             x <= (int16_t)(TAB_X + ((visible_count - 1u) * TAB_SLOT_W) + 2u);

        if (!visible) {
            continue;
        }

        if (s_ui.tab_anim == 0u && s_ui.page == page) {
            u8g2_DrawRBox(u8g2, (u8g2_uint_t)x, 2u, (u8g2_uint_t)(TAB_SLOT_W - 2u), 9u, 2u);
            u8g2_SetDrawColor(u8g2, 0);
            u8g2_DrawStr(u8g2, (u8g2_uint_t)label_x, 9u, ui_page_to_label(page));
            u8g2_SetDrawColor(u8g2, 1);
        } else {
            if (page_index > from_first) {
                const int16_t separator_x = (int16_t)(TAB_X +
                                                      (((int16_t)page_index - (int16_t)from_first) *
                                                       (int16_t)TAB_SLOT_W) +
                                                      scroll_offset);
                if (separator_x > (int16_t)TAB_X &&
                    separator_x < (int16_t)(TAB_X + (visible_count * TAB_SLOT_W))) {
                    u8g2_DrawVLine(u8g2, (u8g2_uint_t)separator_x, 3u, 7u);
                }
            }
            u8g2_DrawStr(u8g2, (u8g2_uint_t)label_x, 9u, ui_page_to_label(page));
        }
    }

    if (s_ui.tab_anim > 0u &&
        active_x >= (int16_t)TAB_X &&
        active_x <= (int16_t)(TAB_X + (visible_count * TAB_SLOT_W) - TAB_SLOT_W)) {
        u8g2_DrawRFrame(u8g2, (u8g2_uint_t)active_x, 2u, (u8g2_uint_t)(TAB_SLOT_W - 2u), 9u, 2u);
    }
    if (indicator_x >= (int16_t)TAB_X &&
        indicator_x <= (int16_t)(TAB_X + (visible_count * TAB_SLOT_W) - TAB_INDICATOR_W)) {
        u8g2_DrawBox(u8g2, (u8g2_uint_t)indicator_x, 11u, TAB_INDICATOR_W, 1u);
    }
    u8g2_SetMaxClipWindow(u8g2);

    if (to_first > 0u) {
        const uint8_t pulse = (uint8_t)(1u + ((s_ui.frame >> 1u) & 1u));
        u8g2_DrawTriangle(u8g2, 76u, 6u, 79u, (int16_t)(4u - pulse), 79u, (int16_t)(8u + pulse));
    }
    if ((uint8_t)(to_first + visible_count) < page_count) {
        const uint8_t pulse = (uint8_t)(1u + ((s_ui.frame >> 1u) & 1u));
        u8g2_DrawTriangle(u8g2, 177u, 6u, 174u, (int16_t)(4u - pulse), 174u, (int16_t)(8u + pulse));
    }
}

static void draw_header(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char build_buffer[12];
    char title_buffer[24];
    const bool blink = ((snapshot->uptime_ms / 250u) & 1u) != 0u;

    format_short_build(build_buffer, sizeof(build_buffer));

    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    snprintf(title_buffer,
             sizeof(title_buffer),
             "%s %s",
             trigger_mode_to_short(snapshot->trigger.active_mode),
             trigger_state_to_short(snapshot->trigger.state));
    draw_fit_str(u8g2, 7u, 13u, 66u, title_buffer);
    draw_page_tabs(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    snprintf(title_buffer,
             sizeof(title_buffer),
             "PIN%lu %s CNT%lu",
             (unsigned long)snapshot->trigger.trigger_source_pin,
             edge_to_short(snapshot->trigger.edge),
             (unsigned long)snapshot->trigger.trigger_count);
    draw_fit_str(u8g2, 8u, 20u, 154u, title_buffer);
    draw_fit_str(u8g2, 204u, 11u, 18u, snapshot->fault_active ? "FLT" : "OK");
    draw_status_dot(u8g2,
                    229u,
                    8u,
                    snapshot->fault_active || snapshot->trigger.state == TRIG_STATE_SEQ_ARMED ||
                        snapshot->trigger.state == TRIG_STATE_ENC_ARMED,
                    blink);
    u8g2_DrawStr(u8g2, 168, 19, build_buffer);
}

static void draw_system_card(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char uptime_buffer[12];
    char lock_buffer[12];
    char version_buffer[12];

    format_uptime(uptime_buffer, sizeof(uptime_buffer), snapshot->uptime_ms);
    format_resource_summary(lock_buffer, sizeof(lock_buffer), snapshot->arbiter.active_resources);
    snprintf(version_buffer,
             sizeof(version_buffer),
             "%lu.%lu.%lu",
             (unsigned long)PROJECT_VERSION_MAJOR,
             (unsigned long)PROJECT_VERSION_MINOR,
             (unsigned long)PROJECT_VERSION_PATCH);

    draw_card(u8g2, 4, UI_CARD_Y, 72, UI_CARD_H, "SYSTEM");
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 10, 42, snapshot->fault_active ? "FAULT" : "ONLINE");
    draw_kv_line(u8g2, 9, 52, 62, "UP", uptime_buffer);
    draw_kv_line(u8g2, 9, 62, 62, "VER", version_buffer);
    draw_kv_line(u8g2, 9, 72, 62, "MODE", arbiter_mode_to_short(snapshot->arbiter.mode));
    draw_kv_line(u8g2, 9, 82, 62, "LOCK", lock_buffer);
    draw_kv_line(u8g2, 9, 92, 62, "CAP", bool_to_run_stop(snapshot->arbiter.trigger_capture_running));
    draw_kv_line(u8g2, 9, 102, 62, "CLK", bool_to_run_stop(snapshot->arbiter.trigger_clock_running));
    draw_kv_line(u8g2, 9, 112, 62, "FLT", snapshot->fault_active ? "YES" : "NO");
}

static void draw_trigger_card(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char trigger_width_buffer[12];
    char pulse_width_buffer[12];
    char marker_width_buffer[12];
    char sample_rate_buffer[12];
    char clock_rate_buffer[12];
    char drop_buffer[12];

    format_duration_us(trigger_width_buffer, sizeof(trigger_width_buffer), snapshot->trigger.trigger_width_us);
    format_duration_us(pulse_width_buffer, sizeof(pulse_width_buffer), snapshot->trigger.pulse_width_us);
    format_duration_us(marker_width_buffer, sizeof(marker_width_buffer), snapshot->trigger.marker_width_us);
    format_freq_hz(sample_rate_buffer, sizeof(sample_rate_buffer), snapshot->trigger.capture_sample_hz);
    format_freq_hz(clock_rate_buffer, sizeof(clock_rate_buffer), snapshot->trigger.sync_clock_hz);
    snprintf(drop_buffer, sizeof(drop_buffer), "%lu", (unsigned long)snapshot->trigger.dropped_capture_words);

    draw_card(u8g2, 82, UI_CARD_Y, 76, UI_CARD_H, "TRIGGER");
    draw_kv_line(u8g2, 87, 42, 66, "INIT", snapshot->trigger.initialized ? "OK" : "WAIT");
    draw_kv_line(u8g2, 87, 52, 66, "IO", snapshot->trigger.io_initialized ? "OK" : "WAIT");
    draw_kv_line(u8g2, 87, 62, 66, "CAP", bool_to_run_stop(snapshot->trigger.capture_running));
    draw_kv_line(u8g2, 87, 72, 66, "CLK", bool_to_run_stop(snapshot->trigger.sync_clock_running));
    draw_kv_line(u8g2, 87, 82, 66, "SYNC", bool_to_on_off(snapshot->trigger.sync_clock_enabled));
    draw_kv_line(u8g2, 87, 92, 66, "TRIG", trigger_width_buffer);
    draw_kv_line(u8g2, 87, 102, 66, "PULS", pulse_width_buffer);
    draw_kv_line(u8g2, 87, 112, 66, "DROP", drop_buffer);
}

static void draw_ota_card(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char progress_buffer[16];    /* "%lu.%lu%%" 最大 ~14 chars */
    char rx_buffer[24];           /* "X.XM/X.XM" 最大 ~18 chars */
    char error_buffer[12];
    char seq_buffer[12];
    char event_buffer[12];

    format_progress(progress_buffer, sizeof(progress_buffer), snapshot->ota.progress_permille);
    format_rx_progress(rx_buffer,
                       sizeof(rx_buffer),
                       snapshot->ota.received_size,
                       snapshot->ota.expected_size);
    copy_compact_error(error_buffer, sizeof(error_buffer), snapshot->ota.error_code);
    snprintf(seq_buffer, sizeof(seq_buffer), "%lu", (unsigned long)snapshot->ota.sequence);
    snprintf(event_buffer, sizeof(event_buffer), "%lu", (unsigned long)snapshot->ota.last_event);

    draw_card(u8g2, 164, UI_CARD_Y, 72, UI_CARD_H, "OTA");
    draw_kv_line(u8g2, 169, 42, 62, "STATE", ota_state_to_short_label((ota_state_t)snapshot->ota.state));
    draw_kv_line(u8g2, 169, 51, 62, "PROG", progress_buffer);
    draw_kv_line(u8g2, 169, 60, 62, "RX", rx_buffer);
    draw_kv_line(u8g2, 169, 69, 62, "TARG", ota_slot_to_short_label(snapshot->ota.target_slot));
    draw_kv_line(u8g2, 169, 78, 62, "RES", ota_result_to_short_label((ota_result_t)snapshot->ota.last_result));
    draw_kv_line(u8g2, 169, 87, 62, "ERR", error_buffer);
    draw_kv_line(u8g2, 169, 96, 62, "BOOT", ota_boot_result_to_short_label(snapshot->ota.boot_flags_summary));
    draw_kv_line(u8g2, 169, 105, 62, "SEQ", seq_buffer);
    draw_kv_line(u8g2, 169, 112, 62, "EVT", event_buffer);
}

static void draw_footer(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char sample_rate_buffer[12];
    char clock_rate_buffer[12];
    char footer_left[24];
    char footer_right[24];
    u8g2_uint_t footer_right_width;
    u8g2_uint_t footer_right_x;

    format_freq_hz(sample_rate_buffer, sizeof(sample_rate_buffer), snapshot->trigger.capture_sample_hz);
    format_freq_hz(clock_rate_buffer, sizeof(clock_rate_buffer), snapshot->trigger.sync_clock_hz);
    snprintf(footer_left, sizeof(footer_left), "SAMP %s", sample_rate_buffer);
    snprintf(footer_right, sizeof(footer_right), "CLK %s", clock_rate_buffer);

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    footer_right_width = u8g2_GetStrWidth(u8g2, footer_right);
    footer_right_x = (u8g2_uint_t)(234u - footer_right_width);
    draw_fit_str(u8g2, 6u, UI_FOOTER_Y, (u8g2_uint_t)(footer_right_x - 10u), footer_left);
    u8g2_DrawStr(u8g2, footer_right_x, UI_FOOTER_Y, footer_right);

    footer_right_width = u8g2_GetStrWidth(u8g2, "UI TASK LIVE");
    footer_right_x = (u8g2_uint_t)(234u - footer_right_width);
    draw_fit_str(u8g2, 6u, 133u, (u8g2_uint_t)(footer_right_x - 10u),
                 snapshot->fault_active ? "DIAG FAULT ACTIVE" : "DIAG NOMINAL");
    u8g2_DrawStr(u8g2, footer_right_x, 133u, "UI TASK LIVE");
}

static void draw_trigger_page(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value_buffer[24];
    char clock_rate_buffer[12];
    char trigger_width_buffer[12];
    char pulse_width_buffer[12];
    const bool armed = snapshot->trigger.state == TRIG_STATE_SEQ_ARMED ||
                       snapshot->trigger.state == TRIG_STATE_ENC_ARMED;
    const bool blink = ((snapshot->uptime_ms / 250u) & 1u) != 0u;

    format_freq_hz(clock_rate_buffer, sizeof(clock_rate_buffer), snapshot->trigger.sync_clock_hz);
    format_duration_us(trigger_width_buffer, sizeof(trigger_width_buffer), snapshot->trigger.trigger_width_us);
    format_duration_us(pulse_width_buffer, sizeof(pulse_width_buffer), snapshot->trigger.pulse_width_us);

    draw_card(u8g2, 4u, 24u, 112u, 62u, "TRIGGER CORE");
    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    snprintf(value_buffer,
             sizeof(value_buffer),
             "%s %s",
             trigger_mode_to_short(snapshot->trigger.active_mode),
             trigger_state_to_short(snapshot->trigger.state));
    draw_fit_str(u8g2, 12u, 51u, 82u, value_buffer);
    draw_status_dot(u8g2, 101u, 45u, armed, blink);
    draw_kv_line(u8g2, 10u, 63u, 100u, "STATE", value_buffer);
    snprintf(value_buffer,
             sizeof(value_buffer),
             "GPIO%lu %s",
             (unsigned long)snapshot->trigger.trigger_source_pin,
             edge_to_short(snapshot->trigger.edge));
    draw_kv_line(u8g2, 10u, 72u, 100u, "IO", value_buffer);
    draw_kv_line(u8g2, 10u, 81u, 100u, "SAFE", snapshot->trigger.safe_state == TRIG_SAFE_ONE ? "ONE" : "ZERO");

    draw_card(u8g2, 122u, 24u, 114u, 62u, "LIVE COUNTS");
    snprintf(value_buffer, sizeof(value_buffer), "%lu", (unsigned long)snapshot->trigger.trigger_count);
    draw_kv_line(u8g2, 128u, 45u, 100u, "TRIG", value_buffer);
    draw_activity_bar(u8g2, 128u, 51u, 100u, snapshot->trigger.trigger_count % 100u, 100u);
    snprintf(value_buffer, sizeof(value_buffer), "%lu", (unsigned long)snapshot->trigger.output_count);
    draw_kv_line(u8g2, 128u, 66u, 100u, "OUT", value_buffer);
    snprintf(value_buffer, sizeof(value_buffer), "%lu", (unsigned long)snapshot->trigger.missed_count);
    draw_kv_line(u8g2, 128u, 74u, 100u, "MISS", value_buffer);
    snprintf(value_buffer, sizeof(value_buffer), "%lu", (unsigned long)snapshot->trigger.dropped_capture_words);
    draw_kv_line(u8g2, 128u, 82u, 100u, "DROP", value_buffer);

    draw_card(u8g2, 4u, 90u, 232u, 28u, "TIMING");
    draw_kv_line(u8g2, 11u, 110u, 62u, "TRIG", trigger_width_buffer);
    draw_kv_line(u8g2, 82u, 110u, 62u, "PULS", pulse_width_buffer);
    draw_kv_line(u8g2, 153u, 110u, 76u, "CLK", clock_rate_buffer);
}

static void draw_ota_page(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char progress_buffer[16];
    char rx_buffer[24];
    char value_buffer[24];
    char lock_buffer[16];
    const bool busy = snapshot->ota.state != OTA_STATE_IDLE &&
                      snapshot->ota.state != OTA_STATE_COMMITTED &&
                      snapshot->ota.state != OTA_STATE_FAILED &&
                      snapshot->ota.state != OTA_STATE_ABORTED;
    const bool blink = ((snapshot->uptime_ms / 250u) & 1u) != 0u;

    format_progress(progress_buffer, sizeof(progress_buffer), snapshot->ota.progress_permille);
    format_rx_progress(rx_buffer,
                       sizeof(rx_buffer),
                       snapshot->ota.received_size,
                       snapshot->ota.expected_size);
    format_resource_summary(lock_buffer, sizeof(lock_buffer), snapshot->arbiter.active_resources);

    draw_card(u8g2, 4u, 24u, 232u, 48u, "OTA FLOW");
    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    snprintf(value_buffer,
             sizeof(value_buffer),
             "%s %s",
             ota_state_to_short_label((ota_state_t)snapshot->ota.state),
             progress_buffer);
    draw_fit_str(u8g2, 12u, 51u, 196u, value_buffer);
    draw_status_dot(u8g2, 224u, 45u, busy, blink);
    draw_progress_bar(u8g2, 12u, 58u, 214u, 8u, snapshot->ota.progress_permille);

    draw_card(u8g2, 4u, 76u, 112u, 42u, "PACKAGE");
    draw_kv_line(u8g2, 10u, 97u, 100u, "TARG", ota_slot_to_short_label(snapshot->ota.target_slot));
    draw_kv_line(u8g2, 10u, 106u, 100u, "RX", rx_buffer);
    copy_compact_error(value_buffer, sizeof(value_buffer), snapshot->ota.error_code);
    draw_kv_line(u8g2, 10u, 115u, 100u, "ERR", value_buffer);

    draw_card(u8g2, 124u, 76u, 112u, 42u, "SYSTEM");
    draw_kv_line(u8g2, 130u, 97u, 100u, "RESULT", ota_result_to_short_label((ota_result_t)snapshot->ota.last_result));
    draw_kv_line(u8g2, 130u, 106u, 100u, "LOCK", lock_buffer);
    snprintf(value_buffer, sizeof(value_buffer), "%lu", (unsigned long)snapshot->ota.sequence);
    draw_kv_line(u8g2, 130u, 115u, 100u, "SEQ", value_buffer);
}

static void draw_sd_page(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value_buffer[32];
    char capacity_buffer[16];
    char block_buffer[16];
    char probe_buffer[16];
    const storage_manager_vector_t *storage = &snapshot->storage;
    const bool ready = storage->state == STORAGE_MANAGER_STATE_CARD_READY;
    const bool blink = ((snapshot->uptime_ms / 250u) & 1u) != 0u;

    format_kib_compact(capacity_buffer, sizeof(capacity_buffer), storage->capacity_kib);
    snprintf(block_buffer, sizeof(block_buffer), "%lu", (unsigned long)storage->block_count);
    snprintf(probe_buffer, sizeof(probe_buffer), "%lu", (unsigned long)storage->probe_count);

    draw_card(u8g2, 4u, 24u, 232u, 42u, "SD CARD");
    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    snprintf(value_buffer,
             sizeof(value_buffer),
             "%s %s",
             storage_manager_state_string(storage->state),
             sd_card_status_string(storage->card_status));
    draw_fit_str(u8g2, 12u, 51u, 196u, value_buffer);
    draw_status_dot(u8g2, 224u, 45u, ready, blink && storage->card_present);
    draw_kv_line(u8g2, 12u, 62u, 216u, "CARD", storage->card_present ? "PRESENT" : "ABSENT");

    draw_card(u8g2, 4u, 70u, 112u, 48u, "MEDIA");
    draw_kv_line(u8g2, 10u, 91u, 100u, "TYPE", sd_card_type_string(storage->card_type));
    draw_kv_line(u8g2, 10u, 101u, 100u, "CAP", capacity_buffer);
    draw_kv_line(u8g2, 10u, 111u, 100u, "BLK", block_buffer);

    draw_card(u8g2, 124u, 70u, 112u, 48u, "FILESYS");
    draw_kv_line(u8g2, 130u, 91u, 100u, "FATFS", storage->fatfs_available ? "YES" : "NO");
    draw_kv_line(u8g2, 130u, 101u, 100u, "MOUNT", storage->fs_mounted ? "YES" : "NO");
    draw_kv_line(u8g2, 130u, 111u, 100u, "PROBE", probe_buffer);
}

static void draw_body(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    switch (s_ui.page) {
    case UI_PAGE_TRIGGER:
        draw_trigger_page(u8g2, snapshot);
        break;
    case UI_PAGE_OTA:
        draw_ota_page(u8g2, snapshot);
        break;
    case UI_PAGE_SD:
        draw_sd_page(u8g2, snapshot);
        break;
    case UI_PAGE_OVERVIEW:
    default:
        draw_system_card(u8g2, snapshot);
        draw_trigger_card(u8g2, snapshot);
        draw_ota_card(u8g2, snapshot);
        break;
    }
}

static bool mono_pixel_is_set(uint16_t x, uint16_t y)
{
    const size_t byte_index = ((size_t)(y >> 3u) * UI_WIDTH) + x;
    const uint8_t bit_mask = (uint8_t)(1u << (y & 7u));
    return (s_ui.mono_buffer[byte_index] & bit_mask) != 0u;
}

static uint16_t themed_background(uint16_t x, uint16_t y)
{
    if (s_ui.boot_splash_active) {
        if (y < 28u) {
            return rgb565(8, 29, 39);
        }
        if (y > 104u) {
            return rgb565(19, 24, 34);
        }
        return (x < 120u) ? rgb565(11, 22, 32) : rgb565(14, 28, 34);
    }

    if (y < 20u) {
        switch (s_ui.page) {
        case UI_PAGE_TRIGGER:
            return rgb565(24, 35, 30);
        case UI_PAGE_OTA:
            return rgb565(35, 26, 39);
        case UI_PAGE_SD:
            return rgb565(20, 38, 32);
        case UI_PAGE_OVERVIEW:
        default:
            return rgb565(13, 36, 47);
        }
    }
    if (y >= 118u) {
        return rgb565(17, 27, 34);
    }
    if (s_ui.page == UI_PAGE_TRIGGER) {
        return (x < 120u) ? rgb565(14, 25, 23) : rgb565(18, 30, 24);
    }
    if (s_ui.page == UI_PAGE_OTA) {
        return (y < 70u) ? rgb565(22, 24, 38) : rgb565(28, 25, 34);
    }
    if (s_ui.page == UI_PAGE_SD) {
        return (y < 68u) ? rgb565(15, 34, 31) : rgb565(20, 31, 30);
    }
    if (x < 76u) {
        return rgb565(18, 30, 36);
    }
    if (x > 161u) {
        return rgb565(22, 32, 40);
    }
    return rgb565(15, 24, 31);
}

static uint16_t themed_foreground(uint16_t x, uint16_t y)
{
    if (s_ui.boot_splash_active) {
        if (y < 28u) {
            return rgb565(121, 237, 224);
        }
        if (y > 104u) {
            return rgb565(248, 194, 86);
        }
        if (x > 20u && x < 222u && y > 73u && y < 88u) {
            return rgb565(96, 219, 151);
        }
        return rgb565(225, 236, 232);
    }

    if (y < 20u) {
        switch (s_ui.page) {
        case UI_PAGE_TRIGGER:
            return rgb565(138, 238, 156);
        case UI_PAGE_OTA:
            return rgb565(211, 166, 255);
        case UI_PAGE_SD:
            return rgb565(124, 235, 189);
        case UI_PAGE_OVERVIEW:
        default:
            return rgb565(115, 232, 222);
        }
    }
    if (y >= 118u) {
        return rgb565(246, 185, 77);
    }
    if (s_ui.page == UI_PAGE_TRIGGER) {
        if (y > 45u && y < 55u) {
            return rgb565(247, 208, 92);
        }
        return rgb565(205, 239, 214);
    }
    if (s_ui.page == UI_PAGE_OTA) {
        if (y > 50u && y < 66u) {
            return rgb565(114, 218, 255);
        }
        return rgb565(234, 224, 246);
    }
    if (s_ui.page == UI_PAGE_SD) {
        if (y > 46u && y < 56u) {
            return rgb565(247, 212, 104);
        }
        return rgb565(217, 239, 226);
    }
    if ((x > 169u && x < 232u && y > 21u && y < 113u) ||
        (x > 80u && x < 158u && y > 21u && y < 83u)) {
        return rgb565(85, 211, 150);
    }
    return rgb565(220, 232, 229);
}

static void flush_to_lcd(void)
{
    lcd_st7789_set_window(0u, 0u, (uint16_t)(UI_WIDTH - 1u), (uint16_t)(UI_HEIGHT - 1u));

    size_t count = 0u;
    for (uint16_t y = 0u; y < UI_HEIGHT; y++) {
        for (uint16_t x = 0u; x < UI_WIDTH; x++) {
            s_ui.line_buffer[count++] = mono_pixel_is_set(x, y) ? themed_foreground(x, y) : themed_background(x, y);
            if (count == UI_FLUSH_PIXELS) {
                lcd_st7789_write_rgb565(s_ui.line_buffer, count);
                count = 0u;
            }
        }
    }

    if (count > 0u) {
        lcd_st7789_write_rgb565(s_ui.line_buffer, count);
    }
}

static void draw_boot_splash_frame(uint8_t frame)
{
    u8g2_t *u8g2 = &s_ui.u8g2;
    const uint8_t progress = (uint8_t)(((uint16_t)(frame + 1u) * 100u) / 16u);
    const uint8_t bar_width = (uint8_t)(((uint16_t)188u * progress) / 100u);
    const uint8_t sweep = (uint8_t)((frame * 13u) % UI_WIDTH);
    char text_buffer[24];

    u8g2_ClearBuffer(u8g2);

    for (uint8_t i = 0u; i < 5u; i++) {
        const uint8_t x = (uint8_t)((sweep + (i * 47u)) % UI_WIDTH);
        u8g2_DrawVLine(u8g2, x, 28u, 70u);
    }

    u8g2_DrawFrame(u8g2, 8u, 8u, 224u, 112u);
    u8g2_DrawFrame(u8g2, 11u, 11u, 218u, 106u);
    u8g2_DrawBox(u8g2, 18u, 18u, 204u, 1u);

    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    u8g2_DrawStr(u8g2, 44u, 44u, PROJECT_NAME);
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 55u, 57u, "HAOFV CONTROL CORE");

    u8g2_DrawFrame(u8g2, 24u, 75u, 192u, 10u);
    if (bar_width > 0u) {
        u8g2_DrawBox(u8g2, 26u, 77u, bar_width, 6u);
    }

    snprintf(text_buffer, sizeof(text_buffer), "BOOT %u%%", progress);
    u8g2_DrawStr(u8g2, 24u, 98u, text_buffer);
    snprintf(text_buffer,
             sizeof(text_buffer),
             "FW %lu.%lu.%lu",
             (unsigned long)PROJECT_VERSION_MAJOR,
             (unsigned long)PROJECT_VERSION_MINOR,
             (unsigned long)PROJECT_VERSION_PATCH);
    u8g2_DrawStr(u8g2, 166u, 98u, text_buffer);

    u8g2_DrawBox(u8g2, (u8g2_uint_t)(24u + (frame % 16u) * 12u), 109u, 18u, 2u);
}

static void run_boot_splash(void)
{
    if (!resource_arbiter_acquire(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                  RESOURCE_ARBITER_RESOURCE_LCD)) {
        return;
    }

    s_ui.boot_splash_active = true;
    for (uint8_t frame = 0u; frame < 16u; frame++) {
        draw_boot_splash_frame(frame);
        flush_to_lcd();
        board_service();
        osal_delay_ms(28u);
    }
    s_ui.boot_splash_active = false;

    resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_SPI0 |
                             RESOURCE_ARBITER_RESOURCE_LCD);
}

bool sync_config_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    u8g2_port_setup_240x136(&s_ui.u8g2, s_ui.mono_buffer);
    u8g2_SetFontMode(&s_ui.u8g2, 1);
    s_ui.page = UI_PAGE_OVERVIEW;
    s_ui.target_page = UI_PAGE_OVERVIEW;
    s_ui.previous_page = UI_PAGE_OVERVIEW;
    s_ui.tab_from_first = 0u;
    s_ui.tab_to_first = 0u;
    s_ui.initialized = true;
    run_boot_splash();
    return true;
}

void sync_config_ui_key_next(void)
{
    if (!s_ui.initialized) {
        return;
    }
    if (s_ui.tab_anim > 0u || s_ui.page != s_ui.target_page) {
        return;
    }

    s_ui.previous_page = s_ui.page;
    s_ui.tab_from_first = tab_first_for_page((uint8_t)s_ui.previous_page,
                                             (uint8_t)UI_PAGE_COUNT,
                                             3u);
    s_ui.target_page = (ui_page_t)(((uint32_t)s_ui.page + 1u) % (uint32_t)UI_PAGE_COUNT);
    s_ui.tab_to_first = tab_first_for_page((uint8_t)s_ui.target_page,
                                           (uint8_t)UI_PAGE_COUNT,
                                           3u);
    s_ui.tab_anim = UI_TAB_ANIM_STEPS;
}

bool sync_config_ui_render(void)
{
    ui_snapshot_t snapshot;

    if (!s_ui.initialized) {
        return false;
    }

    if (!resource_arbiter_acquire(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                  RESOURCE_ARBITER_RESOURCE_LCD)) {
        return false;
    }

    capture_snapshot(&snapshot);

    u8g2_t *u8g2 = &s_ui.u8g2;
    s_ui.frame++;
    if (s_ui.tab_anim == 0u && s_ui.page != s_ui.target_page) {
        s_ui.page = s_ui.target_page;
        s_ui.previous_page = s_ui.page;
    }

    u8g2_ClearBuffer(u8g2);
    draw_header(u8g2, &snapshot);
    draw_body(u8g2, &snapshot);
    draw_footer(u8g2, &snapshot);

    flush_to_lcd();
    if (s_ui.tab_anim > 0u) {
        s_ui.tab_anim--;
    }
    resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_SPI0 |
                             RESOURCE_ARBITER_RESOURCE_LCD);
    return true;
}

bool sync_config_ui_needs_render(void)
{
    return s_ui.initialized && (s_ui.tab_anim > 0u || s_ui.page != s_ui.target_page);
}
