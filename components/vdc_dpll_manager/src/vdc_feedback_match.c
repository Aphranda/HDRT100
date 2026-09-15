#include "vdc_feedback_match.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert((VDC_FEEDBACK_MATCH_CAPACITY &
               (VDC_FEEDBACK_MATCH_CAPACITY - 1u)) == 0u,
               "reference index requires a power-of-two capacity");
_Static_assert(sizeof(vdc_feedback_match_reference_t) == 32u,
               "reference cache entry budget");
_Static_assert(sizeof(vdc_feedback_match_cache_t) == 4120u,
               "one compact reference cache, including its binding");
_Static_assert(sizeof(vdc_feedback_match_snapshot_t) == 120u,
               "two independently recomputable diagnostic pairs");
_Static_assert(sizeof(vdc_feedback_match_peer_t) == 192u,
               "per-source baseline and historical witness budget");
_Static_assert((uint64_t)VDC_FEEDBACK_MATCH_MAX_TICK_HZ *
               VDC_FEEDBACK_MATCH_MAX_INTERVAL_SECONDS <= UINT64_C(1000000000),
               "bounded tick interval keeps scaled numerator below 1e18");

static bool valid_hz(uint32_t tick_hz)
{
    return tick_hz != 0u && tick_hz <= VDC_FEEDBACK_MATCH_MAX_TICK_HZ;
}

static bool cache_active(const vdc_feedback_match_cache_t *cache)
{
    return cache != NULL && cache->reference_epoch != 0u &&
           cache->generation != 0u && valid_hz(cache->tick_hz);
}

void vdc_feedback_match_cache_init(vdc_feedback_match_cache_t *cache)
{
    if (cache != NULL) memset(cache, 0, sizeof(*cache));
}

void vdc_feedback_match_peer_init(vdc_feedback_match_peer_t *peer)
{
    if (peer != NULL) memset(peer, 0, sizeof(*peer));
}

bool vdc_feedback_match_cache_bind(vdc_feedback_match_cache_t *cache,
    uint32_t reference_epoch, uint32_t tick_hz)
{
    if (cache == NULL || reference_epoch == 0u || !valid_hz(tick_hz)) return false;
    if (cache_active(cache) && cache->reference_epoch == reference_epoch &&
        cache->tick_hz == tick_hz) return true;
    if (cache->generation == UINT32_MAX) {
        cache->reference_epoch = 0u;
        cache->tick_hz = 0u;
        cache->has_latest = 0u;
        return false;
    }
    ++cache->generation;
    cache->reference_epoch = reference_epoch;
    cache->tick_hz = tick_hz;
    cache->latest_sequence = 0u;
    cache->latest_published_version = 0u;
    cache->has_latest = 0u;
    return true;
}

void vdc_feedback_match_cache_retire(vdc_feedback_match_cache_t *cache)
{
    if (cache == NULL || cache->reference_epoch == 0u) return;
    cache->reference_epoch = 0u;
    cache->tick_hz = 0u;
    cache->has_latest = 0u;
    if (cache->generation != UINT32_MAX) ++cache->generation;
}

bool vdc_feedback_match_cache_put(vdc_feedback_match_cache_t *cache,
    uint32_t measurement_sequence, uint32_t identity_crc32,
    uint32_t published_version, uint64_t tx_lo, uint64_t tx_hi)
{
    if (!cache_active(cache) || published_version == 0u ||
        (published_version & 1u) != 0u || tx_lo > tx_hi) return false;
    vdc_feedback_match_reference_t *entry =
        &cache->entries[measurement_sequence & (VDC_FEEDBACK_MATCH_CAPACITY - 1u)];
    if (cache->has_latest != 0u) {
        if (measurement_sequence == cache->latest_sequence) {
            return published_version == cache->latest_published_version &&
                entry->generation == cache->generation &&
                entry->measurement_sequence == measurement_sequence &&
                entry->published_version == published_version &&
                entry->identity_crc32 == identity_crc32 &&
                entry->tx_lo == tx_lo && entry->tx_hi == tx_hi;
        }
        if (measurement_sequence < cache->latest_sequence ||
            published_version <= cache->latest_published_version) return false;
    }
    *entry = (vdc_feedback_match_reference_t){
        .tx_lo = tx_lo, .tx_hi = tx_hi,
        .measurement_sequence = measurement_sequence,
        .identity_crc32 = identity_crc32,
        .published_version = published_version,
        .generation = cache->generation,
    };
    cache->latest_sequence = measurement_sequence;
    cache->latest_published_version = published_version;
    cache->has_latest = 1u;
    return true;
}

bool vdc_feedback_match_cache_lookup(const vdc_feedback_match_cache_t *cache,
    uint32_t measurement_sequence, vdc_feedback_match_reference_t *reference)
{
    if (!cache_active(cache) || reference == NULL || cache->has_latest == 0u) return false;
    const vdc_feedback_match_reference_t *entry =
        &cache->entries[measurement_sequence & (VDC_FEEDBACK_MATCH_CAPACITY - 1u)];
    if (entry->generation != cache->generation ||
        entry->measurement_sequence != measurement_sequence) return false;
    *reference = *entry;
    return true;
}

static bool same_source(const vdc_feedback_match_lifetime_t *a,
    const vdc_feedback_match_lifetime_t *b)
{
    return a->source_arm_epoch == b->source_arm_epoch &&
        a->source_clock_epoch_id == b->source_clock_epoch_id &&
        a->source_clock_run_id == b->source_clock_run_id &&
        a->observer_epoch == b->observer_epoch && a->tick_hz == b->tick_hz;
}

