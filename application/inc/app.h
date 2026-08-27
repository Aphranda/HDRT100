#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "app_realtime_schedule.h"

#define APP_REALTIME_SCHEDULE_VERSION 2u
#define APP_REALTIME_LOAD_COUNT 8u
#define APP_REALTIME_LOAD_ALL_MASK ((1u << APP_REALTIME_LOAD_COUNT) - 1u)

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
} app_realtime_schedule_snapshot_t;

bool app_init(void);
bool app_is_ready(void);
bool app_is_control_plane_ready(void);
void app_realtime_cycle_counter_init(void);
void app_realtime_run_once(void);
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
