#ifndef VDC_PRIORITY_MATCH_H
#define VDC_PRIORITY_MATCH_H

#include <stdbool.h>
#include <stdint.h>

enum {
    VDC_PRIORITY_MATCH_OK = 0u, VDC_PRIORITY_MATCH_DISABLED = 1u,
    VDC_PRIORITY_MATCH_RETIRED = 2u, VDC_PRIORITY_MATCH_CONFIG = 3u,
    VDC_PRIORITY_MATCH_ROLE = 4u, VDC_PRIORITY_MATCH_BINDING = 5u,
    VDC_PRIORITY_MATCH_MODEL = 6u, VDC_PRIORITY_MATCH_RX = 7u,
    VDC_PRIORITY_MATCH_GENERATION = 8u, VDC_PRIORITY_MATCH_SOURCE = 9u,
    VDC_PRIORITY_MATCH_PENDING = 10u, VDC_PRIORITY_MATCH_EVICTED = 11u,
    VDC_PRIORITY_MATCH_BUSY = 12u, VDC_PRIORITY_MATCH_SEQUENCE = 13u,
    VDC_PRIORITY_MATCH_PROJECTION = 14u, VDC_PRIORITY_MATCH_CHANGED = 15u,
    VDC_PRIORITY_MATCH_OVERFLOW = 16u, VDC_PRIORITY_MATCH_PATH = 17u,
    VDC_PRIORITY_MATCH_STOP = 18u
};
#define VDC_PRIORITY_MATCH_PATH_PROVISIONAL_TRANSPOSE 1u

/* Core1 observational matching only: no DCO command or lock/precision grant.
 * Counter meanings are service attempts except matched (at most once per
 * source event) and superseded (an unresolved candidate replaced by another).
 * Identity/interval fields describe the last successful match and survive
 * retirement. A different fresh request clears them and all counters.
 * The path is a provisional forward-CS scalar obtained by transposing the
 * installed reverse-DATA matrix, not a calibrated CS endpoint interval. */
typedef struct {
    uint32_t schema, generation, active, retired, have_match;
    uint32_t calls, matched, repeated, superseded, busy, pending, history_miss;
    uint32_t rejected, last_reason, history_result;
    uint32_t source_slot, event_sequence, carrier_sequence, rx_epoch;
    uint32_t session, role_generation, clock_epoch, clock_run, model_token, applied_command_seq;
    uint32_t ring_config_seq, ring_applied_seq, local_slot, reference_slot, node_count;
    uint32_t schedule_crc32, profile_crc32, observer_epoch, tick_hz, path_crc32, delay_ns;
    uint32_t path_direction, path_qualification;
    uint64_t arm_epoch, valid_from_raw, raw_lo, raw_hi;
    uint64_t local_lo, local_hi, remote_lo, remote_hi, expected_lo, expected_hi;
    int64_t residual_lo, residual_hi;
    /* Sticky first non-lifecycle rejection of this request: the observation
     * never reached `bound`, and `rejected` alone cannot say why. Zero means
     * nothing latched; DISABLED/STOP are never latched because they describe
     * arming/teardown, not an observation failure. Appended last so every
     * existing snapshot and SCPI field index is unchanged. */
    uint32_t first_reject_reason;
} vdc_priority_match_snapshot_t;

/* Core0, stopped metadata gate; nonzero requires an existing feedback session
 * and must exceed every previously accepted nonzero request. The session is
 * latched with that request; any later session change retires it. Zero disables.
 * Before the first successful match, the request may await its first ARM.
 * active describes the last service, not a live grant after STOP. */
bool vdc_dpll_manager_set_priority_match(uint32_t expected_remote_generation);
uint32_t vdc_dpll_manager_priority_match_generation(void);
/* One bounded atomic-word read; false preserves *out. Reader may not span
 * 2^31 producer publications (one complete guard cycle). STOP diagnostics. */
bool vdc_dpll_manager_get_priority_match(vdc_priority_match_snapshot_t *out);
/* Core1 DPLL boundary before opening the committed-model writer guard. */
void vdc_priority_match_core1(void);

/* FOLLOWER lock from the internal observation (VDC_DOMAIN_TODO.md
 * VDC-LOCKPUB-001). The matcher above only forms the error observation; this
 * small word is the owner's declaration derived from it: the residual interval
 * [residual_lo, residual_hi] is this follower's phase error against NO1's
 * published event time, so its CENTER is the lock quantity. The interval width
 * is the measurement uncertainty and already exceeds 2 x FINE on the bench, so
 * "whole interval inside +/-FINE" is never asked; the width only rejects a
 * broken measurement. Thresholds and counts are defaults pending user ruling. */
/* ENTER is VDC_DOMAIN_LOCK_TIER_FINE_NS (bound by a static assert in the
 * matcher), kept literal so this header stays self-contained. */
