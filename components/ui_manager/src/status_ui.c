#include "status_ui.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "board_identity.h"
#include "diagnostics.h"
#include "drv_watchdog.h"
#include "lcd_st7789.h"
#include "led_manager.h"
#include "osal.h"
#include "ota_ao.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "storage_manager.h"
#include "sync_trigger.h"
#include "u8g2.h"
#include "u8g2_port.h"
#include "vdc_dpll_manager.h"

#define UI_WIDTH 160u
#define UI_HEIGHT 80u
#define UI_U8G2_HEIGHT 80u
#define UI_MONO_BUFFER_SIZE ((UI_WIDTH * UI_U8G2_HEIGHT) / 8u)
#define UI_FLUSH_PIXELS 120u
#define UI_CARD_Y 22u
#define UI_CARD_H 92u
#define UI_FOOTER_Y 124u
#define UI_TAB_ANIM_STEPS 4u

typedef enum {
    UI_PAGE_OVERVIEW = 0,
    UI_PAGE_SYNC,
    UI_PAGE_VDC,
    UI_PAGE_TRIGGER,
    UI_PAGE_SYSTEM,
    UI_PAGE_HEALTH,
    UI_PAGE_COUNT,
} ui_page_t;

enum {
    UI_OVERVIEW_TDMA = 1u << 0,
    UI_OVERVIEW_VDC = 1u << 1,
    UI_OVERVIEW_DPLL = 1u << 2,
    UI_OVERVIEW_TRIGGER = 1u << 3,
    UI_OVERVIEW_SD = 1u << 4,
    UI_OVERVIEW_FAULT = 1u << 5,
};

typedef struct {
    sync_trigger_summary_t trigger;
    ota_vector_t ota;
    storage_manager_vector_t storage;
    resource_arbiter_snapshot_t arbiter;
    tdma_service_snapshot_t tdma;
    vdc_domain_snapshot_t vdc;
    diagnostics_status_t diagnostics;
    diagnostics_core_status_t core;
    led_manager_status_t led;
    uint32_t heap_free_bytes;
    uint32_t heap_min_free_bytes;
    bool tdma_valid;
    bool vdc_valid;
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
    uint8_t subpage;
    uint8_t overview_active_mask;
    uint8_t overview_fault_mask;
    bool cover_active;
    bool cover_fault_active;
    bool boot_splash_active;
    bool initialized;
} status_ui_t;

static status_ui_t s_ui;
/* The aggregate snapshot grows with TDMA/VDC observability.  Keep it in the
 * component's static RAM instead of consuming the UI task stack on every
 * render. */
static ui_snapshot_t s_snapshot;

/* Official GTS symbol, rasterized from the four #D60037 paths in
 * docs/reports/distributed-trigger/...0804.html <g id="gts-logo">. */
static const uint8_t s_gts_logo_xbm[] = {
    0x00u, 0x00u, 0x00u, 0x1Eu, 0x80u, 0x01u, 0x7Fu, 0xE0u, 0x03u, 0xFFu, 0xF1u, 0x03u,
    0xFFu, 0xE7u, 0x03u, 0xFFu, 0xDFu, 0x03u, 0xFFu, 0x3Fu, 0x03u, 0xFFu, 0x7Fu, 0x00u,
    0xFEu, 0xFFu, 0x00u, 0xFEu, 0x1Fu, 0x00u, 0xFEu, 0xE0u, 0x02u, 0x0Cu, 0x7Cu, 0x07u,
    0x80u, 0x3Fu, 0x07u, 0xF0u, 0x9Fu, 0x07u, 0xFCu, 0xCFu, 0x07u, 0xFCu, 0xE7u, 0x0Fu,
    0xFCu, 0xF3u, 0x0Fu, 0xFCu, 0xF1u, 0x0Fu, 0xFCu, 0xF8u, 0x0Fu, 0x7Cu, 0xF0u, 0x07u,
    0x18u, 0x80u, 0x03u, 0x00u, 0x00u, 0x00u,
};

static uint8_t ui_subpage_count(ui_page_t page)
{
    switch (page) {
    case UI_PAGE_SYNC:
        return 4u;
    case UI_PAGE_VDC:
    case UI_PAGE_HEALTH:
        return 3u;
    case UI_PAGE_SYSTEM:
        return 4u;
    case UI_PAGE_OVERVIEW:
    case UI_PAGE_TRIGGER:
    default:
        return 2u;
    }
}

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
    case UI_PAGE_SYNC:
        return "SYN";
    case UI_PAGE_VDC:
        return "VDC";
    case UI_PAGE_TRIGGER:
        return "TRG";
    case UI_PAGE_SYSTEM:
        return "SYS";
    case UI_PAGE_HEALTH:
        return "HLT";
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

static const char *vdc_lock_to_short(uint32_t state)
{
    switch ((vdc_domain_lock_state_t)state) {
    case VDC_DOMAIN_LOCK_OFF:
        return "OFF";
    case VDC_DOMAIN_LOCK_CHECKING:
        return "CHECK";
    case VDC_DOMAIN_LOCK_INITIAL_SYNC:
        return "INIT";
    case VDC_DOMAIN_LOCK_FREQ_LOCK:
        return "FREQ";
    case VDC_DOMAIN_LOCK_PHASE_LOCK:
        return "PHASE";
    case VDC_DOMAIN_LOCK_LOCKED:
        return "LOCK";
    case VDC_DOMAIN_LOCK_HOLDOVER:
        return "HOLD";
    case VDC_DOMAIN_LOCK_RELOCKING:
        return "RELOCK";
    case VDC_DOMAIN_LOCK_FAULT:
        return "FAULT";
    default:
        return "UNK";
    }
}

static const char *vdc_health_to_short(uint32_t state)
{
    switch ((vdc_domain_health_state_t)state) {
    case VDC_DOMAIN_HEALTH_CHECKING:
        return "CHECK";
    case VDC_DOMAIN_HEALTH_DEGRADED:
        return "DEGR";
    case VDC_DOMAIN_HEALTH_LOCK_CANDIDATE:
        return "CAND";
    case VDC_DOMAIN_HEALTH_HEALTHY:
        return "OK";
    case VDC_DOMAIN_HEALTH_FAULT:
        return "FAULT";
    case VDC_DOMAIN_HEALTH_UNKNOWN:
    default:
        return "UNK";
    }
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
    case OTA_BOOT_RESULT_SLOT_RANGE_INVALID:
        return "RANGE";
    case OTA_BOOT_RESULT_VECTOR_INVALID:
        return "VECTOR";
    case OTA_BOOT_RESULT_IMAGE_CRC_INVALID:
        return "CRC";
    case OTA_BOOT_RESULT_IMAGE_HASH_INVALID:
        return "HASH";
    case OTA_BOOT_RESULT_SIGNATURE_INVALID:
        return "SIGN";
    case OTA_BOOT_RESULT_COMPATIBILITY_INVALID:
        return "COMPAT";
    case OTA_BOOT_RESULT_RECOVERY_UNAVAILABLE:
        return "RECOV";
    case OTA_BOOT_RESULT_SLOT_EMPTY:
        return "EMPTY";
    default:
        return "UNK";
    }
}

static const char *storage_manifest_to_short(storage_manager_manifest_status_t status)
{
    switch (status) {
    case STORAGE_MANAGER_MANIFEST_OK:
        return "OK";
    case STORAGE_MANAGER_MANIFEST_NOT_FOUND:
        return "NOIDX";
    case STORAGE_MANAGER_MANIFEST_INVALID:
        return "BAD";
    case STORAGE_MANAGER_MANIFEST_SCHEMA_UNSUPPORTED:
        return "SCHEMA";
    case STORAGE_MANAGER_MANIFEST_PRODUCT_MISMATCH:
        return "PROD";
    case STORAGE_MANAGER_MANIFEST_HARDWARE_MISMATCH:
        return "HW";
    case STORAGE_MANAGER_MANIFEST_REQUIRED_MISSING:
        return "MISS";
    case STORAGE_MANAGER_MANIFEST_IO_ERROR:
        return "IO";
    case STORAGE_MANAGER_MANIFEST_PATH_DENIED:
        return "DENY";
    case STORAGE_MANAGER_MANIFEST_UNKNOWN:
    default:
        return "UNK";
    }
}

static const char *storage_job_to_short(storage_manager_job_type_t type)
{
    switch (type) {
    case STORAGE_MANAGER_JOB_TYPE_FILE_INFO:
        return "INFO";
    case STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE:
        return "SNAP";
    case STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN:
        return "MAN";
    case STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE:
        return "FAULT";
    case STORAGE_MANAGER_JOB_TYPE_NONE:
    default:
        return "NONE";
    }
}

