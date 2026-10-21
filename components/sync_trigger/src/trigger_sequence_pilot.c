#include "trigger_sequence_pilot.h"

#include <string.h>

#include "sync_io_sequence.h"
#include "sync_io.h"
#include "trigger_t2_runtime.h"
#include "trigger_switch_service.h"
#include "trigger_gateway_service.h"

/* HAOFV split (see the header): SCPI writes the command slot, Core0 reserves and releases the
 * software-only SMA GPIO resource, Core1 owns every realtime model call. Each cross-core fact has
 * exactly one writer and is published with __atomic release/acquire. */

/* One SMA output carries the step value, a second one is the gateway trigger: the model rejects
 * a config whose sequence and gateway masks overlap, so they are kept apart. */
#define PILOT_SEQUENCE_BIT 0x1u
#define PILOT_GATEWAY_BIT  0x2u
#define PILOT_DEFAULT_US   8000u

typedef enum {
    PILOT_STATE_IDLE = 0u,
    PILOT_STATE_ARMING,
    PILOT_STATE_STEPPING,
    PILOT_STATE_WAITING,
    PILOT_STATE_DONE,
} pilot_state_t;

/* Command slot: written by Core0 (SCPI), read by the Core0 service and Core1. */
static uint32_t s_req_steps, s_req_pulse_us;
static volatile uint32_t s_req_sequence;      /* Core0 writer: bumped for every request */
static volatile uint32_t s_reserved_sequence; /* Core0 writer: request covered by the reservation */
/* Completion slot: written by Core1, read by the Core0 service. */
static volatile uint32_t s_done_sequence;
/* Core0-private bookkeeping. */
static uint32_t s_core0_reserved, s_core0_failures, s_reserve_reason;
/* Three-phase pin hand-over for mode 2: Core0 asks (bumping the request), Core1 unloads the legacy
 * switch persona and acknowledges, and only then may Core0 reserve. Reserving first made every
 * attempt collide with the persona still holding the pins (fault detail 8). */
static volatile uint32_t s_handback_request, s_handback_done;

/* Core1-private state machine. */
static pilot_state_t s_state;
static uint32_t s_index, s_errors, s_step_detail;
static bool s_fired;
/* Position flow switch (enabled from SCPI; default off). */
static uint32_t s_position_mode;
/* Inputs of the last position arm (reported for diagnosis). */
static uint32_t s_pos_pulse_us, s_pos_step_us, s_pos_count, s_pos_stage;
static bool s_pos_submitted;

/* Core1-published result copy (seqlock, single writer). */
static trigger_sequence_pilot_status_t s_status;
static volatile uint32_t s_status_guard;

/* Two throws: the SP2T's A and B. A plan value must be a subset of sequence_output_mask. */
static const uint32_t s_values[2] = {PILOT_SEQUENCE_BIT, 0u};

static void pilot_config(sync_io_sequence_config_t *c, uint32_t pulse_us)
{
    memset(c, 0, sizeof(*c));
    c->input_channel = 0u;             /* software driven: no external pulse */
    c->sequence_output_mask = PILOT_SEQUENCE_BIT;
    c->status_mode = SYNC_IO_SEQUENCE_STATUS_NONE;
    c->settle_us = 0u;
    c->pulse_us = 0u;
    c->gateway_enabled = true;
    c->gateway_input_channel = 0u;     /* software READY: no analyzer on the bench */
    c->gateway_output_mask = PILOT_GATEWAY_BIT;
    c->gateway_pulse_us = pulse_us;
    c->gateway_falling = false;
    c->step_limit_enabled = false;
    c->counter_input_channel = 0u;
    c->counter_threshold = 0u;
}

