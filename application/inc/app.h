#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "app_realtime_schedule.h"

#define APP_REALTIME_SCHEDULE_VERSION 2u
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

bool app_init(void);
bool app_is_ready(void);
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
