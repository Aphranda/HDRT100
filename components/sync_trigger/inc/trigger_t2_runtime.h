#ifndef TRIGGER_T2_RUNTIME_H
#define TRIGGER_T2_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_action_runtime.h"
#include "trigger_time_grid.h"
#include "trigger_time_cursor.h"
#include "trigger_turntable_predictor.h"

#define TRIGGER_T2_RUNTIME_POSITION_COUNT 3u
#define TRIGGER_T2_RUNTIME_EVENT_CAPACITY TRIGGER_ACTION_RUNTIME_CAPACITY
#define TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC 2u

typedef enum {
    TRIGGER_T2_RUNTIME_OK = 0u,
    TRIGGER_T2_RUNTIME_ARGUMENT = 1u,
    TRIGGER_T2_RUNTIME_NOT_CONFIGURED = 2u,
    TRIGGER_T2_RUNTIME_NOT_ACTIVE = 3u,
    TRIGGER_T2_RUNTIME_COUNTER = 4u,
    TRIGGER_T2_RUNTIME_CURSOR = 5u,
    TRIGGER_T2_RUNTIME_TRANSPORT = 6u,
    TRIGGER_T2_RUNTIME_DESCRIPTOR = 7u,
    TRIGGER_T2_RUNTIME_ACTION = 8u,
    TRIGGER_T2_RUNTIME_BACKWARD = 9u,
} trigger_t2_runtime_result_t;

typedef struct {
    trigger_time_grid_t grid;
    trigger_turntable_predictor_config_t predictor;
    trigger_action_template_table_t templates;
    trigger_deployment_t deployment;
    uint32_t template_crc;
    uint32_t run_id;
    uint32_t local_role;
    uint32_t source_slot;
    uint32_t target_mask;
    /* Sync-sequence allowance after the predicted target, configured at STOP. */
    uint32_t descriptor_window_sync;
    uint32_t origin;
} trigger_t2_runtime_config_t;

typedef struct {
    uint32_t configured, active, origin;
    uint32_t source_slot, target_mask, run_id;
    uint32_t last_sync_sequence, published_mask, received_mask, emitted_position_mask;
    uint32_t frozen_mask, freeze_mismatch_count;
    uint32_t freeze_version[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint32_t freeze_local_version[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint8_t freeze_local_age[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    uint32_t freeze_fallback_count, freeze_missing_local_count;
    uint32_t descriptor_reject_count, transport_reject_count;
    uint32_t event_count, event_dropped_count;
    uint8_t event_dispatched[TRIGGER_T2_RUNTIME_EVENT_CAPACITY];
    uint64_t last_counter_count;
    trigger_time_cursor_snapshot_t cursor;
    trigger_turntable_predictor_snapshot_t predictor;
    trigger_action_runtime_event_t events[TRIGGER_T2_RUNTIME_EVENT_CAPACITY];
    trigger_position_descriptor_t descriptors[TRIGGER_T2_RUNTIME_POSITION_COUNT];
    trigger_t2_runtime_result_t last_error;
} trigger_t2_runtime_snapshot_t;

/* Core0 STOP/configuration owner.  The runtime service itself is bounded and
 * called from the realtime trigger phase after RefMem RX has been serviced. */
bool trigger_t2_runtime_configure(const trigger_t2_runtime_config_t *config);
bool trigger_t2_runtime_start(uint32_t count_epoch, uint64_t origin_count);
void trigger_t2_runtime_stop(void);
trigger_t2_runtime_result_t trigger_t2_runtime_service(void);
/* Core0 prediction preparation; called independently of the VDC phase. */
void trigger_t2_runtime_prepare_core0(void);
bool trigger_t2_runtime_preview_history(uint32_t position_id, uint32_t age,
                                        trigger_position_descriptor_t *out);
bool trigger_t2_runtime_take_event(trigger_action_runtime_event_t *out);
bool trigger_t2_runtime_snapshot(trigger_t2_runtime_snapshot_t *out);

#endif
