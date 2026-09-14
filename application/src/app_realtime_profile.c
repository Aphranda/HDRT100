#include "app_realtime_profile.h"
#include <stddef.h>

#define PROFILE_PHASE(name, start, end, wcet) \
    [APP_REALTIME_PHASE_##name] = {start, end, wcet},
static const app_realtime_phase_contract_t base[APP_REALTIME_PHASE_COUNT] = {
    APP_REALTIME_PHASE_TABLE(PROFILE_PHASE)
};
#undef PROFILE_PHASE

bool app_realtime_profile_supported(uint32_t cycle_cycles)
{
    return cycle_cycles == PROJECT_CORE1_PROFILE_1500US_CYCLES ||
        cycle_cycles == PROJECT_CORE1_PROFILE_5MS_CYCLES ||
        cycle_cycles == PROJECT_CORE1_PROFILE_10MS_CYCLES ||
        cycle_cycles == PROJECT_CORE1_PROFILE_15MS_CYCLES;
}

bool app_realtime_profile_phase(uint32_t cycle_cycles,
    app_realtime_phase_id_t phase_id, app_realtime_phase_contract_t *phase)
{
    if (phase == NULL || (uint32_t)phase_id >= APP_REALTIME_PHASE_COUNT ||
        !app_realtime_profile_supported(cycle_cycles)) return false;
    const uint32_t extra = cycle_cycles - PROJECT_CORE1_PROFILE_1500US_CYCLES;
    *phase = base[phase_id];
    if (phase_id == APP_REALTIME_PHASE_TDMA) {
        phase->wcet_cycles += extra;
    } else {
        phase->start_cycle += extra;
    }
    phase->end_cycle += extra;
    return true;
}

bool app_realtime_profile_install(app_realtime_schedule_snapshot_t *schedule,
    uint32_t cycle_cycles, uint32_t generation)
{
    if (schedule == NULL || !app_realtime_profile_supported(cycle_cycles)) return false;
    for (uint32_t i = 0u; i < APP_REALTIME_PHASE_COUNT; ++i) {
        app_realtime_phase_contract_t phase;
        (void)app_realtime_profile_phase(cycle_cycles, (app_realtime_phase_id_t)i, &phase);
        schedule->phase_start_cycle[i] = phase.start_cycle;
        schedule->phase_end_cycle[i] = phase.end_cycle;
        schedule->phase_wcet_cycles[i] = phase.wcet_cycles;
    }
    schedule->cycle_cycles = cycle_cycles;
    schedule->profile_generation = generation;
    return true;
}
