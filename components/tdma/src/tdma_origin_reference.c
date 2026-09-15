#include "tdma_origin_plan.h"

#include <limits.h>

bool tdma_origin_raw_reference(const tdma_origin_live_snapshot_t *live,
    uint32_t latch_sm, uint32_t csn_pin, tdma_origin_raw_reference_t *out)
{
    if (live == NULL || out == NULL || latch_sm >= 4u || csn_pin >= 32u ||
        live->retained != 1u || live->active != 1u) return false;
    const tdma_origin_record_frozen_t *sample = &live->sample;
    const tdma_origin_record_t *record = &sample->record;
    const tdma_origin_raw_time_t *raw = &record->raw_time;
    /* RP2350 PIO FSTAT RXEMPTY occupies bits 8..11. This layer owns the
     * hardware interpretation; VDC receives only an immutable scalar copy. */
    if (!sample->epoch || sample->fault || !sample->published_version ||
        (sample->published_version & 1u) || record->epoch != sample->epoch ||
        record->format != TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME ||
        !(record->flags & TDMA_ORIGIN_RECORD_TRANSPORT_CHECKED) ||
        record->observation.sequence != record->sequence_end ||
        !record->observation.local_generation || record->observation.output_remaining ||
        !raw->tick_hz || (raw->latch_fstat & (1u << (8u + latch_sm))) ||
        !(raw->arm_padout & (1u << csn_pin)) ||
        raw->arm_before[0] != raw->arm_before[2] ||
        raw->arm_after[0] != raw->arm_after[2]) return false;
    const uint64_t before = ((uint64_t)raw->arm_before[0] << 32u) | raw->arm_before[1];
    const uint64_t after = ((uint64_t)raw->arm_after[0] << 32u) | raw->arm_after[1];
    /* X is seeded to UINT32_MAX before enable. The high path consists of
     * JMP PIN and JMP X--: two clk_sys cycles per decrement. Preserve both
     * enable endpoints; do not promote a midpoint to an edge timestamp. */
    const uint64_t elapsed = 2ull * (UINT32_MAX - raw->latch_remaining);
    if (after < before || UINT64_MAX - after < elapsed) return false;
    *out = (tdma_origin_raw_reference_t){
        .timer_lower = before + elapsed, .timer_upper = after + elapsed,
        .epoch = sample->epoch, .sequence = record->observation.sequence,
        .identity = record->observation.identity,
        .published_version = sample->published_version, .tick_hz = raw->tick_hz};
    return true;
}