static const char *storage_job_state_to_short(storage_manager_job_state_t state)
{
    switch (state) {
    case STORAGE_MANAGER_JOB_STATE_QUEUED:
        return "QUEUE";
    case STORAGE_MANAGER_JOB_STATE_RUNNING:
        return "RUN";
    case STORAGE_MANAGER_JOB_STATE_DONE:
        return "DONE";
    case STORAGE_MANAGER_JOB_STATE_FAILED:
        return "FAIL";
    case STORAGE_MANAGER_JOB_STATE_IDLE:
    default:
        return "IDLE";
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

    if ((resources & RESOURCE_ARBITER_RESOURCE_SD) != 0u) {
        snprintf(buffer, buffer_size, "SPI1+SD");
        return;
    }

    if ((resources & RESOURCE_ARBITER_RESOURCE_PIO2) != 0u &&
        (resources & RESOURCE_ARBITER_RESOURCE_AUX) != 0u) {
        snprintf(buffer, buffer_size, "PIO2+AUX");
        return;
    }

    if ((resources & RESOURCE_ARBITER_RESOURCE_PIO1) != 0u &&
        (resources & RESOURCE_ARBITER_RESOURCE_AUX) != 0u) {
        snprintf(buffer, buffer_size, "PIO1+AUX");
        return;
    }

    snprintf(buffer, buffer_size, "0x%03lX", (unsigned long)resources);
}

static void capture_snapshot(ui_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    drv_watchdog_mark_progress(0u, 0x0A11u);
    sync_trigger_get_summary(&snapshot->trigger);
    drv_watchdog_mark_progress(0u, 0x0A12u);
    ota_ao_get_vector(&snapshot->ota);
    drv_watchdog_mark_progress(0u, 0x0A13u);
    storage_manager_get_vector(&snapshot->storage);
    drv_watchdog_mark_progress(0u, 0x0A14u);
    resource_arbiter_get_snapshot(&snapshot->arbiter);
    drv_watchdog_mark_progress(0u, 0x0A15u);
    snapshot->tdma_valid = vdc_dpll_manager_get_tdma_snapshot(&snapshot->tdma);
    drv_watchdog_mark_progress(0u, 0x0A16u);
    snapshot->vdc_valid = vdc_dpll_manager_get_snapshot(&snapshot->vdc);
    drv_watchdog_mark_progress(0u, 0x0A17u);
    diagnostics_get_status(&snapshot->diagnostics);
    drv_watchdog_mark_progress(0u, 0x0A18u);
    diagnostics_get_core_status(&snapshot->core);
    led_manager_get_status(&snapshot->led);
    drv_watchdog_mark_progress(0u, 0x0A19u);
    osal_heap_get_status(&snapshot->heap_free_bytes, &snapshot->heap_min_free_bytes);
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

static void draw_progress_card(u8g2_t *u8g2,
                               uint8_t x,
                               uint8_t y,
                               uint8_t w,
                               uint8_t h,
                               const char *title,
                               uint32_t permille)
{
    const uint32_t bounded = permille > 1000u ? 1000u : permille;
    const uint8_t inner_w = (uint8_t)(w - 2u);
    const uint8_t fill_w = (uint8_t)(((uint32_t)inner_w * bounded) / 1000u);

    u8g2_DrawFrame(u8g2, x, y, w, h);
    if (fill_w > 0u) {
        u8g2_DrawBox(u8g2,
                     (u8g2_uint_t)(x + 1u),
                     (u8g2_uint_t)(y + 1u),
                     fill_w,
                     11u);
    }
    u8g2_DrawHLine(u8g2,
                   (u8g2_uint_t)(x + 1u),
                   (u8g2_uint_t)(y + 12u),
                   inner_w);
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    /* XOR keeps the title readable across both the filled and unfilled part
     * of the progress header. */
    u8g2_SetDrawColor(u8g2, 2u);
    u8g2_DrawStr(u8g2, (u8g2_uint_t)(x + 4u), (u8g2_uint_t)(y + 9u), title);
    u8g2_SetDrawColor(u8g2, 1u);
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

static u8g2_uint_t tracked_str_width(u8g2_t *u8g2,
                                     const char *text,
                                     uint8_t tracked_chars,
                                     uint8_t spacing)
{
    u8g2_uint_t width = 0u;
    const size_t len = strlen(text);

    for (size_t i = 0u; i < len; i++) {
        char glyph[2] = {text[i], '\0'};
        width = (u8g2_uint_t)(width + u8g2_GetStrWidth(u8g2, glyph));
        if (i + 1u < len && i + 1u < tracked_chars) {
            width = (u8g2_uint_t)(width + spacing);
        }
    }

    return width;
}

static void draw_tracked_str(u8g2_t *u8g2,
                             u8g2_uint_t x,
                             u8g2_uint_t y,
                             const char *text,
                             uint8_t tracked_chars,
                             uint8_t spacing)
{
    const size_t len = strlen(text);

    for (size_t i = 0u; i < len; i++) {
        char glyph[2] = {text[i], '\0'};
        u8g2_DrawStr(u8g2, x, y, glyph);
        x = (u8g2_uint_t)(x + u8g2_GetStrWidth(u8g2, glyph));
        if (i + 1u < len && i + 1u < tracked_chars) {
            x = (u8g2_uint_t)(x + spacing);
        }
    }
}

static void draw_centered_tracked_str(u8g2_t *u8g2,
                                      u8g2_uint_t left,
                                      u8g2_uint_t width,
                                      u8g2_uint_t y,
                                      const char *text,
                                      uint8_t tracked_chars,
                                      uint8_t spacing)
{
    const u8g2_uint_t text_width = tracked_str_width(u8g2, text, tracked_chars, spacing);
    const u8g2_uint_t x = text_width < width ?
                              (u8g2_uint_t)(left + ((width - text_width) / 2u)) :
                              left;

    draw_tracked_str(u8g2, x, y, text, tracked_chars, spacing);
}

static void draw_tracked_title_with_breath(u8g2_t *u8g2,
                                           u8g2_uint_t left,
                                           u8g2_uint_t width,
                                           u8g2_uint_t text_y,
                                           u8g2_uint_t breath_y,
                                           const char *title,
                                           uint8_t tracked_chars,
                                           uint8_t spacing,
                                           uint8_t breath_radius)
{
    const u8g2_uint_t text_width = tracked_str_width(u8g2, title, tracked_chars, spacing);
    const u8g2_uint_t breath_outer_radius = 3u;
    const u8g2_uint_t title_to_breath_gap = 5u;
    const u8g2_uint_t title_x = text_width < width ?
                                    (u8g2_uint_t)(left + ((width - text_width) / 2u)) :
                                    left;
    const u8g2_uint_t breath_x =
        (u8g2_uint_t)(title_x + text_width + title_to_breath_gap + breath_outer_radius);

    draw_tracked_str(u8g2, title_x, text_y, title, tracked_chars, spacing);
    u8g2_DrawCircle(u8g2, breath_x, breath_y, breath_outer_radius, U8G2_DRAW_ALL);
    u8g2_DrawDisc(u8g2, breath_x, breath_y, breath_radius, U8G2_DRAW_ALL);
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
        UI_PAGE_SYNC,
        UI_PAGE_VDC,
        UI_PAGE_TRIGGER,
        UI_PAGE_SYSTEM,
        UI_PAGE_HEALTH,
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

static void __attribute__((unused)) draw_header(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
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

static void __attribute__((unused)) draw_system_card(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char uptime_buffer[12];
    char lock_buffer[12];
    char conflict_buffer[12];
    char version_buffer[12];

    format_uptime(uptime_buffer, sizeof(uptime_buffer), snapshot->uptime_ms);
    format_resource_summary(lock_buffer, sizeof(lock_buffer), snapshot->arbiter.active_resources);
    format_resource_summary(conflict_buffer,
                            sizeof(conflict_buffer),
                            snapshot->arbiter.last_conflict_resources);
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
    draw_kv_line(u8g2, 9, 112, 62, "CNF", conflict_buffer);
}

static void __attribute__((unused)) draw_trigger_card(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char trigger_width_buffer[12];
    char pulse_width_buffer[12];
    char rj45_width_buffer[12];

    format_duration_us(trigger_width_buffer, sizeof(trigger_width_buffer), snapshot->trigger.trigger_width_us);
    format_duration_us(pulse_width_buffer, sizeof(pulse_width_buffer), snapshot->trigger.pulse_width_us);
    format_duration_us(rj45_width_buffer, sizeof(rj45_width_buffer), snapshot->trigger.rj45_trigger_width_us);

    draw_card(u8g2, 82, UI_CARD_Y, 76, UI_CARD_H, "TRIGGER");
    draw_kv_line(u8g2, 87, 42, 66, "INIT", snapshot->trigger.initialized ? "OK" : "WAIT");
    draw_kv_line(u8g2, 87, 52, 66, "IO", snapshot->trigger.io_initialized ? "OK" : "WAIT");
    draw_kv_line(u8g2, 87, 62, 66, "CAP", bool_to_run_stop(snapshot->trigger.capture_running));
    draw_kv_line(u8g2, 87, 72, 66, "CLK", bool_to_run_stop(snapshot->trigger.sync_clock_running));
    draw_kv_line(u8g2, 87, 82, 66, "SYNC", bool_to_on_off(snapshot->trigger.sync_clock_enabled));
    draw_kv_line(u8g2, 87, 92, 66, "TRIG", trigger_width_buffer);
    draw_kv_line(u8g2, 87, 102, 66, "PULS", pulse_width_buffer);
    draw_kv_line(u8g2, 87, 112, 66, "RJ45", rj45_width_buffer);
}

static void __attribute__((unused)) draw_ota_card(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
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

static void __attribute__((unused)) draw_footer(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
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

static void __attribute__((unused)) draw_trigger_page(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
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

static void __attribute__((unused)) draw_ota_page(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
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

static void __attribute__((unused)) draw_sd_page(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value_buffer[32];
    char capacity_buffer[16];
    char manifest_buffer[24];
    char job_buffer[24];
    char fault_buffer[24];
    const storage_manager_vector_t *storage = &snapshot->storage;
    const bool ready = storage->state == STORAGE_MANAGER_STATE_CARD_READY;
    const bool blink = ((snapshot->uptime_ms / 250u) & 1u) != 0u;
    const bool manifest_ok = storage->manifest_status == STORAGE_MANAGER_MANIFEST_OK &&
                             storage->manifest_missing_count == 0u;

    format_kib_compact(capacity_buffer, sizeof(capacity_buffer), storage->capacity_kib);
    snprintf(manifest_buffer,
             sizeof(manifest_buffer),
             "%s %lu/%lu",
             storage_manifest_to_short(storage->manifest_status),
             (unsigned long)storage->manifest_missing_count,
             (unsigned long)storage->manifest_required_count);
    snprintf(job_buffer,
             sizeof(job_buffer),
             "%s %s",
             storage_job_to_short(storage->current_job_type),
             storage_job_state_to_short(storage->current_job_state));
    snprintf(fault_buffer,
             sizeof(fault_buffer),
             "S%lu T%lu",
             (unsigned long)storage->last_fault_snapshot_id,
             (unsigned long)storage->last_fault_trace_id);

    draw_card(u8g2, 4u, 24u, 232u, 42u, "SD CARD");
    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    snprintf(value_buffer,
             sizeof(value_buffer),
             "%s %s",
             storage_manager_state_string(storage->state),
             sd_card_status_string(storage->card_status));
    draw_fit_str(u8g2, 12u, 51u, 196u, value_buffer);
    draw_status_dot(u8g2, 224u, 45u, ready, blink && storage->card_present);
    snprintf(value_buffer,
             sizeof(value_buffer),
             "%s %s %s",
             storage->card_present ? "CARD" : "NOCRD",
             storage->fs_mounted ? "MNT" : "NOMNT",
             capacity_buffer);
    draw_kv_line(u8g2, 12u, 62u, 216u, "MEDIA", value_buffer);

    draw_card(u8g2, 4u, 70u, 112u, 48u, "SYSTEM PACK");
    draw_kv_line(u8g2, 10u, 91u, 100u, "MAN", manifest_buffer);
    draw_kv_line(u8g2, 10u, 101u, 100u, "OTA", manifest_ok ? "DEFAULT" : "CHECK");
    draw_kv_line(u8g2, 10u, 111u, 100u, "JOB", job_buffer);

    draw_card(u8g2, 124u, 70u, 112u, 48u, "EVIDENCE");
    snprintf(value_buffer, sizeof(value_buffer), "%lu", (unsigned long)storage->last_snapshot_id);
    draw_kv_line(u8g2, 130u, 91u, 100u, "SNAP", value_buffer);
    draw_kv_line(u8g2, 130u, 101u, 100u, "FAULT", fault_buffer);
    snprintf(value_buffer, sizeof(value_buffer), "%lu/%lu",
             (unsigned long)storage->storage_error,
             (unsigned long)storage->job_error);
    draw_kv_line(u8g2, 130u, 111u, 100u, "ERR", value_buffer);
}

/* Product-panel layout: one 156x52 card on a 160x80 canvas.  The legacy
 * 240x135 multi-card renderers above remain as formatting references while
 * product bring-up converges, but are no longer selected by draw_body(). */
static void draw_product_header(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    static const char *const labels[] = {"OVR", "SYN", "VDC", "TRG", "SYS", "HLT"};
    const uint8_t tab_x = 40u;
    /* Keep a clear 28-pixel identity area at the upper-right.  The previous
     * 6x20 tab layout occupied the whole row and painted over NO.n. */
    const uint8_t tab_w = 16u;
    const uint8_t active = (uint8_t)(s_ui.tab_anim > 0u ? s_ui.target_page : s_ui.page);
    const uint8_t phase = (uint8_t)((snapshot->uptime_ms / 100u) % 20u);
    const uint8_t triangle = phase <= 10u ? phase : (uint8_t)(20u - phase);
    const uint8_t breath_radius = (uint8_t)(1u + (triangle / 5u));

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 1u, 10u, "DHRT100");
    u8g2_DrawCircle(u8g2, 35u, 6u, 3u, U8G2_DRAW_ALL);
    u8g2_DrawDisc(u8g2, 35u, 6u, breath_radius, U8G2_DRAW_ALL);
    for (uint8_t page = 0u; page < (uint8_t)UI_PAGE_COUNT; page++) {
        const uint8_t x = (uint8_t)(tab_x + (page * tab_w));
        if (page == active) {
            u8g2_DrawBox(u8g2, x, 1u, tab_w, 11u);
            u8g2_SetDrawColor(u8g2, 0u);
        } else {
            u8g2_DrawFrame(u8g2, x, 1u, tab_w, 11u);
        }
        draw_fit_str(u8g2, (u8g2_uint_t)(x + 2u), 9u, (u8g2_uint_t)(tab_w - 3u), labels[page]);
        u8g2_SetDrawColor(u8g2, 1u);
    }

    /* Draw the logical board number last so it cannot be hidden by tabs. */
    char no_label[8];
    uint8_t logical_no = board_identity_get_no();
    if (logical_no == 0u && snapshot->tdma_valid &&
        snapshot->tdma.ring_local_slot_id < BOARD_IDENTITY_MAX_NODES) {
        logical_no = (uint8_t)(snapshot->tdma.ring_local_slot_id + 1u);
    }
    if (logical_no != 0u) {
        snprintf(no_label, sizeof(no_label), "NO.%u", (unsigned int)logical_no);
        draw_fit_str(u8g2, 137u, 10u, 23u, no_label);
    }
}

static void draw_product_cover(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char build[12];
    char status[24];
    char no_label[8];
    const uint8_t phase = (uint8_t)((snapshot->uptime_ms / 100u) % 20u);
    const uint8_t triangle = phase <= 10u ? phase : (uint8_t)(20u - phase);
    const uint8_t breath_radius = (uint8_t)(1u + (triangle / 5u));

    s_ui.cover_fault_active = snapshot->fault_active || snapshot->led.fault_latched;
    u8g2_DrawFrame(u8g2, 2u, 2u, 156u, 76u);
    u8g2_DrawFrame(u8g2, 4u, 4u, 152u, 72u);
    u8g2_DrawXBMP(u8g2, 18u, 8u, 20u, 22u, s_gts_logo_xbm);
    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    draw_tracked_title_with_breath(u8g2,
                                   0u,
                                   UI_WIDTH,
                                   22u,
                                   17u,
                                   "DHRT100",
                                   4u,
                                   2u,
                                   breath_radius);
    uint8_t logical_no = board_identity_get_no();
    if (logical_no == 0u && snapshot->tdma_valid &&
        snapshot->tdma.ring_local_slot_id < BOARD_IDENTITY_MAX_NODES) {
        logical_no = (uint8_t)(snapshot->tdma.ring_local_slot_id + 1u);
    }
    if (logical_no != 0u) {
        snprintf(no_label, sizeof(no_label), "NO.%u", (unsigned int)logical_no);
        u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
        draw_fit_str(u8g2, 127u, 14u, 27u, no_label);
    }
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 15u, 39u, "DISTRIBUTED HARD REAL-TIME");
    u8g2_DrawStr(u8g2, 45u, 48u, "TRIGGER SYSTEM");
    u8g2_DrawHLine(u8g2, 12u, 53u, 136u);
    format_short_build(build, sizeof(build));
    snprintf(status,
             sizeof(status),
             "%s  FW %s",
             s_ui.cover_fault_active ? "FAULT" : "READY",
             build);
    draw_fit_str(u8g2, 37u, 64u, 90u, status);
    u8g2_DrawStr(u8g2, 53u, 73u, "PRESS ANY KEY");
}

static void draw_overview_status_dot(u8g2_t *u8g2,
                                     uint8_t x,
                                     uint8_t y,
                                     bool active,
                                     bool blink)
{
    if (active || blink) {
        u8g2_DrawDisc(u8g2, x, y, active ? 2u : 1u, U8G2_DRAW_ALL);
    } else {
        u8g2_DrawCircle(u8g2, x, y, 2u, U8G2_DRAW_ALL);
    }
}

static void draw_product_overview(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value[32];
    char uptime[12];
    char build[12];
    const bool blink = ((snapshot->uptime_ms / 250u) & 1u) != 0u;

    s_ui.overview_active_mask = UI_OVERVIEW_FAULT;
    s_ui.overview_fault_mask = 0u;
    if (snapshot->tdma_valid && snapshot->tdma.ring_adapter_started != 0u) {
        s_ui.overview_active_mask |= UI_OVERVIEW_TDMA;
    }
    if (snapshot->tdma_valid &&
        (snapshot->tdma.ring_adapter_last_error != 0u ||
         snapshot->tdma.ring_last_error != 0u)) {
        s_ui.overview_fault_mask |= UI_OVERVIEW_TDMA;
    }
    if (snapshot->vdc_valid && snapshot->vdc.ready != 0u) {
        s_ui.overview_active_mask |= UI_OVERVIEW_VDC;
    }
    if (snapshot->vdc_valid &&
        snapshot->vdc.quality.health_state == VDC_DOMAIN_HEALTH_FAULT) {
        s_ui.overview_fault_mask |= UI_OVERVIEW_VDC;
    }
    if (snapshot->vdc_valid && snapshot->vdc.dpll.state == VDC_DOMAIN_LOCK_LOCKED) {
        s_ui.overview_active_mask |= UI_OVERVIEW_DPLL;
    }
    if (snapshot->vdc_valid && snapshot->vdc.dpll.state == VDC_DOMAIN_LOCK_FAULT) {
        s_ui.overview_fault_mask |= UI_OVERVIEW_DPLL;
    }
    if (snapshot->trigger.state != TRIG_STATE_IDLE) {
        s_ui.overview_active_mask |= UI_OVERVIEW_TRIGGER;
    }
    if (snapshot->trigger.state == TRIG_STATE_FAULT) {
        s_ui.overview_fault_mask |= UI_OVERVIEW_TRIGGER;
    }
    if (snapshot->storage.card_present && snapshot->storage.fs_mounted) {
        s_ui.overview_active_mask |= UI_OVERVIEW_SD;
    }
    if ((snapshot->storage.card_present && !snapshot->storage.fs_mounted) ||
        snapshot->storage.storage_error != 0u ||
        snapshot->storage.job_error != 0u) {
        s_ui.overview_fault_mask |= UI_OVERVIEW_SD;
    }
    if (snapshot->fault_active) {
        s_ui.overview_fault_mask |= UI_OVERVIEW_FAULT;
    }

    draw_card(u8g2, 2u, 15u, 156u, 52u, s_ui.subpage > 0u ? "OVERVIEW SYSTEM" : "OVERVIEW STATUS");
    if (s_ui.subpage > 0u) {
        format_uptime(uptime, sizeof(uptime), snapshot->uptime_ms);
        draw_kv_line(u8g2, 7u, 36u, 148u, "UPTIME", uptime);
        format_short_build(build, sizeof(build));
        draw_kv_line(u8g2, 7u, 45u, 148u, "FIRMWARE", build);
        snprintf(value,
                 sizeof(value),
                 "%s 0x%03lX",
                 arbiter_mode_to_short(snapshot->arbiter.mode),
                 (unsigned long)snapshot->arbiter.active_resources);
        draw_kv_line(u8g2, 7u, 54u, 148u, "RESOURCE", value);
        snprintf(value,
                 sizeof(value),
                 "%s E%lu/%lu",
                 snapshot->fault_active ? "FAULT" : "OK",
                 (unsigned long)snapshot->storage.storage_error,
                 (unsigned long)snapshot->storage.job_error);
        draw_kv_line(u8g2, 7u, 63u, 148u, "HEALTH", value);
        return;
    }

    draw_overview_status_dot(u8g2, 10u, 38u,
                    (s_ui.overview_active_mask & UI_OVERVIEW_TDMA) != 0u,
                    blink && (s_ui.overview_fault_mask & UI_OVERVIEW_TDMA) != 0u);
    draw_overview_status_dot(u8g2, 60u, 38u,
                    (s_ui.overview_active_mask & UI_OVERVIEW_VDC) != 0u,
                    blink && (s_ui.overview_fault_mask & UI_OVERVIEW_VDC) != 0u);
    draw_overview_status_dot(u8g2, 110u, 38u,
                    (s_ui.overview_active_mask & UI_OVERVIEW_DPLL) != 0u,
                    blink && (s_ui.overview_fault_mask & UI_OVERVIEW_DPLL) != 0u);
    draw_overview_status_dot(u8g2, 10u, 57u,
                    (s_ui.overview_active_mask & UI_OVERVIEW_TRIGGER) != 0u,
                    blink && (s_ui.overview_fault_mask & UI_OVERVIEW_TRIGGER) != 0u);
    draw_overview_status_dot(u8g2, 60u, 57u,
                    (s_ui.overview_active_mask & UI_OVERVIEW_SD) != 0u,
                    blink && (s_ui.overview_fault_mask & UI_OVERVIEW_SD) != 0u);
    draw_overview_status_dot(u8g2, 110u, 57u,
                    (s_ui.overview_fault_mask & UI_OVERVIEW_FAULT) == 0u,
                    blink && (s_ui.overview_fault_mask & UI_OVERVIEW_FAULT) != 0u);
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 16u, 41u, "TDMA");
    u8g2_DrawStr(u8g2, 66u, 41u, "VDC");
    u8g2_DrawStr(u8g2, 116u, 41u, "DPLL");
    u8g2_DrawStr(u8g2, 16u, 60u, "TRG");
    u8g2_DrawStr(u8g2, 66u, 60u, "SD");
    u8g2_DrawStr(u8g2, 116u, 60u, "FLT");
}

static void draw_product_sync(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value[32];
    char rate[12];

    draw_card(u8g2,
              2u,
              15u,
              156u,
              52u,
              s_ui.subpage == 0u ? "SYNC OVERVIEW" :
              (s_ui.subpage == 1u ? "TDMA LINK" :
               (s_ui.subpage == 2u ? "TDMA FRAME DIAG" : "TDMA QUEUE WATER")));

    if (s_ui.subpage == 0u) {
        snprintf(value,
                 sizeof(value),
                 "%s TX%lu RX%lu",
                 snapshot->tdma_valid && snapshot->tdma.ring_adapter_started != 0u ?
                     "RUN" : "OFF",
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.ring_adapter_tx_count : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.ring_adapter_rx_count : 0ul);
        draw_kv_line(u8g2, 7u, 36u, 148u, "TDMA", value);
        snprintf(value,
                 sizeof(value),
                 "%s",
                 snapshot->vdc_valid ?
                     vdc_health_to_short(snapshot->vdc.quality.health_state) : "NO VDC");
        draw_kv_line(u8g2, 7u, 45u, 148u, "VDC", value);
        snprintf(value,
                 sizeof(value),
                 "%s",
                 snapshot->vdc_valid ? vdc_lock_to_short(snapshot->vdc.dpll.state) : "NO DPLL");
        draw_kv_line(u8g2, 7u, 54u, 148u, "DPLL", value);
        snprintf(value,
                 sizeof(value),
                 "T%lu V%lu G%lu",
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.ring_adapter_last_error : 0ul,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.last_reject_code : 0ul,
                 snapshot->vdc_valid ? (unsigned long)snapshot->vdc.gate.reject_code : 0ul);
        draw_kv_line(u8g2, 7u, 63u, 148u, "CHAIN ERR", value);
        return;
    }

    if (s_ui.subpage == 2u) {
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu",
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_up_tx_sequence : 0ul,
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_down_rx_sequence : 0ul);
        draw_kv_line(u8g2, 7u, 36u, 148u, "SEQ TX/RX", value);

        snprintf(value,
                 sizeof(value),
                 "%04lX/%04lX",
                 snapshot->tdma_valid ?
                     (unsigned long)(snapshot->tdma.ring_up_tx_frame_crc32 & 0xFFFFu) : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)(snapshot->tdma.ring_down_rx_frame_crc32 & 0xFFFFu) : 0ul);
        draw_kv_line(u8g2, 7u, 45u, 148u, "CRC TX/RX", value);

        snprintf(value,
                 sizeof(value),
                 "%lu/%lu/%lu",
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.dropped_seq : 0ul,
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.timeout_count : 0ul,
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.overrun_count : 0ul);
        draw_kv_line(u8g2, 7u, 54u, 148u, "DROP/T/O", value);

        snprintf(value,
                 sizeof(value),
                 "%luns RES%lu",
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.ring_feedback_round_trip_ns : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.ring_timestamp_resolution_ns : 0ul);
        draw_kv_line(u8g2, 7u, 63u, 148u, "RTT", value);
        return;
    }

    if (s_ui.subpage == 3u) {
        snprintf(value,
                 sizeof(value),
                 "%lu CFG%lu",
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.traffic_scheduler_queued_count : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.traffic_scheduler_configured : 0ul);
        draw_kv_line(u8g2, 7u, 36u, 148u, "QUEUE", value);
        snprintf(value,
                 sizeof(value),
                 "%lu R%lu C%lu",
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.traffic_scheduler_fault_latched : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.traffic_scheduler_last_result : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.traffic_scheduler_last_class : 0ul);
        draw_kv_line(u8g2, 7u, 45u, 148u, "SCHED", value);
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu/%lu",
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.service_count : 0ul,
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.completed_seq : 0ul,
                 snapshot->tdma_valid ? (unsigned long)snapshot->tdma.dropped_seq : 0ul);
        draw_kv_line(u8g2, 7u, 54u, 148u, "SVC/DONE/DR", value);
        snprintf(value,
                 sizeof(value),
                 "%lu %lu/%lu",
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.scheduled_window_miss_count : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.scheduled_window_wait_ns : 0ul,
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.scheduled_window_late_ns : 0ul);
        draw_kv_line(u8g2, 7u, 63u, 148u, "WIN M/W/L", value);
        return;
    }

    snprintf(value,
             sizeof(value),
             "%s UP%lu DN%lu",
             snapshot->tdma_valid && snapshot->tdma.ring_adapter_started != 0u ? "RUN" : "OFF",
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_up_running : 0ul,
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_down_running : 0ul);
    draw_kv_line(u8g2, 7u, 36u, 148u, "LINK", value);

    snprintf(value,
             sizeof(value),
             "%lu/%lu",
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_adapter_tx_count : 0ul,
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_adapter_rx_count : 0ul);
    draw_kv_line(u8g2, 7u, 45u, 148u, "TX/RX", value);

    snprintf(value,
             sizeof(value),
             "%lu E%lu",
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_adapter_rx_bad_count : 0ul,
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_adapter_last_error : 0ul);
    draw_kv_line(u8g2, 7u, 54u, 148u, "BAD", value);

    format_freq_hz(rate, sizeof(rate), snapshot->tdma_valid ? snapshot->tdma.baud_hz : 0u);
    snprintf(value,
             sizeof(value),
             "%s N%lu R%lu",
             rate,
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_local_slot_id : 0ul,
             snapshot->tdma_valid ? (unsigned long)snapshot->tdma.ring_reference_slot_id : 0ul);
    draw_kv_line(u8g2, 7u, 63u, 148u, "RATE", value);
}

static void draw_product_vdc(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value[32];

    draw_card(u8g2,
              2u,
              15u,
              156u,
              52u,
              s_ui.subpage == 0u ? "VDC / DPLL" :
              (s_ui.subpage == 1u ? "VDC SAMPLE WATER" : "DPLL DIAGNOSTICS"));

    if (s_ui.subpage == 1u) {
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu",
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.accepted_sample_count : 0ul,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.rejected_sample_count : 0ul);
        draw_kv_line(u8g2, 7u, 36u, 148u, "ACCEPT/REJ", value);
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu",
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.consecutive_good_samples : 0ul,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.consecutive_bad_samples : 0ul);
        draw_kv_line(u8g2, 7u, 45u, 148u, "GOOD/BAD", value);
        snprintf(value,
                 sizeof(value),
                 "%luus LIM%lu",
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.last_sample_age_us : 0ul,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.freshness_limit_us : 0ul);
        draw_kv_line(u8g2, 7u, 54u, 148u, "AGE", value);
        snprintf(value,
                 sizeof(value),
                 "G%lu R%lu",
                 snapshot->vdc_valid ? (unsigned long)snapshot->vdc.gate.reject_code : 0ul,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.quality.last_reject_code : 0ul);
        draw_kv_line(u8g2, 7u, 63u, 148u, "GATE", value);
        return;
    }

    if (s_ui.subpage == 2u) {
        snprintf(value,
                 sizeof(value),
                 "%ld/%luppb",
                 snapshot->vdc_valid ?
                     (long)snapshot->vdc.error_budget.freq_offset_ppb : 0l,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.error_budget.freq_skew_ppb : 0ul);
        draw_kv_line(u8g2, 7u, 36u, 148u, "FREQ/SKEW", value);
        snprintf(value,
                 sizeof(value),
                 "%lu/%luns",
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.error_budget.dispersion_ns : 0ul,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.error_budget.root_distance_ns : 0ul);
        draw_kv_line(u8g2, 7u, 45u, 148u, "DISP/ROOT", value);
        snprintf(value,
                 sizeof(value),
                 "%luus",
                 snapshot->vdc_valid ? (unsigned long)snapshot->vdc.dpll.holdover_age_us : 0ul);
        draw_kv_line(u8g2, 7u, 54u, 148u, "HOLDOVER", value);
        snprintf(value,
                 sizeof(value),
                 "U%lu R%lu",
                 snapshot->vdc_valid ? (unsigned long)snapshot->vdc.dpll.update_seq : 0ul,
                 snapshot->vdc_valid ?
                     (unsigned long)snapshot->vdc.dpll.last_reject_code : 0ul);
        draw_kv_line(u8g2, 7u, 63u, 148u, "UPDATE", value);
        return;
    }

    snprintf(value,
             sizeof(value),
             "%s %s",
             snapshot->vdc_valid ?
                 vdc_health_to_short(snapshot->vdc.quality.health_state) : "NO VDC",
             snapshot->vdc_valid ? vdc_lock_to_short(snapshot->vdc.dpll.state) : "UNK");
    draw_kv_line(u8g2, 7u, 36u, 148u, "STATE", value);
    snprintf(value,
             sizeof(value),
             "%ldns RMS%lu",
             snapshot->vdc_valid ? (long)snapshot->vdc.dpll.last_offset_ns : 0l,
             snapshot->vdc_valid ? (unsigned long)snapshot->vdc.dpll.rms_offset_ns : 0ul);
    draw_kv_line(u8g2, 7u, 45u, 148u, "OFFSET", value);
    snprintf(value,
             sizeof(value),
             "%lu/%luns",
             snapshot->vdc_valid ? (unsigned long)snapshot->vdc.quality.last_jitter_ns : 0ul,
             snapshot->vdc_valid ? (unsigned long)snapshot->vdc.quality.jitter_pk_ns : 0ul);
    draw_kv_line(u8g2, 7u, 54u, 148u, "JIT/PK", value);
    snprintf(value,
             sizeof(value),
             "%luus %ldppb",
             snapshot->vdc_valid ? (unsigned long)snapshot->vdc.dpll.holdover_age_us : 0ul,
             snapshot->vdc_valid ? (long)snapshot->vdc.dpll.last_frequency_error_ppb : 0l);
    draw_kv_line(u8g2, 7u, 63u, 148u, "HOLD/FREQ", value);
}

static void draw_product_trigger(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value[32];
    char duration[12];

    draw_card(u8g2, 2u, 15u, 156u, 52u, s_ui.subpage > 0u ? "TRIGGER DIAG" : "TRIGGER");
    snprintf(value,
             sizeof(value),
             "%s %s",
             trigger_mode_to_short(snapshot->trigger.active_mode),
             trigger_state_to_short(snapshot->trigger.state));
    draw_kv_line(u8g2, 7u, 36u, 148u, "STATE", value);
    snprintf(value,
             sizeof(value),
             "GPIO%lu %s",
             (unsigned long)snapshot->trigger.trigger_source_pin,
             edge_to_short(snapshot->trigger.edge));
    draw_kv_line(u8g2, 7u, 45u, 148u, "SOURCE", value);
    if (s_ui.subpage > 0u) {
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu/%lu",
                 (unsigned long)snapshot->trigger.trigger_count,
                 (unsigned long)snapshot->trigger.output_count,
                 (unsigned long)snapshot->trigger.missed_count);
        draw_kv_line(u8g2, 7u, 54u, 148u, "TRG/OUT/MISS", value);
        snprintf(value,
                 sizeof(value),
                 "CAP %s CLK %s",
                 bool_to_run_stop(snapshot->trigger.capture_running),
                 bool_to_run_stop(snapshot->trigger.sync_clock_running));
        draw_kv_line(u8g2, 7u, 63u, 148u, "ENGINE", value);
    } else {
        format_duration_us(duration, sizeof(duration), snapshot->trigger.trigger_width_us);
        draw_kv_line(u8g2, 7u, 54u, 148u, "WIDTH", duration);
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu",
                 (unsigned long)snapshot->trigger.trigger_count,
                 (unsigned long)snapshot->trigger.missed_count);
        draw_kv_line(u8g2, 7u, 63u, 148u, "COUNT/MISS", value);
    }
}

