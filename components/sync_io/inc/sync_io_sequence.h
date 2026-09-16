#ifndef SYNC_IO_SEQUENCE_H
#define SYNC_IO_SEQUENCE_H

#include <stdbool.h>
#include <stdint.h>

#define SYNC_IO_SEQUENCE_TIME_MAX_US (UINT32_MAX / 10u)
#define SYNC_IO_SEQUENCE_PLAN_MAX 256u
#define SYNC_IO_SEQUENCE_TICK_NS 100u
#define SYNC_IO_SEQUENCE_TIMING_PIO0 1u
#define SYNC_IO_SEQUENCE_PLAN_WORDS 3u

typedef enum {
    SYNC_IO_SEQUENCE_STATUS_LEVEL = 0,
    SYNC_IO_SEQUENCE_STATUS_PULSE = 1,
    SYNC_IO_SEQUENCE_STATUS_NONE = 2,
} sync_io_sequence_status_mode_t;

typedef struct {
    uint32_t input_channel;
    bool falling;
    uint32_t sequence_output_mask;
    uint32_t status_output_mask;
    sync_io_sequence_status_mode_t status_mode;
    uint32_t settle_us;
    uint32_t pulse_us;
    /* Optional gateway, sharing this owner's SMA lease. Requires BUS/NONE.
     * A zero input disables it. Output is one non-overlapping logical bit. */
    uint32_t gateway_input_channel;
    uint32_t gateway_output_mask;
    uint32_t gateway_pulse_us;
    bool gateway_falling;
    /* Finite admission quota is enforced in PIO for external inputs. Zero
     * steps still establishes and settles START's first state. */
    bool step_limit_enabled;
    uint32_t max_steps;
} sync_io_sequence_config_t;

typedef enum {
    SYNC_IO_SEQUENCE_FAULT_NONE = 0,
    SYNC_IO_SEQUENCE_FAULT_CONFIG,
    SYNC_IO_SEQUENCE_FAULT_RESOURCE,
    SYNC_IO_SEQUENCE_FAULT_ALARM,
    SYNC_IO_SEQUENCE_FAULT_DEADLINE,
    SYNC_IO_SEQUENCE_FAULT_CORE,
    SYNC_IO_SEQUENCE_FAULT_RECEIPT,
    SYNC_IO_SEQUENCE_FAULT_OVERFLOW,
    SYNC_IO_SEQUENCE_FAULT_DMA,
    SYNC_IO_SEQUENCE_FAULT_COUNTER_REGRESSION,
    SYNC_IO_SEQUENCE_FAULT_COUNTER_OVERFLOW,
    SYNC_IO_SEQUENCE_FAULT_RECEIPT_REGRESSION,
    SYNC_IO_SEQUENCE_FAULT_RECEIPT_CAPACITY,
    SYNC_IO_SEQUENCE_FAULT_RECEIPT_RACE,
} sync_io_sequence_fault_t;

typedef struct {
    bool armed;
    bool ready;
    bool pending;
    bool busy;
    uint32_t input_events;
    uint32_t busy_rejected;
    uint32_t notready_rejected;
    uint32_t cancelled;
    uint32_t written;
    uint32_t completed;
    uint32_t fault;
    uint32_t output_ownership_mask;
    uint32_t alarm_late_us;
    uint64_t written_at_us;
    uint64_t completion_rise_at_us;
    uint64_t completed_at_us;
    uint32_t accepted;
    uint32_t current_index;
    uint32_t completed_index;
    uint32_t plan_count;
    uint32_t tick_ns;
    uint32_t timing_kind;
    bool paused;
    bool rejection_counts_pending;
    bool gateway_waiting;
    bool gateway_pulse_busy;
    uint32_t gateway_trigger_count;
    uint32_t gateway_ready_count;
    uint32_t gateway_cancelled;
    bool finished;
} sync_io_sequence_snapshot_t;

/* Core0 reservation is software-only and precedes the START mailbox.
 * Release is only for rollback before ARM. ARM failure and STOP release it.
 * All other lifecycle calls belong to Core1. Snapshot publication is atomic. */
bool sync_io_sequence_reserve(void);
void sync_io_sequence_release(void);
bool sync_io_sequence_arm_plan(const sync_io_sequence_config_t *config,
                               const uint32_t *values, uint32_t count);
bool sync_io_sequence_software_step(void);
/* Core1 only. Arm fresh PIO READY capture and emit exactly one PIO pulse.
 * READY is a receipt only: it never directly requests a sequence step. */
bool sync_io_sequence_gateway_fire(void);
bool sync_io_sequence_pause(bool paused);
void sync_io_sequence_stop(void);
void sync_io_sequence_service(void);
void sync_io_sequence_get_snapshot(sync_io_sequence_snapshot_t *snapshot);
bool sync_io_sequence_is_armed(void);

/* Pure pad reads, permitted from either core. Logical channel 1 is bit 0. */
uint32_t sync_io_sequence_read_inputs(void);
uint32_t sync_io_sequence_read_outputs(void);
uint32_t sync_io_sequence_owned_mask(void);

/* Independent maintenance switch control.  The caller must be idle; the
 * operation claims the same SMA GPIO resource as the sequence engine and
 * therefore fails while a sequence (or another GPIO owner) is active. */
bool sync_io_sequence_set_switch(uint32_t switch_number, uint32_t value);
uint32_t sync_io_sequence_get_switch(uint32_t switch_number);

/* Existing SMA mutators take this transient lease around their entire call.
 * Nesting is confined to one core; never hold it across an asynchronous call. */
bool sync_io_sequence_legacy_begin(void);
void sync_io_sequence_legacy_end(void);

#endif
