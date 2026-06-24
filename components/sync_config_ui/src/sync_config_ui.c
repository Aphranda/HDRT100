#include "sync_config_ui.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "lcd_st7789.h"
#include "ota_ao.h"
#include "project_config.h"
#include "resource_arbiter.h"
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

typedef struct {
    sync_trigger_summary_t trigger;
    ota_vector_t ota;
    resource_arbiter_snapshot_t arbiter;
    bool fault_active;
    uint32_t uptime_ms;
} ui_snapshot_t;

typedef struct {
    u8g2_t u8g2;
    uint8_t mono_buffer[UI_MONO_BUFFER_SIZE];
    uint16_t line_buffer[UI_FLUSH_PIXELS];
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

static void draw_kv_line(u8g2_t *u8g2,
                         uint8_t x,
                         uint8_t y,
                         uint8_t w,
                         const char *label,
                         const char *value)
{
    const uint8_t right = (uint8_t)(x + w - 4u);

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, x, y, label);
    u8g2_DrawStr(u8g2,
                 (u8g2_uint_t)(right - u8g2_GetStrWidth(u8g2, value)),
                 y,
                 value);
}

static void draw_header(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char build_buffer[12];

    format_short_build(build_buffer, sizeof(build_buffer));

    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    u8g2_DrawStr(u8g2, 7, 13, PROJECT_NAME);
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 8, 20, "RUNTIME DASHBOARD");
    u8g2_DrawStr(u8g2, 168, 11, snapshot->fault_active ? "FAULT" : "READY");
    u8g2_DrawDisc(u8g2, 229, 8, 3, U8G2_DRAW_ALL);
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
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 10, 37, snapshot->fault_active ? "FAULT" : "ONLINE");
    draw_kv_line(u8g2, 9, 50, 62, "UP", uptime_buffer);
    draw_kv_line(u8g2, 9, 60, 62, "VER", version_buffer);
    draw_kv_line(u8g2, 9, 70, 62, "MODE", arbiter_mode_to_short(snapshot->arbiter.mode));
    draw_kv_line(u8g2, 9, 80, 62, "LOCK", lock_buffer);
    draw_kv_line(u8g2, 9, 90, 62, "CAP", bool_to_run_stop(snapshot->arbiter.trigger_capture_running));
    draw_kv_line(u8g2, 9, 100, 62, "CLK", bool_to_run_stop(snapshot->arbiter.trigger_clock_running));
    draw_kv_line(u8g2, 9, 110, 62, "FLT", snapshot->fault_active ? "YES" : "NO");
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
    draw_kv_line(u8g2, 87, 34, 66, "INIT", snapshot->trigger.initialized ? "OK" : "WAIT");
    draw_kv_line(u8g2, 87, 44, 66, "IO", snapshot->trigger.io_initialized ? "OK" : "WAIT");
    draw_kv_line(u8g2, 87, 54, 66, "CAP", bool_to_run_stop(snapshot->trigger.capture_running));
    draw_kv_line(u8g2, 87, 64, 66, "CLK", bool_to_run_stop(snapshot->trigger.sync_clock_running));
    draw_kv_line(u8g2, 87, 74, 66, "SYNC", bool_to_on_off(snapshot->trigger.sync_clock_enabled));
    draw_kv_line(u8g2, 87, 84, 66, "TRIG", trigger_width_buffer);
    draw_kv_line(u8g2, 87, 94, 66, "PULS", pulse_width_buffer);
    draw_kv_line(u8g2, 87, 104, 66, "MARK", marker_width_buffer);
    draw_kv_line(u8g2, 87, 114, 66, "DROP", drop_buffer);

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 86, 123, sample_rate_buffer);
    u8g2_DrawStr(u8g2,
                 (u8g2_uint_t)(155u - u8g2_GetStrWidth(u8g2, clock_rate_buffer)),
                 123,
                 clock_rate_buffer);
}

static void draw_ota_card(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char progress_buffer[12];
    char rx_buffer[16];
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
    draw_kv_line(u8g2, 169, 34, 62, "STATE", ota_state_to_short_label((ota_state_t)snapshot->ota.state));
    draw_kv_line(u8g2, 169, 44, 62, "PROG", progress_buffer);
    draw_kv_line(u8g2, 169, 54, 62, "RX", rx_buffer);
    draw_kv_line(u8g2, 169, 64, 62, "SLOT", ota_slot_to_short_label(snapshot->ota.target_slot));
    draw_kv_line(u8g2, 169, 74, 62, "RES", ota_result_to_short_label((ota_result_t)snapshot->ota.last_result));
    draw_kv_line(u8g2, 169, 84, 62, "ERR", error_buffer);
    draw_kv_line(u8g2, 169, 94, 62, "BOOT", ota_boot_result_to_short_label(snapshot->ota.boot_flags_summary));
    draw_kv_line(u8g2, 169, 104, 62, "SEQ", seq_buffer);
    draw_kv_line(u8g2, 169, 114, 62, "EVT", event_buffer);
}

static void draw_footer(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char sample_rate_buffer[12];
    char clock_rate_buffer[12];
    char footer_left[24];
    char footer_right[24];

    format_freq_hz(sample_rate_buffer, sizeof(sample_rate_buffer), snapshot->trigger.capture_sample_hz);
    format_freq_hz(clock_rate_buffer, sizeof(clock_rate_buffer), snapshot->trigger.sync_clock_hz);
    snprintf(footer_left, sizeof(footer_left), "SAMP %s", sample_rate_buffer);
    snprintf(footer_right, sizeof(footer_right), "CLK %s", clock_rate_buffer);

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 6, UI_FOOTER_Y, footer_left);
    u8g2_DrawStr(u8g2,
                 (u8g2_uint_t)(234u - u8g2_GetStrWidth(u8g2, footer_right)),
                 UI_FOOTER_Y,
                 footer_right);
    u8g2_DrawStr(u8g2, 6, 133, snapshot->fault_active ? "DIAG FAULT ACTIVE" : "DIAG NOMINAL");
    u8g2_DrawStr(u8g2,
                 (u8g2_uint_t)(234u - u8g2_GetStrWidth(u8g2, "UI TASK LIVE")),
                 133,
                 "UI TASK LIVE");
}

static bool mono_pixel_is_set(uint16_t x, uint16_t y)
{
    const size_t byte_index = ((size_t)(y >> 3u) * UI_WIDTH) + x;
    const uint8_t bit_mask = (uint8_t)(1u << (y & 7u));
    return (s_ui.mono_buffer[byte_index] & bit_mask) != 0u;
}

static uint16_t themed_background(uint16_t x, uint16_t y)
{
    if (y < 20u) {
        return rgb565(13, 36, 47);
    }
    if (y >= 118u) {
        return rgb565(17, 27, 34);
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
    if (y < 20u) {
        return rgb565(115, 232, 222);
    }
    if (y >= 118u) {
        return rgb565(246, 185, 77);
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

bool sync_config_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    u8g2_port_setup_240x136(&s_ui.u8g2, s_ui.mono_buffer);
    u8g2_SetFontMode(&s_ui.u8g2, 1);
    s_ui.initialized = true;
    return true;
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

    u8g2_ClearBuffer(u8g2);
    draw_header(u8g2, &snapshot);
    draw_system_card(u8g2, &snapshot);
    draw_trigger_card(u8g2, &snapshot);
    draw_ota_card(u8g2, &snapshot);
    draw_footer(u8g2, &snapshot);

    flush_to_lcd();
    resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_SPI0 |
                             RESOURCE_ARBITER_RESOURCE_LCD);
    return true;
}