static void pilot_publish(void)
{
    const uint32_t guard = s_status_guard + 1u;      /* odd: write in progress */
    __atomic_store_n(&s_status_guard, guard, __ATOMIC_RELEASE);
    sync_io_sequence_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    sync_io_sequence_get_snapshot(&snap);
    s_status.state = (uint32_t)s_state;
    s_status.steps = s_req_steps;
    s_status.steps_done = s_index;
    s_status.errors = s_errors + s_core0_failures;
    s_status.pulse_us = s_req_pulse_us;
    s_status.busy = s_state != PILOT_STATE_IDLE && s_state != PILOT_STATE_DONE;
    s_status.armed = snap.armed;
    s_status.gateway_pulse_busy = snap.gateway_pulse_busy;
    s_status.gateway_waiting = snap.gateway_waiting;
    s_status.gateway_trigger_count = snap.gateway_trigger_count;
    s_status.gateway_ready_count = snap.gateway_ready_count;
    s_status.written = snap.written;
    s_status.completed = snap.completed;
    s_status.accepted = snap.accepted;
    s_status.fault = snap.fault;
    s_status.reserve_reason = s_reserve_reason;
    s_status.requested = s_req_sequence;
    s_status.reserved = s_reserved_sequence;
    s_status.fault_detail = snap.fail_detail;
    s_status.step_detail = s_step_detail;
    s_status.pos_pulse_us = s_pos_pulse_us;
    s_status.pos_step_us = s_pos_step_us;
    s_status.pos_count = s_pos_count;
    s_status.pos_stage = s_pos_stage;
    __atomic_store_n(&s_status_guard, guard + 1u, __ATOMIC_RELEASE);
}

bool trigger_sequence_pilot_request(uint32_t steps, uint32_t pulse_us)
{
    if (steps == 0u || steps > 2u) return false;
    /* A queued or running request keeps the slot busy. */
    if (__atomic_load_n(&s_req_sequence, __ATOMIC_ACQUIRE) !=
        __atomic_load_n(&s_done_sequence, __ATOMIC_ACQUIRE)) return false;
    s_req_steps = steps;
    s_req_pulse_us = pulse_us != 0u ? pulse_us : PILOT_DEFAULT_US;
    s_errors = 0u;
    s_index = 0u;
    __atomic_store_n(&s_req_sequence, s_req_sequence + 1u, __ATOMIC_RELEASE);
    return true;
}

void trigger_sequence_pilot_service_core0(void)
{
    /* Position flow: the model drives every position, so the SMA GPIO reservation is held for the
     * whole run instead of per pilot command. Without it Core1's arm reports RESOURCE (detail 8). */
    if (s_position_mode) {
        /* Mode 2 must wait for Core1's acknowledgement before touching the arbiter. */
        if (__atomic_load_n(&s_handback_done, __ATOMIC_ACQUIRE) !=
            __atomic_load_n(&s_handback_request, __ATOMIC_ACQUIRE)) return;
        if (s_core0_reserved == 0u || !sync_io_sequence_is_reserved()) {
            sync_io_sequence_release();
            if (sync_io_sequence_reserve()) {
                s_core0_reserved = 1u;
            } else {
                /* Record WHY: without this the position flow only showed "reserved=0" and every
                 * arm failed with detail 8, hiding which precondition rejected the reservation. */
                if (s_core0_failures != UINT32_MAX) ++s_core0_failures;
                s_reserve_reason = sync_io_sequence_outputs_blockers();
            }
            __atomic_store_n(&s_reserved_sequence, s_core0_reserved, __ATOMIC_RELEASE);
        }
        return;
    }
    const uint32_t requested = __atomic_load_n(&s_req_sequence, __ATOMIC_ACQUIRE);
    if (requested == 0u) return;
    if (__atomic_load_n(&s_done_sequence, __ATOMIC_ACQUIRE) == requested) return;  /* served */
    if (requested != s_core0_reserved) {
        /* A previous run that ended without a matching done_sequence leaves the arbiter bit
         * acquired, and every later acquire then reports a conflict (outputs look free, reason 0).
         * Release the stale reservation before asking for a new one. */
        sync_io_sequence_release();
        if (sync_io_sequence_reserve()) {
            s_core0_reserved = requested;
            __atomic_store_n(&s_reserved_sequence, requested, __ATOMIC_RELEASE);
        } else {
            if (s_core0_failures != UINT32_MAX) ++s_core0_failures;
            /* Name the rejected precondition once instead of guessing. */
            s_reserve_reason = sync_io_sequence_outputs_blockers();
        }
        return;
    }
    /* Release once Core1 finished the run this reservation covered. */
    if (__atomic_load_n(&s_done_sequence, __ATOMIC_ACQUIRE) == s_core0_reserved) {
        sync_io_sequence_release();
        s_core0_reserved = 0u;
        __atomic_store_n(&s_reserved_sequence, 0u, __ATOMIC_RELEASE);
    }
}

