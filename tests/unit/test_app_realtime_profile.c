#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "app_realtime_profile.h"

int main(void)
{
    const uint32_t periods[] = {375000u,1250000u,2500000u,3750000u};
    const uint32_t microseconds[] = {1500u,5000u,10000u,15000u};
    for (size_t p=0; p<sizeof(periods)/sizeof(periods[0]); ++p) {
        assert(app_realtime_profile_supported(periods[p]));
        uint32_t end=0u;
        for (uint32_t i=0; i<APP_REALTIME_PHASE_COUNT; ++i) {
            app_realtime_phase_contract_t phase, shortest;
            assert(app_realtime_profile_phase(periods[p],i,&phase));
            assert(app_realtime_profile_phase(periods[0],i,&shortest));
            assert(phase.start_cycle==end && phase.end_cycle>phase.start_cycle);
            assert(phase.wcet_cycles<=phase.end_cycle-phase.start_cycle);
            if (i==APP_REALTIME_PHASE_TDMA) {
                assert(phase.start_cycle==0u && phase.wcet_cycles>=61400u+30000u);
                assert(phase.end_cycle-phase.wcet_cycles==5000u);
            } else {
                assert(phase.wcet_cycles==shortest.wcet_cycles);
                assert(phase.end_cycle-phase.start_cycle==shortest.end_cycle-shortest.start_cycle);
            }
            if (i==APP_REALTIME_PHASE_DPLL) {
                /* All catalog entries must allow late entry while retaining
                 * the existing servo WCET. A zero-headroom table starved the
                 * real dispatcher unless it hit exactly the start cycle. */
                assert(phase.wcet_cycles==34000u);
                assert(phase.end_cycle-phase.start_cycle-phase.wcet_cycles>=1000u);
            }
            if (i==APP_REALTIME_PHASE_GUARD) {
                assert(phase.wcet_cycles==0u && phase.end_cycle-phase.start_cycle==7500u);
            }
            end=phase.end_cycle;
        }
        assert(end==periods[p] && end<=0xffffffu);
        assert((uint64_t)periods[p]*1000000u==(uint64_t)250000000u*microseconds[p]);
    }
    app_realtime_phase_contract_t untouched={1u,2u,3u};
    assert(!app_realtime_profile_phase(250000u,APP_REALTIME_PHASE_TDMA,&untouched));
    assert(untouched.start_cycle==1u && untouched.end_cycle==2u && untouched.wcet_cycles==3u);
    assert(!app_realtime_profile_phase(375000u,(app_realtime_phase_id_t)-1,&untouched));
    assert(!app_realtime_profile_phase(375000u,APP_REALTIME_PHASE_COUNT,&untouched));
    assert(!app_realtime_profile_phase(UINT32_MAX,APP_REALTIME_PHASE_TDMA,&untouched));
    assert(!app_realtime_profile_phase(375000u,APP_REALTIME_PHASE_TDMA,NULL));
    app_realtime_schedule_snapshot_t schedule;
    memset(&schedule, 0xa5, sizeof(schedule));
    app_realtime_schedule_snapshot_t before = schedule;
    assert(!app_realtime_profile_install(NULL, 375000u, 1u));
    assert(!app_realtime_profile_install(&schedule, 250000u, 1u));
    assert(memcmp(&schedule, &before, sizeof(schedule)) == 0);
    /* Every pair of profiles is a complete replacement, including rollback
     * to the short table. Cumulative failures and maxima remain available. */
    for (size_t old = 0u; old < 4u; ++old) {
        for (size_t next = 0u; next < 4u; ++next) {
            assert(app_realtime_profile_install(&schedule, periods[old], UINT32_MAX));
            assert(app_realtime_profile_install(&schedule, periods[next], 0u));
            assert(schedule.cycle_cycles == periods[next] && schedule.profile_generation == 0u);
            for (uint32_t i = 0u; i < APP_REALTIME_PHASE_COUNT; ++i) {
                app_realtime_phase_contract_t phase;
                assert(app_realtime_profile_phase(periods[next], i, &phase));
                assert(schedule.phase_start_cycle[i] == phase.start_cycle);
                assert(schedule.phase_end_cycle[i] == phase.end_cycle);
                assert(schedule.phase_wcet_cycles[i] == phase.wcet_cycles);
                assert(schedule.phase_max_runtime_cycles[i] == before.phase_max_runtime_cycles[i]);
                assert(schedule.phase_overrun_count[i] == before.phase_overrun_count[i]);
                assert(schedule.phase_deadline_miss_count[i] == before.phase_deadline_miss_count[i]);
            }
            assert(schedule.cycle_count == before.cycle_count);
            assert(schedule.schedule_miss_count == before.schedule_miss_count);
            assert(schedule.quarantined_mask == before.quarantined_mask);
        }
    }
    return 0;
}
