#ifndef TRIGGER_SEQUENCE_SERVICE_H
#define TRIGGER_SEQUENCE_SERVICE_H

#include "trigger_sequence_config.h"

/* Core0 configuration transaction only. LINK installs the TDMA owner gate
 * for combined-role hardware submissions; standalone backends need no gate. */
void trigger_sequence_service_set_transport_action_locked(
    bool (*dispatch)(bool (*action)(void), bool *result));

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
    TRIGGER_SEQUENCE_STATUS_NONE = 2,
} trigger_sequence_status_mode_t;

typedef struct {
    uint32_t source; /* 0=MANUAL; 1..4=logical IN. */
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
    bool enabled;
    uint32_t ready_input; /* 0=MANUAL; 1..4=logical IN. */
    bool falling;
    uint32_t trigger_output_mask;
    uint32_t pulse_us;
    uint32_t counter_input; /* 0 disables the third, position-driven mode. */
    uint32_t counter_threshold;
} trigger_sequence_gateway_config_t;

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
    bool gateway_waiting;
    bool gateway_pulse_busy;
    uint32_t gateway_trigger_count;
    uint32_t gateway_ready_count;
    uint32_t gateway_cancelled;
    uint32_t counter_events;
    bool counter_busy;
    uint32_t counter_rearm_count;
    uint32_t repeat_count;
    bool finished;
} trigger_sequence_service_status_t;

/* Init and configuration/command APIs belong to Core0. Init precedes Core1.
 * service() alone owns runtime and hardware on Core1. Queries copy snapshots.
 * Configuration pointers must never cross cores or outlive a configuration call. */
void trigger_sequence_service_init(void);
/* Serializes every configuration/model mutation against START. The gate is
 * non-reentrant and must be held until a complete mutation is committed. */
bool trigger_sequence_service_configuration_begin(void);
void trigger_sequence_service_configuration_end(void);
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
/* Acceptance/diagnostic input. Core0 posts only; Core1 applies the simulated
 * edge batch to the configured position counter through sync_io. */
trigger_sequence_service_result_t trigger_sequence_service_counter_inject(
    uint32_t input, uint32_t count);
void trigger_sequence_service_service(void);
void trigger_sequence_service_get_status(trigger_sequence_service_status_t *status);
bool trigger_sequence_service_is_active(void);
/* A bounded read of the command mailbox, before the owner publishes STOPPING.
 * Realtime coordinators must not submit another action over a pending STOP. */
bool trigger_sequence_service_stop_pending(void);
trigger_sequence_service_result_t trigger_sequence_service_set_repeat(uint32_t count);
/* Core0 only, with configuration_begin held; used for atomic linked setup. */
void trigger_sequence_service_set_repeat_locked(uint32_t count);
uint32_t trigger_sequence_service_get_repeat(void);
/* Core0 orchestration submits actions; Core1 remains the IO/runtime owner. */
trigger_sequence_service_result_t trigger_sequence_service_set_gateway(
    const trigger_sequence_gateway_config_t *config, bool (*start_guard)(void));
/* Caller must hold the configuration gate. */
trigger_sequence_service_result_t trigger_sequence_service_set_gateway_locked(
    const trigger_sequence_gateway_config_t *config, bool (*start_guard)(void));
trigger_sequence_service_result_t trigger_sequence_service_gateway_fire(
    uint32_t run, uint32_t generation, uint32_t step);
trigger_sequence_service_result_t trigger_sequence_service_gateway_ready(
    uint32_t run, uint32_t generation, uint32_t step);
trigger_sequence_service_result_t trigger_sequence_service_cycle_step(
    uint32_t run, uint32_t generation, uint32_t step);
trigger_sequence_service_result_t trigger_sequence_service_cycle_finish(
    uint32_t run, uint32_t generation, uint32_t step);
trigger_sequence_service_result_t trigger_sequence_service_counter_rearm(
    uint32_t run, uint32_t generation, uint32_t step);
const char *trigger_sequence_service_result_name(trigger_sequence_service_result_t result);
const char *trigger_sequence_service_state_name(trigger_sequence_service_state_t state);

#endif
