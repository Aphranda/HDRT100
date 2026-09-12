#include "tdma_service_timing.h"

#if TDMA_SERVICE_TIMING_ENABLED
#include <stddef.h>
#include <string.h>
#include "vdc_timestamp_clock.h"

static tdma_service_timing_record_t s_work;
static tdma_service_timing_snapshot_t s_snapshot;
static volatile uint32_t s_guard;
static volatile uint32_t s_reset_request;
static bool s_active;

uint64_t tdma_service_timing_now(void)
{
    return vdc_timestamp_clock_read_ticks64();
}

uint32_t tdma_service_timing_request_reset(void)
{
    return __atomic_add_fetch(&s_reset_request, 1u, __ATOMIC_RELEASE);
}

void tdma_service_timing_phase_begin(void)
{
    const uint32_t requested = __atomic_load_n(&s_reset_request, __ATOMIC_ACQUIRE);
    /* Only the phase owner writes the snapshot. No shared guard is held
     * across the phase, so Core0 may read the previous complete record. */
    if (requested != s_snapshot.reset_generation || s_snapshot.version == 0u) {
        (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_ACQ_REL);
        memset(&s_snapshot, 0, sizeof(s_snapshot));
        s_snapshot.version = TDMA_SERVICE_TIMING_VERSION;
        s_snapshot.clock_hz = vdc_timestamp_clock_tick_hz();
        s_snapshot.reset_generation = requested;
        (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_RELEASE);
    }
    memset(&s_work, 0, sizeof(s_work));
    s_work.sequence = s_snapshot.phase_count + 1u;
    s_work.start_ticks = tdma_service_timing_now();
    s_active = true;
}

static uint32_t tdma_service_timing_elapsed(uint64_t start, uint64_t end)
{
    if (end < start || end - start > UINT32_MAX) {
        s_work.invalid_count++;
        return UINT32_MAX;
    }
    return (uint32_t)(end - start);
}

void tdma_service_timing_record(tdma_service_timing_stage_t stage, uint64_t start)
{
    if (!s_active) return;
    if ((uint32_t)stage >= TDMA_TIMING_STAGE_COUNT || start < s_work.start_ticks) {
        s_work.invalid_count++;
        return;
    }
    const uint32_t ticks = tdma_service_timing_elapsed(start, tdma_service_timing_now());
    if (ticks > UINT32_MAX - s_work.elapsed_ticks[stage]) {
        s_work.invalid_count++;
        s_work.elapsed_ticks[stage] = UINT32_MAX;
    } else {
        s_work.elapsed_ticks[stage] += ticks;
    }
    s_work.calls[stage]++;
}

void tdma_service_timing_phase_end(void)
{
    if (!s_active) return;
    s_work.total_ticks = tdma_service_timing_elapsed(s_work.start_ticks,
                                                   tdma_service_timing_now());
    if (s_snapshot.clock_hz == 0u) s_work.invalid_count++;
    s_active = false;
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_ACQ_REL);
    s_snapshot.last = s_work;
    s_snapshot.phase_count = s_work.sequence;
    if (s_work.invalid_count == 0u &&
        (s_snapshot.peak.sequence == 0u || s_work.total_ticks > s_snapshot.peak.total_ticks)) {
        s_snapshot.peak = s_work;
    }
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_RELEASE);
}

bool tdma_service_timing_try_snapshot(tdma_service_timing_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    const uint32_t before = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
    if ((before & 1u) != 0u) return false;
    *snapshot = s_snapshot;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint32_t after = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
    return before == after && snapshot->version == TDMA_SERVICE_TIMING_VERSION;
}
#endif