#define VDC_PRIORITY_LOCK_ENTER_NS 100u
#define VDC_PRIORITY_LOCK_EXIT_NS (2u * VDC_PRIORITY_LOCK_ENTER_NS)
#define VDC_PRIORITY_LOCK_WIDTH_LIMIT_NS (4u * VDC_PRIORITY_LOCK_ENTER_NS)
#define VDC_PRIORITY_LOCK_ENTER_COUNT 4u
#define VDC_PRIORITY_LOCK_EXIT_COUNT 3u
/* Same window as the Domain sample freshness default (1 s). */
#define VDC_PRIORITY_LOCK_STALE_NS UINT64_C(1000000000)

typedef struct {
    uint32_t locked;        /* hysteresis state; read through _is_live() */
    uint32_t in_band_run;   /* consecutive in-band observations */
    uint32_t out_band_run;  /* consecutive out-of-band observations */
    int32_t center_ns;      /* last observation, saturated */
    uint32_t width_ns;      /* last observation, saturated */
    uint32_t event_sequence;
    uint64_t last_match_ns; /* TIMER1 ns of the last new match; 0 = none */
    /* Live observation evidence (VDC-ISO-001): what the matcher's own input
     * predicate saw on the latest service - the event-live flags and the anchor
     * fields `priority_match_live_valid()` requires. A follower whose records
     * arrive but never match needs exactly this: which of RETAINED / ACTIVE /
     * ANCHOR_VALID, and which anchor term, was missing. Appended: existing
     * positions are unchanged. */
    uint32_t live_flags;
    uint32_t live_valid;          /* priority_match_live_valid() verdict */
    uint32_t live_arm_epoch_lo;
    uint32_t live_observer_epoch;
    uint32_t live_tick_hz;
    uint32_t live_bracket_ok;     /* enable_after >= enable_before and nonzero */
    uint32_t live_samples;        /* services that evaluated the predicate */
} vdc_priority_lock_word_t;

/* One new matched event. A gap longer than the stale window restarts the
 * acquisition, an unreadable clock or inverted interval fails closed. */
static inline void vdc_priority_lock_observe(vdc_priority_lock_word_t *word,
    int64_t residual_lo, int64_t residual_hi, uint32_t event_sequence,
    uint64_t now_ns)
{
    if (now_ns == UINT64_MAX || now_ns == 0u || residual_hi < residual_lo) {
        *word = (vdc_priority_lock_word_t){0};
        return;
    }
    if (word->last_match_ns != 0u &&
        (now_ns < word->last_match_ns ||
         now_ns - word->last_match_ns > VDC_PRIORITY_LOCK_STALE_NS)) {
        *word = (vdc_priority_lock_word_t){0};
    }
    /* Unsigned difference is exact for hi >= lo; lo + width/2 cannot pass hi. */
    const uint64_t width = (uint64_t)residual_hi - (uint64_t)residual_lo;
    const int64_t center = residual_lo + (int64_t)(width / 2u);
    const uint64_t magnitude = center < 0 ? (uint64_t)0 - (uint64_t)center
                                          : (uint64_t)center;
    const bool width_ok = width <= VDC_PRIORITY_LOCK_WIDTH_LIMIT_NS;
    if (width_ok && magnitude <= VDC_PRIORITY_LOCK_ENTER_NS) {
        word->out_band_run = 0u;
        if (word->in_band_run != UINT32_MAX) word->in_band_run++;
        if (word->in_band_run >= VDC_PRIORITY_LOCK_ENTER_COUNT) word->locked = 1u;
    } else if (!width_ok || magnitude > VDC_PRIORITY_LOCK_EXIT_NS) {
        word->in_band_run = 0u;
        if (word->out_band_run != UINT32_MAX) word->out_band_run++;
        if (word->out_band_run >= VDC_PRIORITY_LOCK_EXIT_COUNT) word->locked = 0u;
    } else {
        /* Between the enter and exit bands: hold the state, break both runs. */
        word->in_band_run = 0u;
        word->out_band_run = 0u;
    }
    word->center_ns = center > INT32_MAX ? INT32_MAX
                    : center < INT32_MIN ? INT32_MIN : (int32_t)center;
    word->width_ns = width > UINT32_MAX ? UINT32_MAX : (uint32_t)width;
    word->event_sequence = event_sequence;
    word->last_match_ns = now_ns;
}

/* The DPLL is dynamic: a lock whose last match is older than the stale window
 * (or whose age cannot be read) is not live. */
static inline bool vdc_priority_lock_is_live(const vdc_priority_lock_word_t *word,
                                             uint64_t now_ns)
{
    return word->locked != 0u && word->last_match_ns != 0u &&
           now_ns != UINT64_MAX && now_ns >= word->last_match_ns &&
           now_ns - word->last_match_ns <= VDC_PRIORITY_LOCK_STALE_NS;
}

/* One bounded read of the Core1-published word; false preserves *out. The
 * owner's lock declaration reads the Core1 working word directly; this copy is
 * for Core0 readers, which explain the declaration (reason/STALE) with their own
 * clock. Core1 ages `locked` out itself before declaring. */
bool vdc_dpll_manager_get_priority_lock(vdc_priority_lock_word_t *out);

#endif