static void baseline(vdc_feedback_match_peer_t *peer,
    const vdc_feedback_match_cache_t *cache,
    const vdc_feedback_match_lifetime_t *source,
    const vdc_feedback_match_pair_t *pair)
{
    peer->previous = *pair;
    peer->source = *source;
    peer->reference_epoch = cache->reference_epoch;
    peer->reference_generation = cache->generation;
    peer->has_baseline = 1u;
}

vdc_feedback_match_result_t vdc_feedback_match_update(
    const vdc_feedback_match_cache_t *cache, vdc_feedback_match_peer_t *peer,
    const vdc_feedback_match_sample_t *sample)
{
    if (peer == NULL || sample == NULL || sample->source_arm_epoch == 0u ||
        sample->observer_epoch == 0u || !valid_hz(sample->tick_hz))
        return VDC_FEEDBACK_MATCH_INVALID;
    if (!cache_active(cache)) return VDC_FEEDBACK_MATCH_NO_REFERENCE;
    if (sample->tick_hz != cache->tick_hz) return VDC_FEEDBACK_MATCH_INVALID;
    vdc_feedback_match_reference_t reference;
    if (!vdc_feedback_match_cache_lookup(cache, sample->measurement_sequence, &reference))
        return VDC_FEEDBACK_MATCH_NO_REFERENCE;
    const vdc_feedback_match_lifetime_t source = {
        .source_arm_epoch = sample->source_arm_epoch,
        .source_clock_epoch_id = sample->source_clock_epoch_id,
        .source_clock_run_id = sample->source_clock_run_id,
        .observer_epoch = sample->observer_epoch, .tick_hz = sample->tick_hz,
    };
    const vdc_feedback_match_pair_t pair = {
        .rx_elapsed_cycles = sample->rx_elapsed_cycles,
        .reference_tx_lo = reference.tx_lo, .reference_tx_hi = reference.tx_hi,
        .measurement_sequence = sample->measurement_sequence,
        .reference_identity_crc32 = reference.identity_crc32,
    };
    if (peer->has_baseline == 0u ||
        peer->reference_epoch != cache->reference_epoch ||
        peer->reference_generation != cache->generation || !same_source(&peer->source, &source)) {
        baseline(peer, cache, &source, &pair);
        return VDC_FEEDBACK_MATCH_BASELINED;
    }
    const vdc_feedback_match_pair_t *previous = &peer->previous;
    if (pair.measurement_sequence < previous->measurement_sequence)
        return VDC_FEEDBACK_MATCH_STALE;
    if (pair.measurement_sequence == previous->measurement_sequence) {
        return pair.rx_elapsed_cycles == previous->rx_elapsed_cycles &&
            pair.reference_tx_lo == previous->reference_tx_lo &&
            pair.reference_tx_hi == previous->reference_tx_hi &&
            pair.reference_identity_crc32 == previous->reference_identity_crc32
                ? VDC_FEEDBACK_MATCH_DUPLICATE : VDC_FEEDBACK_MATCH_INVALID;
    }
    if (pair.rx_elapsed_cycles <= previous->rx_elapsed_cycles ||
        pair.reference_tx_lo <= previous->reference_tx_hi)
        return VDC_FEEDBACK_MATCH_INVALID;
    const uint64_t source_delta = pair.rx_elapsed_cycles - previous->rx_elapsed_cycles;
    const uint64_t reference_delta_lo = pair.reference_tx_lo - previous->reference_tx_hi;
    const uint64_t reference_delta_hi = pair.reference_tx_hi - previous->reference_tx_lo;
    const uint64_t max_delta = (uint64_t)sample->tick_hz *
        VDC_FEEDBACK_MATCH_MAX_INTERVAL_SECONDS;
    if (source_delta > max_delta || reference_delta_hi > max_delta) {
        baseline(peer, cache, &source, &pair);
        return VDC_FEEDBACK_MATCH_INTERVAL_REBASED;
    }
    /* Both deltas are positive, and source_delta <= 1e9. The product is at
     * most 1e18, below INT64_MAX as well as UINT64_MAX. Outward rounding is
     * performed on the positive ratio before subtracting exactly 1e9. */
    const uint64_t numerator = source_delta * UINT64_C(1000000000);
    const uint64_t lower = numerator / reference_delta_hi;
    const uint64_t upper = numerator / reference_delta_lo +
        (numerator % reference_delta_lo != 0u ? 1u : 0u);
    const vdc_feedback_match_snapshot_t snapshot = {
        .pairs = {*previous, pair}, .source = source,
        .raw_ppb_lo = (int64_t)lower - INT64_C(1000000000),
        .raw_ppb_hi = (int64_t)upper - INT64_C(1000000000),
        .reference_epoch = cache->reference_epoch,
        .reference_generation = cache->generation, .has_pair = 1u,
    };
    peer->snapshot = snapshot;
    baseline(peer, cache, &source, &pair);
    return VDC_FEEDBACK_MATCH_MATCHED;
}

bool vdc_feedback_match_peer_snapshot(const vdc_feedback_match_peer_t *peer,
    vdc_feedback_match_snapshot_t *snapshot)
{
    if (peer == NULL || snapshot == NULL || peer->snapshot.has_pair == 0u) return false;
    *snapshot = peer->snapshot;
    return true;
}