/* Position-flow state (declared before the Core1 service that uses it). */
static bool s_pos_armed[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static bool s_pos_gateway;
static uint32_t s_pos_completed_base;
static uint32_t s_pos_plan_sig, s_pos_plan_sig_valid;
static uint32_t s_pos_slot, s_pos_steps, s_pos_index;

void trigger_sequence_pilot_service(void)
{
    /* Idle fast path: this runs on every Core1 service pass. Publishing a snapshot each time put a
     * seqlock copy into the realtime loop even when no pilot run existed, which showed up as the
     * legacy gateway path dropping to a few positions per scan. */
    if (s_position_mode == 0u &&
        __atomic_load_n(&s_req_sequence, __ATOMIC_ACQUIRE) ==
        __atomic_load_n(&s_done_sequence, __ATOMIC_ACQUIRE) &&
        s_state == PILOT_STATE_IDLE) return;
    /* A refused reservation usually means a legacy persona still owns the SMA pins (blocker bits
     * 4/5: wave persona active, pin not in SIO). Hand both personas back so Core0 can reserve. */
    if (s_position_mode != 0u && (__atomic_load_n(&s_reserve_reason, __ATOMIC_ACQUIRE) & 0x30u) != 0u) {
        (void)trigger_gateway_force_unload();
        (void)trigger_switch_force_unload();
        __atomic_store_n(&s_reserve_reason, 0u, __ATOMIC_RELEASE);
    }
    const uint32_t handback = __atomic_load_n(&s_handback_request, __ATOMIC_ACQUIRE);
    if (handback != __atomic_load_n(&s_handback_done, __ATOMIC_ACQUIRE)) {
        /* Mode changed: Core1 owns the model lifecycle and the persona licences. */
        sync_io_sequence_stop();
        sync_io_sequence_release();
        s_pos_armed[0] = s_pos_armed[1] = s_pos_armed[2] = false;
        s_pos_plan_sig_valid = 0u;
        if (s_position_mode >= 2u && trigger_switch_is_loaded()) (void)trigger_switch_force_unload();
        __atomic_store_n(&s_handback_done, handback, __ATOMIC_RELEASE);
    }
    const uint32_t requested = __atomic_load_n(&s_req_sequence, __ATOMIC_ACQUIRE);
    const uint32_t reserved = __atomic_load_n(&s_reserved_sequence, __ATOMIC_ACQUIRE);

    if (s_state == PILOT_STATE_IDLE && requested != 0u &&
        requested != __atomic_load_n(&s_done_sequence, __ATOMIC_ACQUIRE)) {
        if (reserved == requested) {
            s_state = PILOT_STATE_ARMING;   /* Core0 holds the resource */
        } else {
            /* Core1 owns the realtime side of the ownership transition: a persisted sequence
             * persona still holds the SMA outputs on an idle board, so the model cannot claim
             * them until it is disarmed. Doing it here (not in the SCPI path) keeps HAOFV's
             * "core0 only expresses intent" rule; Core0's reserve retry then succeeds. */
            if (sync_io_seq_step_is_running()) sync_io_seq_step_disarm();
            pilot_publish();
            return;
        }
    }
    switch (s_state) {
    case PILOT_STATE_ARMING: {
        sync_io_sequence_config_t config;
        pilot_config(&config, s_req_pulse_us);
        if (!sync_io_sequence_arm_plan(&config, s_values, s_req_steps)) {
            ++s_errors;
            s_state = PILOT_STATE_DONE;
            break;
        }
        s_state = PILOT_STATE_STEPPING;
        break;
    }
    case PILOT_STATE_STEPPING:
        if (s_index >= s_req_steps) { s_state = PILOT_STATE_DONE; break; }
        /* software_step() = service() + prepared submission, and it CLEARS READY: the model's own
         * contract expects the next admission (raised by the executor after a service pass) before
         * the gateway may fire. */
        if (!sync_io_sequence_software_step()) {
            s_step_detail = 1u; ++s_errors; s_state = PILOT_STATE_DONE; break; }
        s_fired = false;
        s_state = PILOT_STATE_WAITING;
        break;
    case PILOT_STATE_WAITING: {
        const uint32_t seq = __atomic_load_n(&s_req_sequence, __ATOMIC_ACQUIRE);
        (void)seq;
        sync_io_sequence_service();               /* advance the model, refresh READY */
        sync_io_sequence_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        sync_io_sequence_get_snapshot(&snap);
        if (!s_fired) {
            /* READY only rises once the executor admits the submitted step. */
            if (!snap.ready) break;
            if (!sync_io_sequence_gateway_fire()) {
                s_step_detail = 2u; ++s_errors; s_state = PILOT_STATE_DONE; break; }
            s_fired = true;
            s_step_detail = 0u;
            break;
        }
        if (snap.gateway_pulse_busy) break;       /* pulse still high */
        if (!sync_io_sequence_gateway_ready()) {
            s_step_detail = 3u; ++s_errors; s_state = PILOT_STATE_DONE; break; }
        s_step_detail = 0u;
        ++s_index;
        s_state = PILOT_STATE_STEPPING;
        break;
    }
    case PILOT_STATE_DONE:
        /* STOP releases the model's own leases; the SMA GPIO reservation belongs to Core0. */
        sync_io_sequence_stop();
        __atomic_store_n(&s_done_sequence, requested, __ATOMIC_RELEASE);
        s_state = PILOT_STATE_IDLE;
        break;
    case PILOT_STATE_IDLE:
    default:
        break;
    }
    pilot_publish();
}

void trigger_sequence_pilot_status(trigger_sequence_pilot_status_t *out)
{
    if (!out) return;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s_status_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;                   /* a publish is in flight */
        *out = s_status;
        if (__atomic_load_n(&s_status_guard, __ATOMIC_ACQUIRE) == before) return;
    }
    *out = s_status;                                 /* best effort after 4 attempts */
}


