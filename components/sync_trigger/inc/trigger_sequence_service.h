#ifndef TRIGGER_SEQUENCE_SERVICE_H
#define TRIGGER_SEQUENCE_SERVICE_H

#include "trigger_sequence_config.h"

#define TRIGGER_SEQUENCE_NO_INDEX UINT32_MAX

typedef enum {
    TRIGGER_SEQUENCE_TIMING_NONE = 0,
    TRIGGER_SEQUENCE_TIMING_PIO0 = 1
} trigger_sequence_timing_kind_t;

typedef enum {
    TRIGGER_SEQUENCE_SERVICE_OK = 0,
    TRIGGER_SEQUENCE_SERVICE_INVALID,
    TRIGGER_SEQUENCE_SERVICE_FROZEN,
    TRIGGER_SEQUENCE_SERVICE_NOT_READY,
    TRIGGER_SEQUENCE_SERVICE_BUSY,
    TRIGGER_SEQUENCE_SERVICE_SOURCE_MISMATCH,
    TRIGGER_SEQUENCE_SERVICE_CONFIG,
    TRIGGER_SEQUENCE_SERVICE_IO_CONFIG,
    TRIGGER_SEQUENCE_SERVICE_CODE_MISSING,
    TRIGGER_SEQUENCE_SERVICE_RESOURCE,
    TRIGGER_SEQUENCE_SERVICE_BACKEND,
    TRIGGER_SEQUENCE_SERVICE_EXHAUSTED
} trigger_sequence_service_result_t;

typedef enum {
    TRIGGER_SEQUENCE_SERVICE_IDLE = 0,
    TRIGGER_SEQUENCE_SERVICE_STARTING,
    TRIGGER_SEQUENCE_SERVICE_READY,
    TRIGGER_SEQUENCE_SERVICE_RUNNING,
    TRIGGER_SEQUENCE_SERVICE_PAUSING,
    TRIGGER_SEQUENCE_SERVICE_PAUSED,
    TRIGGER_SEQUENCE_SERVICE_STOPPING,
    TRIGGER_SEQUENCE_SERVICE_FAULT
} trigger_sequence_service_state_t;

typedef enum {
    TRIGGER_SEQUENCE_STATUS_LEVEL = 0,
    TRIGGER_SEQUENCE_STATUS_PULSE = 1,
} trigger_sequence_status_mode_t;

typedef struct {
    uint32_t source; /* 0=BUS; 1..4=logical IN. */
    bool falling;
    uint32_t sequence_output_mask;
    uint32_t status_output_mask;
    trigger_sequence_status_mode_t status_mode;
    uint32_t settle_us;
    uint32_t pulse_us;
    uint32_t generation;
    bool valid;
} trigger_sequence_service_io_t;

typedef struct {
    trigger_sequence_service_state_t state;
    trigger_sequence_service_result_t error;
    uint32_t run_id;
    uint32_t generation;
    uint32_t count;
    uint32_t current_index;
    uint32_t current_state;
    uint32_t next_index;
    uint32_t executed_index;
    uint32_t executed_state;
    uint32_t completed_index;
    uint32_t completed_state;
    uint32_t cycles;
    uint32_t accepted;
    uint32_t completed;
    uint32_t busy_rejected;
    uint32_t notready_rejected;
    uint32_t cancelled;
    uint32_t faults;
    uint32_t backend_fault;
    uint64_t written_at_us;
    uint64_t completion_rise_at_us;
    uint64_t completed_at_us;
    uint32_t alarm_late_us;
    uint32_t tick_ns;
    uint32_t timing_kind;
    bool rejection_counts_pending;
} trigger_sequence_service_status_t;

/* Init and configuration/command APIs belong to Core0. Init precedes Core1.
 * service() alone owns runtime and hardware on Core1. Queries copy snapshots.
 * Configuration pointers must never cross cores or outlive a configuration call. */
void trigger_sequence_service_init(void);
trigger_sequence_store_t *trigger_sequence_service_config(void);
trigger_sequence_service_result_t trigger_sequence_service_set_source(
    uint32_t source, bool falling);
trigger_sequence_service_result_t trigger_sequence_service_set_io(
    uint32_t output_mask, uint32_t completion_channel,
    uint32_t settle_us, uint32_t pulse_us);
trigger_sequence_service_result_t trigger_sequence_service_set_outputs(
    uint32_t sequence_output_mask, uint32_t status_output_mask,
    trigger_sequence_status_mode_t status_mode,
    uint32_t settle_us, uint32_t pulse_us);
trigger_sequence_service_result_t trigger_sequence_service_set_code(
    uint32_t state_id, uint32_t value);
void trigger_sequence_service_get_io(trigger_sequence_service_io_t *io);
bool trigger_sequence_service_get_code(uint32_t state_id, uint32_t *value);
trigger_sequence_service_result_t trigger_sequence_service_start(const char *plan_id);
trigger_sequence_service_result_t trigger_sequence_service_stop(void);
trigger_sequence_service_result_t trigger_sequence_service_pause(void);
trigger_sequence_service_result_t trigger_sequence_service_continue(void);
trigger_sequence_service_result_t trigger_sequence_service_step(void);
void trigger_sequence_service_service(void);
void trigger_sequence_service_get_status(trigger_sequence_service_status_t *status);
bool trigger_sequence_service_is_active(void);
const char *trigger_sequence_service_result_name(trigger_sequence_service_result_t result);
const char *trigger_sequence_service_state_name(trigger_sequence_service_state_t state);

#endif
