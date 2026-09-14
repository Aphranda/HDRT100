#include "tdma_origin_handoff.h"

static void invalid(tdma_origin_handoff_snapshot_t *s)
{
    if (s->invalid_count != UINT32_MAX) ++s->invalid_count;
}

static uint32_t elapsed(tdma_origin_handoff_snapshot_t *s, uint64_t start, uint64_t end)
{
    if (end < start || end - start > UINT32_MAX) {
        invalid(s);
        return UINT32_MAX;
    }
    return (uint32_t)(end - start);
}

void tdma_origin_handoff_begin(tdma_origin_handoff_t *record, uint32_t trial_epoch,
    uint32_t config_seq, uint32_t clock_hz, uint64_t begin_ticks)
{
    if (record == NULL) return;
    (void)__atomic_add_fetch(&record->guard, 1u, __ATOMIC_ACQ_REL);
    record->snapshot = (tdma_origin_handoff_snapshot_t){
        .schema = trial_epoch != 0u && clock_hz != 0u ? TDMA_ORIGIN_HANDOFF_SCHEMA : 0u,
        .trial_epoch = trial_epoch, .config_seq = config_seq, .clock_hz = clock_hz,
        .result = TDMA_ORIGIN_BUILD_BUSY, .begin_ticks = begin_ticks};
    (void)__atomic_add_fetch(&record->guard, 1u, __ATOMIC_RELEASE);
}

void tdma_origin_handoff_record(tdma_origin_handoff_t *record, uint32_t stage,
    uint64_t started, uint64_t ended, tdma_origin_build_result_t result)
{
    if (record == NULL || record->snapshot.schema != TDMA_ORIGIN_HANDOFF_SCHEMA ||
        record->snapshot.result != TDMA_ORIGIN_BUILD_BUSY) return;
    (void)__atomic_add_fetch(&record->guard, 1u, __ATOMIC_ACQ_REL);
    tdma_origin_handoff_snapshot_t *s = &record->snapshot;
    const uint32_t previous_end = s->elapsed_ticks;
    s->elapsed_ticks = elapsed(s, s->begin_ticks, ended);
    if (stage >= TDMA_ORIGIN_HANDOFF_STAGES || (uint32_t)result > TDMA_ORIGIN_BUILD_DONE) {
        invalid(s);
    } else {
        const uint32_t offset = elapsed(s, s->begin_ticks, started);
        const uint32_t work = elapsed(s, started, ended);
        if (offset < previous_end) invalid(s);
        if (s->calls[stage] == 0u) s->first_ticks[stage] = offset;
        if (s->calls[stage] == UINT16_MAX) invalid(s);
        else ++s->calls[stage];
        if (work > UINT32_MAX - s->work_ticks[stage]) {
            s->work_ticks[stage] = UINT32_MAX;
            invalid(s);
        } else s->work_ticks[stage] += work;
        s->result = result;
    }
    (void)__atomic_add_fetch(&record->guard, 1u, __ATOMIC_RELEASE);
}

bool tdma_origin_handoff_get(const tdma_origin_handoff_t *record,
    tdma_origin_handoff_snapshot_t *out)
{
    if (record == NULL || out == NULL) return false;
    const uint32_t before = __atomic_load_n(&record->guard, __ATOMIC_ACQUIRE);
    if ((before & 1u) != 0u) return false;
    *out = record->snapshot;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return before == __atomic_load_n(&record->guard, __ATOMIC_ACQUIRE) &&
        out->schema == TDMA_ORIGIN_HANDOFF_SCHEMA;
}
