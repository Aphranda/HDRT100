#ifndef STATUS_UI_INTERNAL_H
#define STATUS_UI_INTERNAL_H

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
#define UI_COVER_IDLE_TIMEOUT_MS 60000u

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
    sync_trigger_status_t trigger;
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
    uint32_t last_interaction_ms;
    bool cover_active;
    bool cover_fault_active;
    bool boot_splash_active;
    bool initialized;
} status_ui_t;

/* N5.0.4f: single UI state instance, defined in status_ui.c. */
extern status_ui_t s_ui;

/* Render-kit helpers called by the page unit (one-way dependency). */
void draw_dhrt100_brand_mark(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y);
uint8_t ui_subpage_count(ui_page_t page);
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
const char *bool_to_run_stop(bool value);
const char *trigger_mode_to_short(trig_mode_t mode);
const char *trigger_state_to_short(trig_state_t state);
const char *edge_to_short(trig_edge_t edge);
const char *vdc_lock_to_short(uint32_t state);
const char *vdc_health_to_short(uint32_t state);
const char *arbiter_mode_to_short(resource_arbiter_mode_t mode);
const char *ota_state_to_short_label(ota_state_t state);
const char *ota_result_to_short_label(ota_result_t result);
const char *storage_manifest_to_short(storage_manager_manifest_status_t status);
const char *storage_job_to_short(storage_manager_job_type_t type);
const char *storage_job_state_to_short(storage_manager_job_state_t state);
void copy_compact_error(char *buffer, size_t buffer_size, uint32_t error_code);
void format_freq_hz(char *buffer, size_t buffer_size, uint32_t hz);
void format_duration_us(char *buffer, size_t buffer_size, uint32_t duration_us);
void format_progress(char *buffer, size_t buffer_size, uint32_t progress_permille);
void format_kib_compact(char *buffer, size_t buffer_size, uint32_t size_kib);
void format_rx_progress(char *buffer, size_t buffer_size, uint32_t received_size, uint32_t expected_size);
void format_uptime(char *buffer, size_t buffer_size, uint32_t uptime_ms);
void format_short_build(char *buffer, size_t buffer_size);
const char *ota_slot_to_short_label(uint32_t slot);
void capture_snapshot(ui_snapshot_t *snapshot);
void draw_card(u8g2_t *u8g2, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const char *title);
void draw_progress_card(u8g2_t *u8g2, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const char *title, uint32_t permille);
void draw_fit_str(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t max_width, const char *text);
void draw_centered_tracked_str(u8g2_t *u8g2, u8g2_uint_t left, u8g2_uint_t width, u8g2_uint_t y, const char *text, uint8_t tracked_chars, uint8_t spacing);
void draw_tracked_title_with_breath(u8g2_t *u8g2, u8g2_uint_t left, u8g2_uint_t width, u8g2_uint_t text_y, u8g2_uint_t breath_y, const char *title, uint8_t tracked_chars, uint8_t spacing, uint8_t breath_radius);
void draw_kv_line(u8g2_t *u8g2, uint8_t x, uint8_t y, uint8_t w, const char *label, const char *value);
uint8_t tab_first_for_page(uint8_t active_page, uint8_t page_count, uint8_t visible_count);

#endif /* STATUS_UI_INTERNAL_H */

