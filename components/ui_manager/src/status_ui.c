/* status_ui pages and state machine (N5.0.4f). */
#include "status_ui_internal.h"

status_ui_t s_ui;
/* The aggregate snapshot grows with TDMA/VDC observability.  Keep it in the
 * component's static RAM instead of consuming the UI task stack on every
 * render. */
static ui_snapshot_t s_snapshot;


/* Product-panel layout: one 156x52 card on a 160x80 canvas.  The legacy
 * 240x135 multi-card renderers above remain as formatting references while
 * product bring-up converges, but are no longer selected by draw_body(). */
static void draw_product_header(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    static const char *const labels[] = {"OVR", "SYN", "VDC", "TRG", "SYS", "HLT"};
    const uint8_t tab_x = 40u;
    /* Keep the left identity area clear of the page tabs. */
    const uint8_t tab_w = 16u;
    const uint8_t active = (uint8_t)(s_ui.tab_anim > 0u ? s_ui.target_page : s_ui.page);

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
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

    char no_label[8];
    uint8_t logical_no = board_identity_get_no();
    if (logical_no == 0u && snapshot->tdma_valid &&
        snapshot->tdma.ring_local_slot_id < BOARD_IDENTITY_MAX_NODES) {
        logical_no = (uint8_t)(snapshot->tdma.ring_local_slot_id + 1u);
    }
    if (logical_no != 0u) {
        snprintf(no_label, sizeof(no_label), "NO.%u", (unsigned int)logical_no);
        draw_fit_str(u8g2, 1u, 10u, 35u, no_label);
    }
}

static void draw_product_cover(u8g2_t *u8g2, const ui_snapshot_t *snapshot)
{
    char build[12];
    char status[24];
    char no_label[8];
    char uptime[12];
    const uint8_t phase = (uint8_t)((snapshot->uptime_ms / 100u) % 20u);
    const uint8_t triangle = phase <= 10u ? phase : (uint8_t)(20u - phase);
    const uint8_t breath_radius = (uint8_t)(1u + (triangle / 5u));

    s_ui.cover_fault_active = snapshot->fault_active || snapshot->led.fault_latched;
    u8g2_DrawFrame(u8g2, 2u, 2u, 156u, 76u);
    u8g2_DrawFrame(u8g2, 4u, 4u, 152u, 72u);
    draw_dhrt100_brand_mark(u8g2, 17u, 7u);
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
    format_uptime(uptime, sizeof(uptime), snapshot->uptime_ms);
    u8g2_DrawStr(u8g2, 8u, 73u, "UP");
    u8g2_DrawStr(u8g2, 23u, 73u, uptime);
    u8g2_DrawStr(u8g2, 84u, 73u, "PRESS ANY KEY");
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
        if (x >= 15u && x < 43u && y >= 5u && y < 33u) {
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

static void note_ui_interaction(void)
{
    s_ui.last_interaction_ms = board_uptime_ms();
}

static void apply_cover_idle_timeout(uint32_t now_ms)
{
    if (s_ui.cover_active ||
        (uint32_t)(now_ms - s_ui.last_interaction_ms) < UI_COVER_IDLE_TIMEOUT_MS) {
        return;
    }

    s_ui.cover_active = true;
    s_ui.page = UI_PAGE_OVERVIEW;
    s_ui.target_page = UI_PAGE_OVERVIEW;
    s_ui.previous_page = UI_PAGE_OVERVIEW;
    s_ui.subpage = 0u;
    s_ui.tab_anim = 0u;
}

void status_ui_key_next(void)
{
    if (!s_ui.initialized) {
        return;
    }
    note_ui_interaction();
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
    note_ui_interaction();
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
    note_ui_interaction();
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
    note_ui_interaction();
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
    apply_cover_idle_timeout(snapshot->uptime_ms);
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
