#ifndef TRIGGER_SEQUENCE_PILOT_H
#define TRIGGER_SEQUENCE_PILOT_H

#include <stdbool.h>
#include <stdint.h>

/* Gateway pilot for the unified sequence base model (sync_io_sequence).
 *
 * HAOFV layering (docs/arch/HAOFV_ARCHITECTURE.md):
 *  - SCPI only expresses intent: trigger_sequence_pilot_request() writes a command slot and
 *    nothing else (no resource claim, no hardware access).
 *  - Core0 owns the software-only reservation of the SMA GPIO resource and its release.
 *  - Core1 owns every realtime lifecycle call of the model (arm, step, fire, stop).
 *  - Cross-core facts travel through generation counters published with __atomic
 *    release/acquire; the result copy is a seqlock snapshot with a single writer (Core1).
 * No position is admitted and no T2 state is touched. */

typedef struct {
    uint32_t state;          /* 0 idle, 1 arming, 2 stepping, 3 waiting, 4 done */
    uint32_t steps;          /* steps requested by the last run */
    uint32_t steps_done;     /* steps that completed a pulse round */
    uint32_t errors;         /* rejected arm/step/fire transitions */
    uint32_t pulse_us;
    bool busy;               /* a run is in progress */
    bool armed;
    bool gateway_pulse_busy;
    bool gateway_waiting;
    uint32_t gateway_trigger_count;
    uint32_t gateway_ready_count;
    uint32_t written;
    uint32_t completed;
    uint32_t accepted;
    uint32_t fault;
    uint32_t reserve_reason;  /* why Core0 could not reserve (0 = ok) */
    uint32_t requested;       /* Core0 command-slot generation */
    uint32_t reserved;        /* generation Core0 covered with a reservation */
    uint32_t fault_detail;    /* model admission exit code when fault == RESOURCE */
    uint32_t step_detail;     /* last refused step call: 1 software_step, 2 gateway_fire, 3 gateway_ready */
    uint32_t pos_pulse_us;    /* pulse width used by the last position arm */
    uint32_t pos_step_us;     /* step spacing used by the last position arm */
    uint32_t pos_count;       /* steps the last position arm asked for */
    uint32_t pos_stage;       /* last early return of the position service: 1 step, 2 ready, 3 fire, 4 pulse, 5 done */
} trigger_sequence_pilot_status_t;

/* Core0/SCPI: write the command slot for one pilot run. Returns false while a run is active. */
bool trigger_sequence_pilot_request(uint32_t steps, uint32_t pulse_us);

/* Core0 domain service: reserve the SMA GPIO resource for the queued run and release it once
 * Core1 reports the run finished. Software-only; never touches the realtime path. */
void trigger_sequence_pilot_service_core0(void);

/* Core1: drive the pilot state machine. Safe to call every service pass. */
void trigger_sequence_pilot_service(void);

/* Either core: read the published result copy (seqlock). */
void trigger_sequence_pilot_status(trigger_sequence_pilot_status_t *out);


/* ---- Position flow: drive one T2 position from the unified model (GATEWAY role) ----
 * Enabled from SCPI; when enabled the runtime asks this module to execute a position instead of
 * staging groups through sync_io_gateway. Default off, so the verified path stays the default. */

/* 0 = legacy backends, 1 = GATEWAY on the model, 2 = GATEWAY + SWITCH on the model. */
void trigger_sequence_pilot_position_mode(uint32_t mode);
bool trigger_sequence_pilot_position_enabled(void);
bool trigger_sequence_pilot_switch_enabled(void);
uint32_t trigger_sequence_pilot_position_mode_get(void);

/* Arm one position: `value` is the pin pattern of step 0, `pulse_us` the trigger width and
 * `step_us` the distance between two throws (settle + pulse for the model). Core1 only. */
bool trigger_sequence_pilot_position_arm(uint32_t slot, const uint32_t *values, uint32_t count,
                                         uint32_t pulse_us, uint32_t step_us, bool gateway_role,
                                         uint64_t base_deadline_tick, const uint64_t *deltas);

/* Advance the armed position. Returns true once every step completed (the runtime may then
 * retire the position). Core1 only; safe to call every service pass. */
bool trigger_sequence_pilot_position_service(uint32_t slot);

#endif
