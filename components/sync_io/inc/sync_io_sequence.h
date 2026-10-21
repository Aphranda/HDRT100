#ifndef SYNC_IO_SEQUENCE_H
#define SYNC_IO_SEQUENCE_H

#include <stdbool.h>
#include <stdint.h>

/* Sequence-owned PIO0 state machines use a 250 MHz instruction clock. Duration
 * parameters remain whole microseconds; countdown packing must not overflow. */
#define SYNC_IO_SEQUENCE_TICK_NS 4u
#define SYNC_IO_SEQUENCE_TICKS_PER_US (1000u / SYNC_IO_SEQUENCE_TICK_NS)
/* Start later than base + this and the plan counts as late (one group step). */
#define SYNC_IO_SEQUENCE_LATE_TOLERANCE_TICKS 8000u
#define SYNC_IO_SEQUENCE_TIME_MAX_US (UINT32_MAX / SYNC_IO_SEQUENCE_TICKS_PER_US)
#define SYNC_IO_SEQUENCE_PLAN_MAX 256u
#define SYNC_IO_SEQUENCE_TIMING_PIO0 1u
#define SYNC_IO_SEQUENCE_PLAN_WORDS 3u
#define SYNC_IO_SEQUENCE_COUNTER_LIMIT (UINT32_MAX - 32u)

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
    /* Optional gateway, sharing this owner's SMA lease. Requires MANUAL/NONE.
     * Input zero selects software READY; 1..4 select external READY capture. */
    bool gateway_enabled;
    uint32_t gateway_input_channel;
    uint32_t gateway_output_mask;
    uint32_t gateway_pulse_us;
    bool gateway_falling;
    /* Finite admission quota is enforced in PIO for external inputs. Zero
     * steps still establishes and settles START's first state. */
    bool step_limit_enabled;
    /* Output-only role (the switch board): the model installs JUST the executor program, which
     * leaves the rest of the PIO instruction space to the capture persona. The full set needs 31
     * words and does not fit next to it (persona claim failed with CONFLICT_INSTRUCTION_SPACE). */
    bool executor_only;
    /* Absolute T2 time base. 0 = free-run (advance on settle alone). When set, the first plan step
     * waits until this local tick and every later step is anchored to base + group_deltas[], so a
     * late start is counted instead of silently stretching the schedule (review S05). */
    uint64_t base_deadline_tick;
    const uint64_t *group_deltas;   /* count entries, in clk_sys ticks from the base */
    uint32_t max_steps;
    /* Optional cumulative turntable input (1..4); zero disables it. Only
     * available with the gateway, distinct from its READY input. Both share
     * gateway_falling because they execute the same PIO instructions.
     * Starts after first-state priming and continues through busy/PAUSE. */
    uint32_t counter_input_channel;
    uint32_t counter_threshold;
    /* Route scaffolding for devices whose outputs are split into contiguous runs, each driven by
     * its own executor SM (the switch splits its output mask that way). Zero means "one route",
     * which is the current single-executor behaviour; the reserved profile is stored so the
     * migration can land without changing the wire/config contract again. See review 14.9. */
    uint8_t route_count;          /* 0 or 1 = single executor (today) */
    uint8_t route_base[2];        /* pin offset of each route, relative to the output base */
    uint8_t route_count_pins[2];  /* pins in each route */
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
    SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY,
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
    uint32_t counter_events;
    bool counter_busy;
    uint32_t counter_rearm_count;
    /* Which admission exit refused the arm when fault == RESOURCE (0 = none). */
    uint32_t fail_detail;
} sync_io_sequence_snapshot_t;

/* Core0 reservation is software-only and precedes the START mailbox.
 * Release is only for rollback before ARM. ARM failure and STOP release it.
 * All other lifecycle calls belong to Core1. Snapshot publication is atomic. */
bool sync_io_sequence_reserve(void);
/* True while THIS owner holds the SMA reservation (a teardown or stop releases it). */
bool sync_io_sequence_is_reserved(void);
void sync_io_sequence_release(void);
bool sync_io_sequence_arm_plan(const sync_io_sequence_config_t *config,
                               const uint32_t *values, uint32_t count);
/* Compact SMA codes use identical validation and PIO packing. */
bool sync_io_sequence_arm_plan_bytes(const sync_io_sequence_config_t *config,
                                    const uint8_t *values, uint32_t count);
bool sync_io_sequence_software_step(void);
/* Core1 owner only, after service: bounded submission without nested service. */
bool sync_io_sequence_software_step_prepared(void);
/* Core1 only. Arm fresh PIO READY capture and emit exactly one PIO pulse.
 * READY is a receipt only: it never directly requests a sequence step. */
bool sync_io_sequence_gateway_fire(void);
/* Core1 only. Complete the active gateway READY wait from the serialized
 * software command path. The subsequent DUT step still requires the TDMA
 * READY_NEXT return message. */
bool sync_io_sequence_gateway_ready(void);
/* Core1 only. Rearm after the entire triggered round has completed. Fails if
 * the next position threshold arrived while busy. Partial pulses accumulated
 * during sampling are preserved; neither total nor threshold base resets. */
bool sync_io_sequence_counter_rearm(void);
bool sync_io_sequence_counter_rearm_prepared(void);
/* Core1-only acceptance hook. Adds a simulated edge batch to the configured
 * position counter; threshold and fault handling remain shared with PIO. */
bool sync_io_sequence_counter_inject(uint32_t input_channel, uint32_t count);
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
