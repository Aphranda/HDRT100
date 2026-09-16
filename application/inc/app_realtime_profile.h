#ifndef APP_REALTIME_PROFILE_H
#define APP_REALTIME_PROFILE_H

#include <stdbool.h>
#include "app.h"

/* Pure table lookup. This does not authorize a lifecycle change or a frame.
 * The application owner installs a whole table only at a stopped boundary. */
bool app_realtime_profile_supported(uint32_t cycle_cycles);
bool app_realtime_profile_phase(uint32_t cycle_cycles,
    app_realtime_phase_id_t phase_id, app_realtime_phase_contract_t *phase);
typedef struct {
    uint32_t background_cycles, irq_cycles, irq_quota, close_cycle;
} app_realtime_priority_contract_t;
/* The dispatcher already owns an installed, validated phase. Derive its
 * reservation directly without reloading/validating the profile catalog on
 * every release; all arithmetic uses the independent physical cadence. */
static inline app_realtime_priority_contract_t app_realtime_phase_priority(
    app_realtime_phase_id_t phase_id, const app_realtime_phase_contract_t *phase)
{
    const bool eligible = phase_id != APP_REALTIME_PHASE_MODEL &&
        phase_id != APP_REALTIME_PHASE_TRIGGER_MEASURE && phase_id != APP_REALTIME_PHASE_GUARD;
    const uint32_t width = phase->end_cycle - phase->start_cycle;
    const uint32_t quota = eligible ? 1u +
        (width + PROJECT_CORE1_PRIORITY_RX_MIN_PHYSICAL_CYCLES - 1u) /
            PROJECT_CORE1_PRIORITY_RX_MIN_PHYSICAL_CYCLES : 0u;
    const uint32_t irq_cycles = quota * PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES;
    return (app_realtime_priority_contract_t){
        .background_cycles = phase->wcet_cycles - irq_cycles,
        .irq_cycles = irq_cycles, .irq_quota = quota,
        .close_cycle = phase->end_cycle -
            (eligible ? PROJECT_CORE1_PRIORITY_RX_CLOSE_CYCLES : 0u)};
}
/* The close lead is an explicit reservation, never GUARD or spare phase time. */
bool app_realtime_profile_priority(uint32_t cycle_cycles,
    app_realtime_phase_id_t phase_id, app_realtime_priority_contract_t *priority);
/* Caller owns the schedule publication boundary. Retains cumulative timing
 * evidence; profile_generation identifies the installed table. */
bool app_realtime_profile_install(app_realtime_schedule_snapshot_t *schedule,
    uint32_t cycle_cycles, uint32_t generation);

#endif
