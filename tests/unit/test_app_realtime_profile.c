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
    /* ---- run-time configurable table -------------------------------------- */
    app_realtime_profile_rejection_t reject = APP_REALTIME_PROFILE_REJECTION_INIT;
    app_realtime_profile_table_t def, eff, staged, custom;
    assert(app_realtime_profile_table_default(&def));
    assert(app_realtime_profile_table_effective(&eff));
    assert(memcmp(&def, &eff, sizeof(def)) == 0);
    assert(!app_realtime_profile_override_active());
    assert(app_realtime_profile_table_validate(&def, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_OK);
    assert(reject.phase_id == APP_REALTIME_PHASE_COUNT);
    assert(def.version == APP_REALTIME_PROFILE_TABLE_VERSION);
    assert(def.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES);
    assert(def.phase[APP_REALTIME_PHASE_TDMA].start_cycle ==
           PROJECT_CORE1_PHASE_TDMA_START_CYCLE);
    assert(def.phase[APP_REALTIME_PHASE_TDMA].wcet_cycles ==
           PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES);
    assert(def.phase[APP_REALTIME_PHASE_GUARD].end_cycle ==
           PROJECT_CORE1_CYCLE_CYCLES);

    /* A rejected table is not installed, and the reason names the rule. */
    assert(!app_realtime_profile_table_validate(NULL, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_NULL);
    custom = def;
    custom.version = APP_REALTIME_PROFILE_TABLE_VERSION + 1u;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_VERSION);
    custom = def;
    custom.phase[APP_REALTIME_PHASE_VDC].start_cycle += 1u;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_ORDER &&
           reject.phase_id == APP_REALTIME_PHASE_VDC);
    custom = def;
    custom.phase[APP_REALTIME_PHASE_VDC].end_cycle =
        custom.phase[APP_REALTIME_PHASE_VDC].start_cycle;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_EMPTY_PHASE &&
           reject.phase_id == APP_REALTIME_PHASE_VDC);
    custom = def;
    custom.phase[APP_REALTIME_PHASE_REFMEM].wcet_cycles =
        custom.phase[APP_REALTIME_PHASE_REFMEM].end_cycle -
        custom.phase[APP_REALTIME_PHASE_REFMEM].start_cycle + 1u;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_WCET &&
           reject.phase_id == APP_REALTIME_PHASE_REFMEM);
    custom = def;
    custom.phase[APP_REALTIME_PHASE_GUARD].wcet_cycles = 1u;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_GUARD_WCET &&
           reject.phase_id == APP_REALTIME_PHASE_GUARD);
    custom = def;
    custom.cycle_cycles = 0u;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_CYCLE_RANGE);
    custom = def;
    custom.phase[APP_REALTIME_PHASE_GUARD].end_cycle =
        APP_REALTIME_PROFILE_CYCLE_MAX_CYCLES + 1u;
    custom.cycle_cycles = custom.phase[APP_REALTIME_PHASE_GUARD].end_cycle;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_CYCLE_RANGE);
    /* The cycle is the end of the last window: a table whose windows stop
     * earlier cannot claim a longer cycle. */
    custom = def;
    custom.cycle_cycles = PROJECT_CORE1_CYCLE_CYCLES + 250u;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_CYCLE_TAIL);
    custom = def;
    custom.phase[APP_REALTIME_PHASE_TDMA].wcet_cycles =
        PROJECT_CORE1_PHASE_TDMA_WCET_FLOOR_CYCLES - 1u;
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_TDMA_FLOOR &&
           reject.phase_id == APP_REALTIME_PHASE_TDMA);
    custom = def;
    custom.phase[APP_REALTIME_PHASE_DPLL].wcet_cycles =
        custom.phase[APP_REALTIME_PHASE_DPLL].end_cycle -
        custom.phase[APP_REALTIME_PHASE_DPLL].start_cycle -
        (APP_REALTIME_PROFILE_DPLL_HEADROOM_CYCLES - 1u);
    assert(!app_realtime_profile_override_install(&custom, &reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_DPLL_HEADROOM &&
           reject.phase_id == APP_REALTIME_PHASE_DPLL);
    assert(!app_realtime_profile_override_active());
    assert(app_realtime_profile_table_effective(&eff));
    assert(memcmp(&def, &eff, sizeof(def)) == 0);
    assert(app_realtime_profile_reject_name(APP_REALTIME_PROFILE_REJECT_TDMA_FLOOR)[0] == 'T');
    assert(app_realtime_profile_reject_name((app_realtime_profile_reject_t)99)[0] == 'U');

    /* Editing is staged: nothing becomes effective before APPLY. */
    assert(app_realtime_profile_stage_read(&staged) == false);
    assert(!app_realtime_profile_stage_phase(APP_REALTIME_PHASE_GUARD, 0u, 1u, 0u));
    assert(!app_realtime_profile_stage_cycle(500000u));
    assert(!app_realtime_profile_stage_apply(&reject));
    assert(reject.code == APP_REALTIME_PROFILE_REJECT_NULL);
    assert(app_realtime_profile_stage_begin());
    assert(!app_realtime_profile_stage_dirty());
    assert(app_realtime_profile_stage_read(&staged));
    assert(memcmp(&staged, &def, sizeof(def)) == 0);
    assert(app_realtime_profile_stage_cycle(500000u));
    assert(app_realtime_profile_stage_dirty());
    assert(app_realtime_profile_stage_phase(APP_REALTIME_PHASE_GUARD,
        def.phase[APP_REALTIME_PHASE_GUARD].start_cycle, 500000u, 0u));
    assert(app_realtime_profile_stage_read(&staged));
    assert(staged.cycle_cycles == 500000u);
    assert(staged.phase[APP_REALTIME_PHASE_GUARD].end_cycle == 500000u);
    assert(!app_realtime_profile_override_active());
    assert(app_realtime_profile_supported(PROJECT_CORE1_PROFILE_5MS_CYCLES));
    assert(app_realtime_profile_stage_validate(&reject));
    assert(app_realtime_profile_stage_apply(&reject));
    assert(app_realtime_profile_stage_validate(&reject) == true);
    assert(!app_realtime_profile_stage_dirty());
    assert(app_realtime_profile_override_active());
    assert(app_realtime_profile_table_effective(&eff));
    assert(memcmp(&eff, &staged, sizeof(eff)) == 0);
    /* An override owns the cycle: the catalog is unreachable until it is
     * cleared, and no other period may re-scale the configured windows. */
    assert(app_realtime_profile_supported(500000u));
    assert(!app_realtime_profile_supported(PROJECT_CORE1_PROFILE_1500US_CYCLES));
    assert(!app_realtime_profile_supported(PROJECT_CORE1_PROFILE_5MS_CYCLES));
    app_realtime_phase_contract_t phase;
    assert(app_realtime_profile_phase(500000u, APP_REALTIME_PHASE_GUARD, &phase));
    assert(phase.end_cycle == 500000u && phase.start_cycle ==
           def.phase[APP_REALTIME_PHASE_GUARD].start_cycle);
    assert(app_realtime_profile_phase(500000u, APP_REALTIME_PHASE_TDMA, &phase));
    assert(phase.wcet_cycles == PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES);
    assert(!app_realtime_profile_phase(PROJECT_CORE1_PROFILE_5MS_CYCLES,
                                       APP_REALTIME_PHASE_TDMA, &untouched));
    assert(untouched.start_cycle == 1u && untouched.end_cycle == 2u &&
           untouched.wcet_cycles == 3u);
    /* The configured table is what the dispatcher receives. */
    memset(&schedule, 0xa5, sizeof(schedule));
    assert(app_realtime_profile_install(&schedule, 500000u, 7u));
    assert(schedule.cycle_cycles == 500000u && schedule.profile_generation == 7u);
    for (uint32_t i = 0u; i < APP_REALTIME_PHASE_COUNT; ++i) {
        assert(schedule.phase_start_cycle[i] == eff.phase[i].start_cycle);
        assert(schedule.phase_end_cycle[i] == eff.phase[i].end_cycle);
        assert(schedule.phase_wcet_cycles[i] == eff.phase[i].wcet_cycles);
    }
    assert(!app_realtime_profile_install(&schedule, PROJECT_CORE1_PROFILE_5MS_CYCLES, 8u));
    /* DISCARD drops the staged edit; CLEAR returns to the compiled default. */
    assert(app_realtime_profile_stage_begin());
    assert(app_realtime_profile_stage_cycle(600000u));
    assert(app_realtime_profile_stage_dirty());
    assert(app_realtime_profile_stage_discard());
    assert(!app_realtime_profile_stage_dirty());
    assert(!app_realtime_profile_stage_read(&staged));
    assert(app_realtime_profile_stage_begin());
    assert(app_realtime_profile_stage_read(&staged));
    assert(staged.cycle_cycles == 500000u);
    assert(app_realtime_profile_override_clear());
    assert(!app_realtime_profile_override_active());
    assert(app_realtime_profile_table_effective(&eff));
    assert(memcmp(&eff, &def, sizeof(eff)) == 0);
    assert(app_realtime_profile_supported(PROJECT_CORE1_PROFILE_5MS_CYCLES));
    /* A table identical to the compiled default is the default: it must not
     * stay installed as an override that hides the catalog profiles. */
    assert(app_realtime_profile_override_install(&def, &reject));
    assert(!app_realtime_profile_override_active());
    assert(app_realtime_profile_phase(PROJECT_CORE1_PROFILE_15MS_CYCLES,
                                      APP_REALTIME_PHASE_TDMA, &phase));
    assert(phase.wcet_cycles == PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES +
           (PROJECT_CORE1_PROFILE_15MS_CYCLES - PROJECT_CORE1_CYCLE_CYCLES));

    /* A configured table may keep the catalog window shape while shortening the
     * TDMA window, as long as every rule still holds. */
    custom = def;
    custom.phase[APP_REALTIME_PHASE_TDMA].end_cycle =
        PROJECT_CORE1_PHASE_TDMA_WCET_FLOOR_CYCLES;
    custom.phase[APP_REALTIME_PHASE_TDMA].wcet_cycles =
        PROJECT_CORE1_PHASE_TDMA_WCET_FLOOR_CYCLES;
    for (uint32_t i = (uint32_t)APP_REALTIME_PHASE_VDC;
         i < (uint32_t)APP_REALTIME_PHASE_COUNT; ++i) {
        const uint32_t start = custom.phase[i].start_cycle;
        const uint32_t end = custom.phase[i].end_cycle;
        custom.phase[i].start_cycle = start -
            (def.phase[APP_REALTIME_PHASE_TDMA].end_cycle -
             PROJECT_CORE1_PHASE_TDMA_WCET_FLOOR_CYCLES);
        custom.phase[i].end_cycle = end -
            (def.phase[APP_REALTIME_PHASE_TDMA].end_cycle -
             PROJECT_CORE1_PHASE_TDMA_WCET_FLOOR_CYCLES);
    }
    custom.cycle_cycles = custom.phase[APP_REALTIME_PHASE_GUARD].end_cycle;
    assert(app_realtime_profile_table_validate(&custom, &reject));
    assert(app_realtime_profile_override_install(&custom, &reject));
    assert(app_realtime_profile_override_active());
    assert(app_realtime_profile_supported(custom.cycle_cycles));
    assert(app_realtime_profile_phase(custom.cycle_cycles,
                                      APP_REALTIME_PHASE_DPLL, &phase));
    assert(phase.end_cycle - phase.start_cycle - phase.wcet_cycles >=
           APP_REALTIME_PROFILE_DPLL_HEADROOM_CYCLES);
    assert(app_realtime_profile_override_clear());
    assert(!app_realtime_profile_override_active());
    /* Editing a VDC budget after selecting a catalog period preserves that
     * running period instead of silently returning to the default cycle. */
    assert(app_realtime_profile_install(&schedule,
        PROJECT_CORE1_PROFILE_5MS_CYCLES, 11u));
    schedule.phase_count = APP_REALTIME_PHASE_COUNT;
    assert(app_realtime_profile_stage_from_schedule(&schedule));
    assert(app_realtime_profile_stage_read(&staged));
    assert(staged.cycle_cycles == PROJECT_CORE1_PROFILE_5MS_CYCLES);
    assert(!app_realtime_profile_stage_dirty());
    assert(staged.phase[APP_REALTIME_PHASE_VDC].start_cycle ==
        schedule.phase_start_cycle[APP_REALTIME_PHASE_VDC]);
    schedule.phase_count--;
    assert(!app_realtime_profile_stage_from_schedule(&schedule));
    assert(!app_realtime_profile_stage_from_schedule(NULL));
    return 0;
}
