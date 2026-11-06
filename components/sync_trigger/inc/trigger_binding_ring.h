#ifndef TRIGGER_BINDING_RING_H
#define TRIGGER_BINDING_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_runtime_owner.h"
#include "trigger_deployment.h"

/* First failing condition of the "retained binding still matches the live
 * ring/staged configuration and this board" check. The order is fixed because
 * the action service emits one bench reject code per condition
 * (41,42,43,44,45,46,47); the counter/gateway services collapse any failure to
 * their single CHANGED code. */
typedef enum {
    TRIGGER_BINDING_RING_OK = 0,
    TRIGGER_BINDING_RING_RUNNING,     /* ring enabled or adapter started */
    TRIGGER_BINDING_RING_SEQ_PENDING, /* config_seq != applied_config_seq */
    TRIGGER_BINDING_RING_STAGED_OFF,  /* staged config disabled */
    TRIGGER_BINDING_RING_CONFIG,      /* ring config_seq != binding ring_config */
    TRIGGER_BINDING_RING_UID,         /* local uid != binding local_uid */
    TRIGGER_BINDING_RING_SLOT,        /* staged slot != binding local_slot */
    TRIGGER_BINDING_RING_SCHEDULE,    /* staged schedule CRC mismatch */
} trigger_binding_ring_result_t;

static inline trigger_binding_ring_result_t trigger_binding_ring_check(
    const trigger_deployment_binding_t *binding,
    uint64_t local_uid,
    const tdma_ring_clock_snapshot_t *ring,
    const tdma_service_ring_runtime_config_t *staged)
{
    if (ring->enabled || ring->adapter_started) {
        return TRIGGER_BINDING_RING_RUNNING;
    }
    if (ring->config_seq != ring->applied_config_seq) {
        return TRIGGER_BINDING_RING_SEQ_PENDING;
    }
    if (!staged->enabled) {
        return TRIGGER_BINDING_RING_STAGED_OFF;
    }
    if (ring->config_seq != binding->ring_config) {
        return TRIGGER_BINDING_RING_CONFIG;
    }
    if (local_uid != binding->local_uid) {
        return TRIGGER_BINDING_RING_UID;
    }
    if (staged->local_slot_id != binding->local_slot) {
        return TRIGGER_BINDING_RING_SLOT;
    }
    if (staged->schedule_crc32 != binding->schedule_crc) {
        return TRIGGER_BINDING_RING_SCHEDULE;
    }
    return TRIGGER_BINDING_RING_OK;
}

#endif /* TRIGGER_BINDING_RING_H */