static void draw_product_ota(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value[32];
    char progress[16];
    char rx[24];

    draw_progress_card(u8g2,
                       2u,
                       15u,
                       156u,
                       52u,
                       "SYS OTA",
                       snapshot->ota.progress_permille);
    format_progress(progress, sizeof(progress), snapshot->ota.progress_permille);
    snprintf(value,
             sizeof(value),
             "%s %s",
             ota_state_to_short_label((ota_state_t)snapshot->ota.state),
             progress);
    draw_kv_line(u8g2, 7u, 36u, 148u, "STATE", value);
    format_rx_progress(rx,
                       sizeof(rx),
                       snapshot->ota.received_size,
                       snapshot->ota.expected_size);
    draw_kv_line(u8g2, 7u, 45u, 148u, "RX", rx);
    snprintf(value,
             sizeof(value),
             "%s %s",
             ota_slot_to_short_label(snapshot->ota.target_slot),
             ota_result_to_short_label((ota_result_t)snapshot->ota.last_result));
    draw_kv_line(u8g2, 7u, 54u, 148u, "TARGET", value);
    copy_compact_error(value, sizeof(value), snapshot->ota.error_code);
    draw_kv_line(u8g2, 7u, 63u, 148u, "ERROR", value);
}

static void draw_product_sd(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    const storage_manager_vector_t *storage = &snapshot->storage;
    char value[32];
    char capacity[16];

    draw_card(u8g2, 2u, 15u, 156u, 52u, "SYS SD STORAGE");
    snprintf(value,
             sizeof(value),
             "%s %s",
             storage_manager_state_string(storage->state),
             storage->fs_mounted ? "MOUNT" : "NO FS");
    draw_kv_line(u8g2, 7u, 36u, 148u, "STATE", value);
    format_kib_compact(capacity, sizeof(capacity), storage->capacity_kib);
    draw_kv_line(u8g2, 7u, 45u, 148u, "CAPACITY", capacity);
    snprintf(value,
             sizeof(value),
             "%s %lu/%lu",
             storage_manifest_to_short(storage->manifest_status),
             (unsigned long)storage->manifest_missing_count,
             (unsigned long)storage->manifest_required_count);
    draw_kv_line(u8g2, 7u, 54u, 148u, "PACK", value);
    snprintf(value,
             sizeof(value),
             "%s %s E%lu/%lu",
             storage_job_to_short(storage->current_job_type),
             storage_job_state_to_short(storage->current_job_state),
             (unsigned long)storage->storage_error,
             (unsigned long)storage->job_error);
    draw_kv_line(u8g2, 7u, 63u, 148u, "JOB", value);
}

