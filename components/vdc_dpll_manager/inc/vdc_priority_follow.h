#ifndef VDC_PRIORITY_FOLLOW_H
#define VDC_PRIORITY_FOLLOW_H

#include <stdbool.h>
#include <stdint.h>

/* Provisional output-frequency control, not phase lock or a precision grant.
 * The two source events have immutable actual-output intervals. No remote
 * affine-model token is present in, or reconstructed from, the typed body. */
#define VDC_PRIORITY_FOLLOW_MIN_INTERVAL_NS UINT64_C(1000000000)
#define VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS UINT64_C(2000000000)
#define VDC_PRIORITY_FOLLOW_MAX_AGE_MS 120u

enum {
    VDC_PRIORITY_FOLLOW_DISABLED = 0u, VDC_PRIORITY_FOLLOW_AWAITING = 1u,
    VDC_PRIORITY_FOLLOW_BASELINE = 2u, VDC_PRIORITY_FOLLOW_WAITING = 3u,
    VDC_PRIORITY_FOLLOW_READY = 4u, VDC_PRIORITY_FOLLOW_APPLIED = 5u,
    VDC_PRIORITY_FOLLOW_NO_ADJUST = 6u, VDC_PRIORITY_FOLLOW_RETIRED = 7u
};
enum {
    VDC_PRIORITY_FOLLOW_OK = 0u, VDC_PRIORITY_FOLLOW_OFF = 1u,
    VDC_PRIORITY_FOLLOW_MODE = 2u, VDC_PRIORITY_FOLLOW_BINDING = 3u,
    VDC_PRIORITY_FOLLOW_MODEL = 4u, VDC_PRIORITY_FOLLOW_AGE = 5u,
    VDC_PRIORITY_FOLLOW_DUPLICATE = 6u, VDC_PRIORITY_FOLLOW_INTERVAL = 7u,
    VDC_PRIORITY_FOLLOW_OVERFLOW = 8u, VDC_PRIORITY_FOLLOW_DEADBAND = 9u,
    VDC_PRIORITY_FOLLOW_DOMAIN = 10u, VDC_PRIORITY_FOLLOW_BUSY = 11u,
    VDC_PRIORITY_FOLLOW_STOP = 12u
};

/* Stable diagnostic field order for STOP readback. All counters saturate.
 * before/after are actual local DCO facts, never a predicted remote model.
 * active is historical publication state; this getter is no actuator grant.
 * Baseline/event identify the last prepared two-event secant; missing frames
 * need not be consecutive and do not authorize synthetic observations. */
typedef struct {
    uint32_t schema, active, mode, state, last_reason, request, session, generation;
    uint32_t calls, tickets, baselines, waits, prepared, applied, no_adjust;
    uint32_t rejected, cancelled, repeated;
    uint32_t baseline_sequence, event_sequence, carrier_sequence, observer_epoch, rx_epoch;
    uint32_t model_token, expected_dco_seq, before_dco_seq, after_dco_seq;
    uint32_t local_slot, reference_slot, reserved;
    int32_t before_ppb, after_ppb, selected_delta_ppb, reserved_signed;
    uint64_t arm_epoch, baseline_raw, raw_lo, raw_hi, interval_lo, interval_hi;
    uint64_t local_interval_lo, local_interval_hi;
    int64_t error_lo_ppb, error_hi_ppb;
} vdc_priority_follow_snapshot_t;

/* Core0 STOP-only mode change. Enabling requires a nonzero MATCH request
 * already bound to the current feedback session. Mutually exclusive with
 * boundary probe/auto, old LOCAL_FOLLOW and remote follower commands. */
bool vdc_dpll_manager_set_priority_follow(bool enabled);
/* False preserves *enabled; concurrent publication is not disabled mode. */
bool vdc_dpll_manager_try_priority_follow_enabled(bool *enabled);
/* One bounded atomic-word copy; false preserves *out. Diagnostic only. */
bool vdc_dpll_manager_get_priority_follow(vdc_priority_follow_snapshot_t *out);

#endif
