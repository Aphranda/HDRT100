/* Minimal doubles for the reference-discipline scenario. */

#include <stdbool.h>
#include <stdint.h>

#include "tdma_ring_runtime.h"
#include "vdc_dpll_manager.h"

bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{
    if (!out) return false;
    *out = (tdma_ring_runtime_snapshot_t){0};
    return true;
}

uint32_t vdc_dpll_manager_feedback_session(void) { return 0u; }