static void draw_product_system(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value[32];
    char progress[16];
    char build[12];

    if (s_ui.subpage == 1u) {
        draw_product_ota(u8g2, snapshot);
        return;
    }
    if (s_ui.subpage == 2u) {
        draw_product_sd(u8g2, snapshot);
        return;
    }

    draw_card(u8g2,
              2u,
              15u,
              156u,
              52u,
              s_ui.subpage == 0u ? "SYSTEM OVERVIEW" : "SYSTEM RESOURCES");
    if (s_ui.subpage == 3u) {
        snprintf(value,
                 sizeof(value),
                 "%lu/%luB",
                 (unsigned long)snapshot->heap_free_bytes,
                 (unsigned long)snapshot->heap_min_free_bytes);
        draw_kv_line(u8g2, 7u, 36u, 148u, "HEAP F/MIN", value);
        snprintf(value,
                 sizeof(value),
                 "0x%03lX C%03lX",
                 (unsigned long)snapshot->arbiter.active_resources,
                 (unsigned long)snapshot->arbiter.last_conflict_resources);
        draw_kv_line(u8g2, 7u, 45u, 148u, "RESOURCE", value);
        snprintf(value,
                 sizeof(value),
                 "%luB %s",
                 (unsigned long)snapshot->storage.log_pending_bytes,
                 storage_job_state_to_short(snapshot->storage.current_job_state));
        draw_kv_line(u8g2, 7u, 54u, 148u, "SD PENDING", value);
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu",
                 (unsigned long)snapshot->storage.storage_error,
                 (unsigned long)snapshot->storage.job_error);
        draw_kv_line(u8g2, 7u, 63u, 148u, "SD ERROR", value);
        return;
    }

    format_progress(progress, sizeof(progress), snapshot->ota.progress_permille);
    snprintf(value,
             sizeof(value),
             "%s %s",
             ota_state_to_short_label((ota_state_t)snapshot->ota.state),
             progress);
    draw_kv_line(u8g2, 7u, 36u, 148u, "OTA", value);
    snprintf(value,
             sizeof(value),
             "%s %s",
             snapshot->storage.card_present ? "CARD" : "NO CARD",
             snapshot->storage.fs_mounted ? "MOUNT" : "NO FS");
    draw_kv_line(u8g2, 7u, 45u, 148u, "SD", value);
    snprintf(value,
             sizeof(value),
             "%s 0x%03lX",
             arbiter_mode_to_short(snapshot->arbiter.mode),
             (unsigned long)snapshot->arbiter.active_resources);
    draw_kv_line(u8g2, 7u, 54u, 148u, "RESOURCE", value);
    format_short_build(build, sizeof(build));
    draw_kv_line(u8g2, 7u, 63u, 148u, "FIRMWARE", build);
}

