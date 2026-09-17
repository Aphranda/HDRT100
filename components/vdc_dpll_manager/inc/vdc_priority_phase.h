#ifndef VDC_PRIORITY_PHASE_H
#define VDC_PRIORITY_PHASE_H

#include <stdbool.h>
#include <stdint.h>

/* Debug acquisition coordinate steps. These are not a phase-slew or product
 * RUN/monotonic-output contract, nor evidence of GPIO phase lock. */
#define VDC_PRIORITY_PHASE_MIN_INTERVAL_MS 100u
#define VDC_PRIORITY_PHASE_MAX_DELTA_NS INT64_C(1000000000)

enum {
    VDC_PRIORITY_PHASE_DISABLED = 0u,
    VDC_PRIORITY_PHASE_WAITING = 1u,
    VDC_PRIORITY_PHASE_READY = 2u,
    VDC_PRIORITY_PHASE_APPLIED = 3u,
    VDC_PRIORITY_PHASE_HELD = 4u,
    VDC_PRIORITY_PHASE_REJECTED = 5u,
    VDC_PRIORITY_PHASE_RETIRED = 6u
};

enum {
    VDC_PRIORITY_PHASE_OK = 0u,
    VDC_PRIORITY_PHASE_OFF = 1u,
    VDC_PRIORITY_PHASE_BINDING = 2u,
    VDC_PRIORITY_PHASE_MODEL = 3u,
    VDC_PRIORITY_PHASE_AGE = 4u,
    VDC_PRIORITY_PHASE_DUPLICATE = 5u,
    VDC_PRIORITY_PHASE_INTERVAL = 6u,
    VDC_PRIORITY_PHASE_OVERFLOW = 7u,
    VDC_PRIORITY_PHASE_ZERO_CROSSING = 8u,
    VDC_PRIORITY_PHASE_RATE_PRIORITY = 9u,
    VDC_PRIORITY_PHASE_DOMAIN = 10u,
    VDC_PRIORITY_PHASE_BUSY = 11u,
    VDC_PRIORITY_PHASE_STOP = 12u,
    VDC_PRIORITY_PHASE_LEDGER = 13u,
    VDC_PRIORITY_PHASE_FORMAL_LOCK = 14u
};

/* STOP diagnostic copy. Only confirmed actual model publication increments
 * applied or emits a successful native PHASE row. Raw MATCH endpoints below
 * retain their original actual coordinates; cumulative_ns belongs to the
 * private rate_epoch normalization and never rewrites these observations. */
typedef struct {
    uint32_t schema, enabled, active, state, reason, request, session, generation;
    uint32_t calls, prepared, attempted, committed, applied, held, rejected, cancelled;
    uint32_t event_sequence, carrier_sequence, source_slot, target_slot;
    uint32_t before_model, after_model, before_dco, after_dco, rate_epoch, reserved;
    int64_t delta_ns, cumulative_ns, residual_lo, residual_hi;
    uint64_t before_base_vdc, after_base_vdc, raw_lo, raw_hi;
    uint64_t local_lo, local_hi, expected_lo, expected_hi, valid_from_raw;
} vdc_priority_phase_snapshot_t;
_Static_assert(sizeof(vdc_priority_phase_snapshot_t) == 208u,
    "phase diagnostics use 52 atomic words without implicit padding");

/* Core0 STOP-only request, default disabled; no Flash write. Core1 latches
 * it once at the first admitted fresh event of a new FOLLOW binding. */
bool vdc_dpll_manager_set_priority_follow_phase(bool enabled);
/* Atomic requested-value read; false preserves *enabled. */
bool vdc_dpll_manager_try_priority_follow_phase_enabled(bool *enabled);
/* Bounded seqlock/atomic-word diagnostic copy; false preserves *out. */
bool vdc_dpll_manager_get_priority_follow_phase(vdc_priority_phase_snapshot_t *out);

#endif
