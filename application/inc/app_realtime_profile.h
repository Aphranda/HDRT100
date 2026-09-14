#ifndef APP_REALTIME_PROFILE_H
#define APP_REALTIME_PROFILE_H

#include <stdbool.h>
#include "app.h"

/* Pure table lookup. This does not authorize a lifecycle change or a frame.
 * The application owner installs a whole table only at a stopped boundary. */
bool app_realtime_profile_supported(uint32_t cycle_cycles);
bool app_realtime_profile_phase(uint32_t cycle_cycles,
    app_realtime_phase_id_t phase_id, app_realtime_phase_contract_t *phase);
/* Caller owns the schedule publication boundary. Retains cumulative timing
 * evidence; profile_generation identifies the installed table. */
bool app_realtime_profile_install(app_realtime_schedule_snapshot_t *schedule,
    uint32_t cycle_cycles, uint32_t generation);

#endif