static void draw_product_health(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char value[32];
    const bool core_ok = snapshot->core.core1_enabled && !snapshot->led.core1_stale;

    draw_card(u8g2,
              2u,
              15u,
              156u,
              52u,
              s_ui.subpage == 0u ? "HEALTH OVERVIEW" :
              (s_ui.subpage == 1u ? "HEALTH CORE / LED" : "HEALTH WATERMARK"));

    if (s_ui.subpage == 1u) {
        snprintf(value,
                 sizeof(value),
                 "%lu/%lu AGE%lu",
                 (unsigned long)snapshot->core.core0_loop_count,
                 (unsigned long)snapshot->core.core1_loop_count,
                 (unsigned long)(snapshot->uptime_ms - snapshot->core.core1_last_ms));
        draw_kv_line(u8g2, 7u, 36u, 148u, "CORE 0/1", value);
        snprintf(value,
                 sizeof(value),
                 "%s/%s/%s",
                 led_manager_pattern_string(snapshot->led.system_pattern),
                 led_manager_pattern_string(snapshot->led.arm_pattern),
                 led_manager_pattern_string(snapshot->led.fault_pattern));
        draw_kv_line(u8g2, 7u, 45u, 148u, "LED S/A/F", value);
        snprintf(value,
                 sizeof(value),
                 "CFG%u SD%u H%lX",
                 snapshot->led.config_ready ? 1u : 0u,
                 snapshot->led.sd_ready ? 1u : 0u,
                 (unsigned long)snapshot->led.health_flags);
        draw_kv_line(u8g2, 7u, 54u, 148u, "READY", value);
        snprintf(value,
                 sizeof(value),
                 "%lu F%lu P%lu",
                 (unsigned long)snapshot->led.event_sequence,
                 (unsigned long)snapshot->led.fault_transition_count,
                 (unsigned long)snapshot->led.pattern_transition_count);
        draw_kv_line(u8g2, 7u, 63u, 148u, "EVENT", value);
        return;
    }

    if (s_ui.subpage == 2u) {
        snprintf(value,
                 sizeof(value),
                 "%lu/%luB",
                 (unsigned long)snapshot->heap_free_bytes,
                 (unsigned long)snapshot->heap_min_free_bytes);
        draw_kv_line(u8g2, 7u, 36u, 148u, "HEAP F/MIN", value);
        snprintf(value,
                 sizeof(value),
                 "%lu/%luB",
                 (unsigned long)snapshot->diagnostics.queue_bytes,
                 (unsigned long)snapshot->diagnostics.queue_high_watermark);
        draw_kv_line(u8g2, 7u, 45u, 148u, "LOG Q/HIGH", value);
        snprintf(value,
                 sizeof(value),
                 "%lu/%luB",
                 (unsigned long)snapshot->diagnostics.persistent_queue_bytes,
                 (unsigned long)snapshot->diagnostics.persistent_queue_high_watermark);
        draw_kv_line(u8g2, 7u, 54u, 148u, "PLOG Q/HIGH", value);
        snprintf(value,
                 sizeof(value),
                 "T%lu SD%luB",
                 snapshot->tdma_valid ?
                     (unsigned long)snapshot->tdma.traffic_scheduler_queued_count : 0ul,
                 (unsigned long)snapshot->storage.log_pending_bytes);
        draw_kv_line(u8g2, 7u, 63u, 148u, "DOMAIN Q", value);
        return;
    }

    draw_kv_line(u8g2,
                 7u,
                 36u,
                 148u,
                 "POLICY",
                 led_manager_policy_string(snapshot->led.policy));
    draw_kv_line(u8g2, 7u, 45u, 148u, "CORE1", core_ok ? "OK" : "STALE");
    snprintf(value,
             sizeof(value),
             "T%lu V%lu SD%lu",
             snapshot->tdma_valid ?
                 (unsigned long)snapshot->tdma.ring_adapter_last_error : 0ul,
             snapshot->vdc_valid ?
                 (unsigned long)snapshot->vdc.quality.last_reject_code : 0ul,
             (unsigned long)snapshot->storage.storage_error);
    draw_kv_line(u8g2, 7u, 54u, 148u, "DOMAIN ERR", value);
    snprintf(value,
             sizeof(value),
             "%s L%u",
             snapshot->fault_active ? "FAULT" : "OK",
             snapshot->led.fault_latched ? 1u : 0u);
    draw_kv_line(u8g2, 7u, 63u, 148u, "GLOBAL", value);
}

