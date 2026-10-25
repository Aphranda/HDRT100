/* Link-time doubles for the platform services the output-delay unit calls.
 * Selected with -Wl,--wrap=<symbol>; production code is untouched. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "product_config.h"
#include "tdma_service.h"

uint32_t __wrap_get_core_num(void) { return 0u; }

/* The TDMA service handle is owned by the manager unit in production; the
 * scenario only needs a non-NULL value because the wrapped service call
 * ignores it. */
tdma_service_service_t *s_vdc_tdma_service = (tdma_service_service_t *)(void *)1;

int32_t fake_output_compensation_ns;
int fake_output_compensation_writes;
bool fake_feedback_session;
bool fake_metadata_ok = true;

bool __wrap_product_config_get_dpll_output_compensation_ns(int32_t *out)
{
    *out = fake_output_compensation_ns;
    return true;
}

bool __wrap_product_config_set_dpll_output_compensation_ns(int32_t value)
{
    fake_output_compensation_ns = value;
    ++fake_output_compensation_writes;
    return true;
}

bool __wrap_tdma_service_update_stopped_metadata(tdma_service_service_t *service,
                                                 bool (*callback)(void *),
                                                 void *context)
{
    (void)service;
    if (!fake_metadata_ok || !callback) return false;
    return callback(context);
}

bool __wrap_tdma_service_run_stopped_maintenance(tdma_service_service_t *service,
                                                 bool (*callback)(void *),
                                                 void *context)
{
    (void)service;
    if (!callback) return false;
    return callback(context);
}

bool __wrap_vdc_dpll_manager_feedback_session(void) { return fake_feedback_session; }