/* ------------------------- position flow (GATEWAY role) ------------------------- */

void trigger_t2_runtime_external_complete(uint32_t index);

static bool s_pos_fired;

uint32_t trigger_sequence_pilot_position_mode_get(void)
{
    return s_position_mode;
}

bool trigger_sequence_pilot_switch_enabled(void)
{
    return s_position_mode >= 2u;
}

void trigger_sequence_pilot_position_mode(uint32_t mode)
{
    const bool enabled = mode != 0u;
    if (!enabled && s_position_mode && s_core0_reserved != 0u) {
        sync_io_sequence_stop();
        sync_io_sequence_release();
        s_core0_reserved = 0u;
        __atomic_store_n(&s_reserved_sequence, 0u, __ATOMIC_RELEASE);
    }
    if (s_position_mode != mode) {
        /* S06: stop()/release() are Core1-only, so this Core0 path may only ASK. The request is
         * serviced by trigger_sequence_pilot_service() on Core1, which stops the model, releases
         * the lease and (for mode>=2) hands the pins back, then acknowledges. Core0 waits for the
         * acknowledgement before touching the arbiter, so rolling back to the legacy path no longer
         * needs a reboot. */
        s_core0_reserved = 0u;
        __atomic_store_n(&s_reserved_sequence, 0u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&s_handback_request, 1u, __ATOMIC_RELEASE);
    }
    s_position_mode = mode;
}

bool trigger_sequence_pilot_position_enabled(void)
{
    return s_position_mode;
}

