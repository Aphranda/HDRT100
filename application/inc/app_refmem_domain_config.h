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

#ifdef __cplusplus
}
#endif

#endif /* APP_REFMEM_DOMAIN_CONFIG_H */
