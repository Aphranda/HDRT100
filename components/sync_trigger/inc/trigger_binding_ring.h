#ifndef TRIGGER_BINDING_RING_H
#define TRIGGER_BINDING_RING_H

#include <stdbool.h>

#include "tdma_runtime_owner.h"
#include "trigger_deployment.h"

/* Shared "retained binding still matches the live ring/staged configuration"
 * predicate. The counter and gateway services carried the same compound
 * expression; this keeps one definition and lets each caller keep its own
 * reject-code mapping (COUNTER_CHANGED / GATEWAY_CHANGED). */
static inline bool trigger_binding_ring_staged_current(
    const trigger_deployment_binding_t *binding,
    const tdma_ring_clock_snapshot_t *ring,
    const tdma_service_ring_runtime_config_t *staged)
{
    return !(ring->enabled || ring->adapter_started ||
             ring->config_seq != ring->applied_config_seq ||
             ring->config_seq != binding->ring_config || !staged->enabled ||
             staged->local_slot_id != binding->local_slot ||
             staged->schedule_crc32 != binding->schedule_crc);
}

#endif /* TRIGGER_BINDING_RING_H */
