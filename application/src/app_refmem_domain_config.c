#include "app_refmem_domain_config.h"

#include <stdint.h>

#include "refmem_command.h"
#include "refmem_domain_config.h"
#include "vdc_dpll_manager.h"

/* N6.2: DPLL is the first RUN-safe domain.  VDC/T2 handlers are added in
 * N6.3/N6.4; until then their command types are authorized by RefMem but have
 * no registered handler, so dispatch fails closed with reason 13. */

static bool reject(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    }
    return false;
}

static bool dpll_role_handler(const uint32_t *params, uint32_t param_words,
                              uint32_t *reason)
{
    uint32_t generation = 0u;
    if (params == NULL || param_words != 2u ||
        !vdc_dpll_manager_request_dpll_role(params[0], params[1],
                                            &generation)) {
        return reject(reason);
    }
    return true;
}

static bool dpll_tune_handler(const uint32_t *params, uint32_t param_words,
                              uint32_t *reason)
{
    uint32_t generation = 0u;
    if (params == NULL || param_words != 5u ||
        !vdc_dpll_manager_request_debug_servo_tune(
            (int32_t)params[0], (int32_t)params[1], params[2], params[3],
            params[4], &generation)) {
        return reject(reason);
    }
    return true;
}

static bool dpll_override_handler(const uint32_t *params, uint32_t param_words,
                                  uint32_t *reason)
{
    uint32_t generation = 0u;
    if (params == NULL || param_words != 1u ||
        !vdc_dpll_manager_request_debug_continue(params[0] != 0u,
                                                 &generation)) {
        return reject(reason);
    }
    return true;
}

bool app_refmem_domain_config_init(void)
{
    refmem_domain_config_reset();
    return refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_ROLE, 2u,
                                         dpll_role_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_TUNE, 5u,
                                         dpll_tune_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_OVERRIDE,
                                         1u, dpll_override_handler);
}