bool trigger_sequence_pilot_position_arm(uint32_t slot, const uint32_t *values, uint32_t count,
                                         uint32_t pulse_us, uint32_t step_us, bool gateway_role,
                                         uint64_t base_deadline_tick, const uint64_t *deltas)
{
    if (s_position_mode == 0u || slot >= TRIGGER_T2_RUNTIME_POSITION_COUNT) {
        s_step_detail = s_position_mode == 0u ? 1u : 2u;   /* 1 = model off, 2 = slot out of range */
        return false;
    }
    /* Mode 2: the legacy switch persona owns the same SMA pins. Hand them back first (Core1), or
     * sync_io_sequence_reserve() keeps failing with RESOURCE (detail 8) on every position. */
    if (s_position_mode >= 2u && trigger_switch_is_loaded()) {
        /* A refused release is not fatal any more: the model can still take the outputs (it
         * normalises the pin functions itself). Record it and try - refusing here made every
         * position fail while the model was perfectly able to run. */
        if (!trigger_switch_force_unload()) s_step_detail = 4u;
    }
    s_pos_pulse_us = pulse_us; s_pos_step_us = step_us; s_pos_count = count;
    if (pulse_us == 0u) pulse_us = 8000u;
    if (step_us < pulse_us) step_us = pulse_us + 3u;
    /* A position may carry any number of groups (16 on the switch, 2 on the SP2T gateway). */
    if (count == 0u || count > SYNC_IO_SEQUENCE_PLAN_MAX || values == NULL) return false;
    sync_io_sequence_config_t config;
    memset(&config, 0, sizeof(config));
    config.input_channel = 0u;
    /* The model's step period is settle + pulse; T2's group step is their sum.
     * GATEWAY: one throw per step on SMA1 plus a triggered pulse on SMA2.
     * SWITCH : the group code word is written to the four outputs and held for the step, so the
     *          config carries no gateway and the whole output mask (status_mode NONE). */
    if (gateway_role) {
        config.sequence_output_mask = PILOT_SEQUENCE_BIT;
        config.status_mode = SYNC_IO_SEQUENCE_STATUS_NONE;
        config.settle_us = step_us > pulse_us ? step_us - pulse_us : 0u;
        config.pulse_us = 0u;
        config.gateway_enabled = true;
        config.gateway_input_channel = 0u;
        config.gateway_output_mask = PILOT_GATEWAY_BIT;
        config.gateway_pulse_us = pulse_us;
    } else {
        config.sequence_output_mask = 0xFu;         /* all four public outputs */
        config.status_mode = SYNC_IO_SEQUENCE_STATUS_NONE;
        config.settle_us = step_us ? step_us : 8003u;
        config.pulse_us = 0u;
        config.gateway_enabled = false;
        config.gateway_input_channel = 0u;
        config.gateway_output_mask = 0u;
        config.gateway_pulse_us = 0u;
        /* Pure-output role: install only the executor so the capture persona keeps its PIO space
         * (the full 31-word set did not fit: CONFLICT_INSTRUCTION_SPACE). */
        config.executor_only = true;
    }
    config.step_limit_enabled = false;
    /* Absolute T2 time base: the model anchors each step to it and counts a late start (S05). */
    config.base_deadline_tick = base_deadline_tick;
    config.group_deltas = deltas;
    s_pos_gateway = gateway_role;
    /* A new position may carry different code words (the switch does): reusing the armed plan made
     * every position after the first run with the first position's values (S03). Stop and re-arm
     * when the signature changes; Core1 owns stop(), and this runs on Core1. */
    uint32_t sig = count;
    for (uint32_t k = 0u; k < count; ++k) sig = sig * 16777619u ^ values[k];
    if (sync_io_sequence_is_armed() && s_pos_plan_sig_valid && s_pos_plan_sig != sig)
        sync_io_sequence_stop();
    if (!sync_io_sequence_is_armed() && !sync_io_sequence_arm_plan(&config, values, count)) return false;
    s_pos_plan_sig = sig;
    s_pos_plan_sig_valid = 1u;
    {
        sync_io_sequence_snapshot_t armed_snap;
        memset(&armed_snap, 0, sizeof(armed_snap));
        sync_io_sequence_get_snapshot(&armed_snap);
        s_pos_completed_base = armed_snap.completed;
    }
    s_pos_slot = slot;
    s_pos_steps = count;
    s_pos_index = 0u;
    s_pos_fired = false;
    s_pos_submitted = false;
    s_pos_armed[slot] = true;
    return true;
}

bool trigger_sequence_pilot_position_service(uint32_t slot)
{
    if (s_position_mode == 0u || slot != s_pos_slot || !s_pos_armed[slot]) return false;
    sync_io_sequence_service();
    sync_io_sequence_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    sync_io_sequence_get_snapshot(&snap);
    if (!s_pos_fired) {
        if (!s_pos_submitted) {
            /* software_step() clears READY, so the fire must wait for the next admission to raise
             * it again: submitting and firing in the same pass refused every time (stage 3, 17k
             * steps written, zero pulses). */
            if (!sync_io_sequence_software_step()) { s_pos_stage = 1u; return false; }
            s_pos_submitted = true;
            s_pos_stage = 2u;
            return false;
        }
        if (!snap.ready && !sync_io_sequence_is_armed()) { s_pos_stage = 2u; return false; }
        if (!snap.ready) { s_pos_stage = 2u; return false; }
        if (!sync_io_sequence_gateway_fire()) { s_pos_stage = 3u; return false; }
        s_pos_submitted = false;
        s_pos_fired = true;
        s_pos_stage = 4u;
        return false;
    }
    if (snap.gateway_pulse_busy) { s_pos_stage = 4u; return false; }
    (void)sync_io_sequence_gateway_ready();
    ++s_pos_index;
    s_pos_fired = false;
    if (s_pos_index < s_pos_steps) { s_pos_stage = 1u; return false; }
    s_pos_armed[slot] = false;
    /* Keep the model armed across positions: stop() releases the model's own reservation and the
     * next position's arm was then refused with RESOURCE (detail 8). The plan is identical for
     * every position (two throws), so it is reused as is; leaving position mode stops it. */
    s_pos_stage = 5u;
    return true;
}
