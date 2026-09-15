#ifndef VDC_FEEDBACK_MATCH_H
#define VDC_FEEDBACK_MATCH_H

#include <stdbool.h>
#include <stdint.h>

/* Pure single-owner diagnostic matching. No MMIO, wire parsing, CRC, dynamic
 * memory, controller or DCO application. All calls require one serialized
 * owner; cross-core snapshot publication belongs to the caller. */
#define VDC_FEEDBACK_MATCH_CAPACITY 128u
#define VDC_FEEDBACK_MATCH_MAX_TICK_HZ 500000000u
#define VDC_FEEDBACK_MATCH_MAX_INTERVAL_SECONDS 2u
#define VDC_FEEDBACK_MODEL_MAX_INTERVAL_NS UINT64_C(2000000000)
#define VDC_FEEDBACK_MODEL_DOMAIN 2u

/* Raw mode: TIMER1 tick bounds, CRC identity and publication version.
 * Model mode: projected output-ns bounds, reference model token as identity,
 * and Core0 preparation age. Neither mode proves physical GPIO adoption. */
typedef struct {
    uint64_t tx_lo;
    uint64_t tx_hi;
    uint32_t measurement_sequence;
    uint32_t identity_crc32;
    union {
        uint32_t published_version;
        uint32_t prepared_ms;
    };
    uint32_t generation;
} vdc_feedback_match_reference_t;

typedef struct {
    vdc_feedback_match_reference_t entries[VDC_FEEDBACK_MATCH_CAPACITY];
    uint32_t reference_epoch;
    uint32_t tick_hz;
    uint32_t generation;
    uint32_t latest_sequence;
    uint32_t latest_published_version;
    uint32_t has_latest;
} vdc_feedback_match_cache_t;

/* SOURCE-local lifetime; never compare these identifiers with NO1's local
 * clock epoch/run. Zero clock epoch/run and measurement sequence are legal.
 * A nonzero ARM and observer epoch must identify an actual acquisition. */
typedef struct {
    uint64_t source_arm_epoch;
    uint32_t source_clock_epoch_id;
    uint32_t source_clock_run_id;
    uint32_t observer_epoch;
    uint32_t tick_hz;
} vdc_feedback_match_lifetime_t;

/* Already decoded/admitted by Core0. Caller selects a separate peer object
 * by the verified source slot. The frequency result describes raw clocks,
 * not a DCO residual or a replay-resistant cross-board session. */
typedef struct {
    uint64_t source_arm_epoch;
    uint64_t rx_elapsed_cycles;
    uint32_t source_clock_epoch_id;
    uint32_t source_clock_run_id;
    uint32_t observer_epoch;
    uint32_t measurement_sequence;
    uint32_t tick_hz;
} vdc_feedback_match_sample_t;

typedef struct {
    uint64_t rx_elapsed_cycles;
    uint64_t reference_tx_lo;
    uint64_t reference_tx_hi;
    uint32_t measurement_sequence;
    uint32_t reference_identity_crc32;
    uint32_t rx_width_ns; /* Model mode only; raw mode is zero. */
    uint32_t source_model_token; /* Model mode only; raw mode is zero. */
} vdc_feedback_match_pair_t;

/* Last successful difference, retained across misses and baseline changes.
 * It is historical: compare its lifetime/generation to the active binding
 * before treating it as current. pairs[0] is older and pairs[1] is newer.
 * Bounds use outward integer rounding in ppb, with no int32 clipping. They
 * propagate only the supplied raw reference brackets and exact RX count
 * difference. GPIO/SM detection uncertainty is NOT included: these are not
 * physical-frequency confidence bounds, output residuals or lock evidence.
 * reserved == VDC_FEEDBACK_MODEL_DOMAIN explicitly changes pair coordinates
 * to model-projected ns: RX is the lower bound and rx_width_ns adds the upper
 * bound. raw_ppb_* then bounds the committed-model interval ratio, not raw
 * clocks or independently verified physical-output residuals. */
typedef struct {
    vdc_feedback_match_pair_t pairs[2];
    vdc_feedback_match_lifetime_t source;
    int64_t raw_ppb_lo;
    int64_t raw_ppb_hi;
    uint32_t reference_epoch;
    uint32_t reference_generation;
    uint32_t has_pair;
    uint32_t reserved;
} vdc_feedback_match_snapshot_t;

typedef struct {
    vdc_feedback_match_pair_t previous;
    vdc_feedback_match_lifetime_t source;
    uint32_t reference_epoch;
    uint32_t reference_generation;
    uint32_t has_baseline;
    uint32_t reserved;
    vdc_feedback_match_snapshot_t snapshot;
} vdc_feedback_match_peer_t;

