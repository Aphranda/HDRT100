#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "app_realtime_schedule.h"
#include "tdma_boot_plan.h"
#include "tdma_boot_profile.h"
#include "tdma_boot_sequence.h"

#define APP_REALTIME_SCHEDULE_VERSION 3u
#define APP_REALTIME_LOAD_COUNT 8u

typedef enum {
    APP_REALTIME_LOAD_VDC = 0u,
    APP_REALTIME_LOAD_DPLL = 1u,
    APP_REALTIME_LOAD_CALIBRATION = 2u,
    APP_REALTIME_LOAD_SYNC_CAPTURE = 3u,
    APP_REALTIME_LOAD_REFMEM = 4u,
    APP_REALTIME_LOAD_MODEL = 5u,
    APP_REALTIME_LOAD_SYNC_TRIGGER = 6u,
    APP_REALTIME_LOAD_TRIGGER_MEASURE = 7u,
} app_realtime_load_id_t;

#define APP_REALTIME_LOAD_BIT(load_id) (1u << (uint32_t)(load_id))
#define APP_REALTIME_LOAD_FOUNDATION_MASK \
    (APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_VDC) | \
     APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_DPLL) | \
     APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_SYNC_CAPTURE) | \
     APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_REFMEM) | \
     APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_SYNC_TRIGGER))
#define APP_REALTIME_LOAD_SECONDARY_MASK \
    (APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_CALIBRATION) | \
     APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_MODEL) | \
     APP_REALTIME_LOAD_BIT(APP_REALTIME_LOAD_TRIGGER_MEASURE))
#define APP_REALTIME_LOAD_ALL_MASK \
    (APP_REALTIME_LOAD_FOUNDATION_MASK | APP_REALTIME_LOAD_SECONDARY_MASK)

typedef struct {
    uint32_t version;
    uint32_t sys_clock_hz;
    uint32_t cycle_cycles;
    uint32_t phase_count;
    uint32_t enabled_mask;
    uint32_t quarantined_mask;
    uint32_t cycle_count;
    uint32_t schedule_miss_count;
    uint32_t phase_start_cycle[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_end_cycle[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_wcet_cycles[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_last_start_cycle[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_last_runtime_cycles[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_max_runtime_cycles[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_run_count[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_skip_count[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_start_miss_count[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_overrun_count[APP_REALTIME_PHASE_COUNT];
    uint32_t phase_deadline_miss_count[APP_REALTIME_PHASE_COUNT];
    uint32_t profile_generation;
} app_realtime_schedule_snapshot_t;

typedef struct {
    uint32_t active_cycles;
    uint32_t applied_generation;
    uint32_t pending_cycles;
    uint32_t requested_generation;
    bool applying;
} app_realtime_period_snapshot_t;

/* Retained ARM-lifetime attribution: stopped service cannot update it, and
 * the next observed inactive -> ARM transition resets it on Core1. Normal
 * schedule wall-time gates remain authoritative. IRQ maxima are conservative
 * run maxima at each phase with an event, not isolated per-IRQ measurements.
 * IRQ allowance is a candidate until the complete ISR is hardware verified. */
typedef struct {
    uint32_t schema, candidate_irq_cycles, close_lead_cycles, physical_min_cycles;
    uint32_t sample_failures, close_misses;
    uint32_t irq_max_cycles[APP_REALTIME_PHASE_COUNT];
    uint32_t background_max_cycles[APP_REALTIME_PHASE_COUNT];
    uint32_t budget_misses[APP_REALTIME_PHASE_COUNT];
} app_realtime_priority_snapshot_t;
bool app_realtime_get_priority_snapshot(app_realtime_priority_snapshot_t *snapshot);

bool app_init(void);
bool app_is_ready(void);
/* Boot-time guide-ring decision captured during app_init: the stored boot
 * profile (v2 map only) is loaded once and turned into a local step list.
 * Execution is a separate step; this accessor only exposes what was decided. */
bool app_guide_boot_plan_get(tdma_boot_plan_t *out);
size_t app_guide_boot_steps_get(tdma_boot_step_t *out, size_t capacity);
/* Loaded guide profile (store or bench auto-form); false when the board booted
 * with no profile. */
bool app_guide_boot_profile_get(tdma_boot_profile_t *out);
/* Execute the frozen guide boot steps once (core0, after the scheduler starts).
 * Returns false on the first failed step; the failing index is 1-based. */
bool app_guide_boot_service(void);
uint32_t app_guide_boot_fail_step(void);
/* LISTEN = ARM (RX receives, TX not published) then start TX when the first
 * frame is heard; the 5 s join window sets join_timeout instead of pretending
 * the follower joined. */
bool app_guide_boot_listen_pending(void);
bool app_guide_boot_join_timeout(void);
/* 0 = topology step not failed; 1 no profile, 2 refmem topology rejected,
 * 3 adapter bring-up topology rejected. */
uint32_t app_guide_boot_topology_fail(void);
/* Apply-step precondition bitmask (see app.c): 1 snapshot, 2 enabled,
 * 4 adapter_started, 8 config not applied, 16 staged disabled,
 * 32 manager missing, 64 staged invalid, 128 level mismatch. */
uint32_t app_guide_boot_apply_fail(void);
/* True when the LISTEN join was frame-triggered (false = window expiry). */
bool app_guide_boot_join_frame_seen(void);
/* True when the completeness watch saw no down direction and stopped TX. */
bool app_guide_boot_half_ring(void);
void app_tdma_record_service(void);
bool app_tdma_record_copy(uint32_t offset, uint8_t *data, uint32_t size);
bool app_is_control_plane_ready(void);
void app_realtime_cycle_counter_init(void);
void app_realtime_run_once(void);
bool app_realtime_request_period_us(uint32_t period_us, uint32_t *generation);
bool app_realtime_get_period_snapshot(app_realtime_period_snapshot_t *snapshot);
/* Core1 only, before the next complete table starts. */
bool app_realtime_apply_pending_profile_core1(void);
uint32_t app_realtime_cycle_cycles_core1(void);
bool app_realtime_set_load_mask(uint32_t enabled_mask);
bool app_realtime_get_schedule_snapshot(
    app_realtime_schedule_snapshot_t *snapshot);
void app_usb_device_service(void);
void app_scpi_service(void);
void app_refmem_service(void);
void app_config_gate_service(void);
void app_ota_service(void);
void app_storage_service(void);
void app_diag_service(void);

#endif