static void draw_product_footer(u8g2_t *u8g2)
{
    char center[12];
    const uint8_t count = ui_subpage_count(s_ui.page);

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawHLine(u8g2, 0u, 69u, 160u);
    u8g2_DrawStr(u8g2, 2u, 79u, "L <");
    snprintf(center,
             sizeof(center),
             "C %c %u/%u",
             s_ui.subpage + 1u >= count ? '^' : 'v',
             (unsigned int)(s_ui.subpage + 1u),
             (unsigned int)count);
    u8g2_DrawStr(u8g2, 58u, 79u, center);
    u8g2_DrawStr(u8g2, 124u, 79u, "R >");
}

static void draw_body(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    switch (s_ui.page) {
    case UI_PAGE_SYNC:
        draw_product_sync(u8g2, snapshot);
        break;
    case UI_PAGE_VDC:
        draw_product_vdc(u8g2, snapshot);
        break;
    case UI_PAGE_TRIGGER:
        draw_product_trigger(u8g2, snapshot);
        break;
    case UI_PAGE_SYSTEM:
        draw_product_system(u8g2, snapshot);
        break;
    case UI_PAGE_HEALTH:
        draw_product_health(u8g2, snapshot);
        break;
    case UI_PAGE_OVERVIEW:
    default:
        draw_product_overview(u8g2, snapshot);
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
        if (y < 18u) {
            return rgb565(8, 29, 39);
        }
        if (y > 62u) {
            return rgb565(19, 24, 34);
        }
        return (x < 80u) ? rgb565(11, 22, 32) : rgb565(14, 28, 34);
    }
    if (s_ui.cover_active) {
        (void)x;
        if (y >= 54u && y <= 66u) {
            return rgb565(241, 245, 243);
        }
        return rgb565(251, 251, 249);
    }

    /* Product pages use one continuous light canvas.  The old 240x135 theme
     * split the body by x/y regions; after the move to 160x80 that left the
     * overview visibly divided into two background colours. */
    if (y < 14u) {
        return rgb565(235, 243, 241);
    }
    if (y >= 68u) {
        return rgb565(232, 235, 234);
    }
    return rgb565(248, 249, 247);
}

