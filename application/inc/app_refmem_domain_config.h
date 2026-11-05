#ifndef APP_REFMEM_DOMAIN_CONFIG_H
#define APP_REFMEM_DOMAIN_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "guide_action_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the N6 domain-config handlers that call the owning TDMA/VDC/T2
 * APIs.  Called once from app_init after the domain owners are initialized
 * and before SCPI commands are reachable.  RefMem itself stays independent of
 * those components; it only dispatches through this registry. */
bool app_refmem_domain_config_init(void);

/* Consumes a guide trigger delivered by CAL_GUIDE_TRIGGER (T segment). Returns
 * false when no trigger is pending; the plan is copied on success. */
bool app_cal_guide_take_trigger(guide_action_plan_t *plan, uint32_t *epoch);
/* R segment: record progress while the ring is down (refmem parameter pool,
 * no link needed). Q reads it back after re-form; `epoch` is the trigger epoch
 * and doubles as the pool slot sequence, so a stale record never matches. */
bool app_cal_guide_publish_progress(const guide_action_progress_t *progress,
                                    uint32_t epoch);
bool app_cal_guide_read_progress(guide_action_progress_t *progress, uint32_t epoch);

#ifdef __cplusplus
}
#endif

#endif /* APP_REFMEM_DOMAIN_CONFIG_H */