typedef enum {
    VDC_FEEDBACK_MATCH_INVALID = 0,
    VDC_FEEDBACK_MATCH_NO_REFERENCE,
    VDC_FEEDBACK_MATCH_BASELINED,
    VDC_FEEDBACK_MATCH_DUPLICATE,
    VDC_FEEDBACK_MATCH_STALE,
    VDC_FEEDBACK_MATCH_INTERVAL_REBASED,
    VDC_FEEDBACK_MATCH_MATCHED,
} vdc_feedback_match_result_t;

/* Cold initialization only. Reinitializing a cache also requires initializing
 * every peer using it: otherwise the generation namespace would be reused. */
void vdc_feedback_match_cache_init(vdc_feedback_match_cache_t *cache);
void vdc_feedback_match_peer_init(vdc_feedback_match_peer_t *peer);

/* Identical active epoch/rate is idempotent. A changed binding invalidates
 * entries lazily using one new generation, without clearing the 4 KiB array.
 * No generation wrap: exhaustion requires cold initialization. Invalid
 * arguments preserve the old binding. Caller retires on STOP/config/role
 * change even if the next origin epoch/rate happens to be identical. */
bool vdc_feedback_match_cache_bind(vdc_feedback_match_cache_t *cache,
    uint32_t reference_epoch, uint32_t tick_hz);
void vdc_feedback_match_cache_retire(vdc_feedback_match_cache_t *cache);

/* New sequence AND even/nonzero publication version must increase numerically
 * within one binding; wrap is rejected. Same latest sequence with identical
 * fields/version is idempotent; conflict/stale input preserves the cache.
 * Cache is sparse: full sequence plus generation must match after indexing. */
bool vdc_feedback_match_cache_put(vdc_feedback_match_cache_t *cache,
    uint32_t measurement_sequence, uint32_t identity_crc32,
    uint32_t published_version, uint64_t tx_lo, uint64_t tx_hi);
/* Model alias shares the same array: identity is a nonzero model token,
 * bounds are projected ns and the entry holds prepared_ms. Publication
 * version still orders insertions in latest_published_version. An identical
 * latest record preserves its original preparation time. The caller MUST
 * retire the cache when switching raw/model mode or admitted session. */
bool vdc_feedback_model_cache_put(vdc_feedback_match_cache_t *cache,
    uint32_t measurement_sequence, uint32_t source_model_token,
    uint32_t published_version, uint64_t output_ns_lo, uint64_t output_ns_hi,
    uint32_t prepared_ms);
bool vdc_feedback_match_cache_lookup(const vdc_feedback_match_cache_t *cache,
    uint32_t measurement_sequence, vdc_feedback_match_reference_t *reference);

/* Caller checks the returned reference's current age in NO1 TIMER1 units
 * BEFORE update; this pure module has no clock or fixed freshness policy.
 * It reuses the exact cached reference and requires equal admitted tick_hz.
 * A miss, duplicate, stale or invalid input leaves the peer unchanged.
 * A valid matched new source/reference lifetime establishes a baseline.
 * Intervals > MAX_INTERVAL_SECONDS rebase, preserving the prior snapshot.
 * Nonpositive source/reference deltas or overlapping reference intervals are
 * rejected without erasing the trusted baseline. The previous pair can be
 * older than the cache because it is held privately by this peer. */
vdc_feedback_match_result_t vdc_feedback_match_update(
    const vdc_feedback_match_cache_t *cache, vdc_feedback_match_peer_t *peer,
    const vdc_feedback_match_sample_t *sample);

/* Same ownership/lifetime rules, with sample.rx_elapsed_cycles interpreted
 * as source output_ns_lo and source_width_ns forming its upper bound. The
 * caller first checks reference.prepared_ms age and admitted model/session.
 * Endpoints may use different nondecreasing nonzero model tokens. Both full
 * source/reference interval differences must be positive; >2e9 ns rebases.
 * Snapshot reserved identifies the model domain; no GPIO eligibility given. */
vdc_feedback_match_result_t vdc_feedback_model_update(
    const vdc_feedback_match_cache_t *cache, vdc_feedback_match_peer_t *peer,
    const vdc_feedback_match_sample_t *sample, uint32_t source_width_ns,
    uint32_t source_model_token);

/* False preserves output. A true result returns the historical last complete
 * pair, not an assertion that a new result was produced by the latest call. */
bool vdc_feedback_match_peer_snapshot(const vdc_feedback_match_peer_t *peer,
    vdc_feedback_match_snapshot_t *snapshot);

#endif