static uint16_t themed_foreground(uint16_t x, uint16_t y)
{
    if (s_ui.boot_splash_active) {
        if (y < 18u) {
            return rgb565(121, 237, 224);
        }
        if (y > 62u) {
            return rgb565(248, 194, 86);
        }
        if (x > 14u && x < 146u && y > 45u && y < 58u) {
            return rgb565(96, 219, 151);
        }
        return rgb565(225, 236, 232);
    }
    if (s_ui.cover_active) {
        if (x >= 18u && x < 38u && y >= 8u && y < 30u) {
            return rgb565(214, 0, 55);
        }
        if (x >= 94u && x <= 102u && y >= 13u && y <= 21u) {
            return s_ui.cover_fault_active ? rgb565(196, 51, 45) : rgb565(35, 145, 75);
        }
        if (y >= 55u && y <= 65u) {
            return s_ui.cover_fault_active ? rgb565(196, 51, 45) : rgb565(35, 126, 82);
        }
        if (y >= 68u) {
            return rgb565(45, 91, 88);
        }
        return rgb565(31, 42, 43);
    }

    if (y < 14u) {
        if (x >= 31u && x <= 39u) {
            return rgb565(35, 154, 91);
        }
        switch (s_ui.page) {
        case UI_PAGE_SYNC:
            return rgb565(39, 101, 156);
        case UI_PAGE_VDC:
            return rgb565(38, 116, 111);
        case UI_PAGE_TRIGGER:
            return rgb565(36, 126, 72);
        case UI_PAGE_SYSTEM:
            return rgb565(116, 70, 150);
        case UI_PAGE_HEALTH:
            return rgb565(164, 73, 39);
        case UI_PAGE_OVERVIEW:
        default:
            return rgb565(28, 122, 116);
        }
    }
    if (y >= 68u) {
        return rgb565(69, 79, 80);
    }
    if (s_ui.page == UI_PAGE_OVERVIEW && s_ui.subpage == 0u) {
        uint8_t status_bit = 0u;
        const bool top_dot = y >= 36u && y <= 40u;
        const bool bottom_dot = y >= 55u && y <= 59u;
        if ((top_dot || bottom_dot) && x >= 8u && x <= 12u) {
            status_bit = top_dot ? UI_OVERVIEW_TDMA : UI_OVERVIEW_TRIGGER;
        } else if ((top_dot || bottom_dot) && x >= 58u && x <= 62u) {
            status_bit = top_dot ? UI_OVERVIEW_VDC : UI_OVERVIEW_SD;
        } else if ((top_dot || bottom_dot) && x >= 108u && x <= 112u) {
            status_bit = top_dot ? UI_OVERVIEW_DPLL : UI_OVERVIEW_FAULT;
        }
        if (status_bit != 0u) {
            if ((s_ui.overview_fault_mask & status_bit) != 0u) {
                return rgb565(196, 51, 45);
            }
            if ((s_ui.overview_active_mask & status_bit) != 0u) {
                return rgb565(35, 145, 75);
            }
            return rgb565(190, 125, 0);
        }
    }
    if (s_ui.page == UI_PAGE_SYNC) {
        if (y > 45u && y < 55u) {
            return rgb565(31, 105, 150);
        }
        return rgb565(32, 45, 55);
    }
    if (s_ui.page == UI_PAGE_VDC) {
        if (y > 45u && y < 55u) {
            return rgb565(24, 124, 117);
        }
        return rgb565(30, 48, 48);
    }
    if (s_ui.page == UI_PAGE_TRIGGER) {
        if (y > 45u && y < 55u) {
            return rgb565(181, 113, 0);
        }
        return rgb565(31, 45, 40);
    }
    if (s_ui.page == UI_PAGE_SYSTEM) {
        if (y > 50u && y < 66u) {
            return rgb565(31, 105, 150);
        }
        return rgb565(45, 38, 51);
    }
    if (s_ui.page == UI_PAGE_HEALTH) {
        if (y > 46u && y < 56u) {
            return rgb565(181, 113, 0);
        }
        return rgb565(55, 40, 36);
    }
    return rgb565(31, 42, 43);
}

