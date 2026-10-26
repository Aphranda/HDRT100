#include "app_refmem_domain_config.h"

#include <stdint.h>

#include "app.h"
#include "refmem_command.h"
#include "refmem_domain_config.h"
#include "tdma_runtime_owner.h"
#include "vdc_dpll_manager.h"

/* N6.2: DPLL.  N6.3: TDMA online seat/load; topology/period stay
 * deployment-time and fail closed with NOT_ONLINE_SAFE.  VDC/T2 handlers are
 * added in N6.3/N6.4. */

static bool reject(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    }
    return false;
}

static bool reject_not_online(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE;
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

static bool tdma_ring_topology_handler(const uint32_t *params,
                                       uint32_t param_words, uint32_t *reason)
{
    (void)params;
    (void)param_words;
    return reject_not_online(reason);
}

static bool tdma_seat_rotation_handler(const uint32_t *params,
                                       uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 3u ||
        !tdma_runtime_owner_commit_seat_rotation3(params[0], params[1],
                                                  params[2])) {
        return reject(reason);
    }
    return true;
}

static bool tdma_period_handler(const uint32_t *params, uint32_t param_words,
                                uint32_t *reason)
{
    (void)params;
    (void)param_words;
    return reject_not_online(reason);
}

static bool tdma_load_mask_handler(const uint32_t *params, uint32_t param_words,
                                   uint32_t *reason)
{
    if (params == NULL || param_words != 1u ||
        !app_realtime_set_load_mask(params[0])) {
        return reject(reason);
    }
    return true;
}

bool app_refmem_domain_config_init(void)
{
    refmem_domain_config_reset();
    return refmem_domain_config_register(
               REFMEM_COMMAND_TYPE_TDMA_RING_TOPOLOGY, 2u,
               tdma_ring_topology_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TDMA_SEAT_ROTATION,
                                         3u, tdma_seat_rotation_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TDMA_PERIOD, 1u,
                                         tdma_period_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TDMA_LOAD_MASK, 1u,
                                         tdma_load_mask_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_ROLE, 2u,
                                         dpll_role_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_TUNE, 5u,
                                         dpll_tune_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_OVERRIDE,
                                         1u, dpll_override_handler);
}