static void flush_to_lcd(void)
{
    /* Keep the ST7735S in its proven native 80x160 scan mode.  Hardware MV
     * landscape mode produces a row-wrap skew on this module, so map the
     * 160x80 logical UI clockwise into the portrait RAM window in software. */
    lcd_st7789_set_window(0u, 0u, (uint16_t)(UI_HEIGHT - 1u), (uint16_t)(UI_WIDTH - 1u));

    size_t count = 0u;
    for (uint16_t panel_y = 0u; panel_y < UI_WIDTH; panel_y++) {
        for (uint16_t panel_x = 0u; panel_x < UI_HEIGHT; panel_x++) {
            const uint16_t logical_x = panel_y;
            const uint16_t logical_y = (uint16_t)(UI_HEIGHT - 1u - panel_x);
            s_ui.line_buffer[count++] = mono_pixel_is_set(logical_x, logical_y)
                                           ? themed_foreground(logical_x, logical_y)
                                           : themed_background(logical_x, logical_y);
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
    const uint8_t bar_width = (uint8_t)(((uint16_t)124u * progress) / 100u);
    const uint8_t sweep = (uint8_t)((frame * 13u) % UI_WIDTH);
    char text_buffer[24];

    u8g2_ClearBuffer(u8g2);

    for (uint8_t i = 0u; i < 5u; i++) {
        const uint8_t x = (uint8_t)((sweep + (i * 47u)) % UI_WIDTH);
        u8g2_DrawVLine(u8g2, x, 18u, 42u);
    }

    u8g2_DrawFrame(u8g2, 4u, 4u, 152u, 72u);
    u8g2_DrawFrame(u8g2, 6u, 6u, 148u, 68u);
    u8g2_DrawBox(u8g2, 12u, 12u, 136u, 1u);

    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    draw_centered_tracked_str(u8g2, 0u, UI_WIDTH, 28u, "DHRT100", 4u, 2u);
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 15u, 38u, "DISTRIBUTED HARD REAL-TIME");
    u8g2_DrawStr(u8g2, 45u, 46u, "TRIGGER SYSTEM");

    u8g2_DrawFrame(u8g2, 16u, 50u, 128u, 8u);
    if (bar_width > 0u) {
        u8g2_DrawBox(u8g2, 18u, 52u, bar_width, 4u);
    }

    snprintf(text_buffer, sizeof(text_buffer), "BOOT %u%%", progress);
    u8g2_DrawStr(u8g2, 16u, 67u, text_buffer);
    snprintf(text_buffer,
             sizeof(text_buffer),
             "FW %lu.%lu.%lu",
             (unsigned long)PROJECT_VERSION_MAJOR,
             (unsigned long)PROJECT_VERSION_MINOR,
             (unsigned long)PROJECT_VERSION_PATCH);
    u8g2_DrawStr(u8g2, 91u, 67u, text_buffer);

    u8g2_DrawBox(u8g2, (u8g2_uint_t)(16u + (frame % 16u) * 8u), 70u, 10u, 2u);
}

static void run_boot_splash(void)
{
    if (!resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                        RESOURCE_ARBITER_RESOURCE_LCD,
                                        "StatusUI")) {
        return;
    }
    board_prepare_lcd_spi();

    s_ui.boot_splash_active = true;
    for (uint8_t frame = 0u; frame < 16u; frame++) {
        draw_boot_splash_frame(frame);
        flush_to_lcd();
        board_service();
        osal_delay_ms(28u);
    }
    s_ui.boot_splash_active = false;

    resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                   RESOURCE_ARBITER_RESOURCE_LCD,
                                   "StatusUI");
}

bool status_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    u8g2_port_setup_160x80(&s_ui.u8g2, s_ui.mono_buffer);
    u8g2_SetFontMode(&s_ui.u8g2, 1);
    s_ui.page = UI_PAGE_OVERVIEW;
    s_ui.target_page = UI_PAGE_OVERVIEW;
    s_ui.previous_page = UI_PAGE_OVERVIEW;
    s_ui.tab_from_first = 0u;
    s_ui.tab_to_first = 0u;
    s_ui.initialized = true;
    run_boot_splash();
    s_ui.cover_active = true;
    return true;
}

static bool dismiss_product_cover(void)
{
    if (!s_ui.cover_active) {
        return false;
    }

    s_ui.cover_active = false;
    s_ui.page = UI_PAGE_OVERVIEW;
    s_ui.target_page = UI_PAGE_OVERVIEW;
    s_ui.previous_page = UI_PAGE_OVERVIEW;
    s_ui.subpage = 0u;
    s_ui.tab_anim = 0u;
    return true;
}

void status_ui_key_next(void)
{
    if (!s_ui.initialized) {
        return;
    }
    if (dismiss_product_cover()) {
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
    if (s_ui.subpage >= ui_subpage_count(s_ui.target_page)) {
        s_ui.subpage = (uint8_t)(ui_subpage_count(s_ui.target_page) - 1u);
    }
    s_ui.tab_to_first = tab_first_for_page((uint8_t)s_ui.target_page,
                                           (uint8_t)UI_PAGE_COUNT,
                                           3u);
    s_ui.page = s_ui.target_page;
    s_ui.tab_anim = 0u;
}

void status_ui_key_previous(void)
{
    if (!s_ui.initialized) {
        return;
    }
    if (dismiss_product_cover()) {
        return;
    }
    if (s_ui.tab_anim > 0u || s_ui.page != s_ui.target_page) {
        return;
    }

    s_ui.previous_page = s_ui.page;
    s_ui.tab_from_first = tab_first_for_page((uint8_t)s_ui.previous_page,
                                             (uint8_t)UI_PAGE_COUNT,
                                             3u);
    s_ui.target_page = s_ui.page == UI_PAGE_OVERVIEW ?
                           (ui_page_t)((uint32_t)UI_PAGE_COUNT - 1u) :
                           (ui_page_t)((uint32_t)s_ui.page - 1u);
    if (s_ui.subpage >= ui_subpage_count(s_ui.target_page)) {
        s_ui.subpage = (uint8_t)(ui_subpage_count(s_ui.target_page) - 1u);
    }
    s_ui.tab_to_first = tab_first_for_page((uint8_t)s_ui.target_page,
                                           (uint8_t)UI_PAGE_COUNT,
                                           3u);
    s_ui.page = s_ui.target_page;
    s_ui.tab_anim = 0u;
}

void status_ui_key_select(void)
{
    if (!s_ui.initialized) {
        return;
    }
    if (dismiss_product_cover()) {
        return;
    }
    if (s_ui.tab_anim > 0u) {
        return;
    }
    s_ui.subpage = (uint8_t)((s_ui.subpage + 1u) % ui_subpage_count(s_ui.page));
}

void status_ui_key_back(void)
{
    if (!s_ui.initialized) {
        return;
    }
    if (dismiss_product_cover()) {
        return;
    }
    if (s_ui.subpage > 0u) {
        s_ui.subpage = 0u;
        return;
    }
    if (s_ui.page != UI_PAGE_OVERVIEW && s_ui.tab_anim == 0u) {
        s_ui.previous_page = s_ui.page;
        s_ui.tab_from_first = tab_first_for_page((uint8_t)s_ui.previous_page,
                                                 (uint8_t)UI_PAGE_COUNT,
                                                 3u);
        s_ui.target_page = UI_PAGE_OVERVIEW;
        s_ui.tab_to_first = 0u;
        s_ui.page = s_ui.target_page;
        s_ui.tab_anim = 0u;
    }
}

bool status_ui_render(void)
{
    ui_snapshot_t *const snapshot = &s_snapshot;

    if (!s_ui.initialized) {
        return false;
    }

    if (!resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                        RESOURCE_ARBITER_RESOURCE_LCD,
                                        "StatusUI")) {
        return false;
    }
    drv_watchdog_mark_progress(0u, 0x0A10u);
    board_prepare_lcd_spi();

    capture_snapshot(snapshot);
    drv_watchdog_mark_progress(0u, 0x0A1Au);

    u8g2_t *u8g2 = &s_ui.u8g2;
    s_ui.frame++;
    if (s_ui.tab_anim == 0u && s_ui.page != s_ui.target_page) {
        s_ui.page = s_ui.target_page;
        s_ui.previous_page = s_ui.page;
    }

    u8g2_ClearBuffer(u8g2);
    if (s_ui.cover_active) {
        draw_product_cover(u8g2, snapshot);
    } else {
        draw_product_header(u8g2, snapshot);
        draw_body(u8g2, snapshot);
        draw_product_footer(u8g2);
    }

    flush_to_lcd();
    drv_watchdog_mark_progress(0u, 0x0A1Bu);
    if (s_ui.tab_anim > 0u) {
        s_ui.tab_anim--;
    }
    resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                   RESOURCE_ARBITER_RESOURCE_LCD,
                                   "StatusUI");
    return true;
}

bool status_ui_needs_render(void)
{
    return s_ui.initialized && (s_ui.tab_anim > 0u || s_ui.page != s_ui.target_page);
}
