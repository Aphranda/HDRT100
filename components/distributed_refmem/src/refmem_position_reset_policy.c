/* N8.5 running-safe reset admission policy.  Split out so the topology
 * decision is host testable without the TDMA service; the reset itself still
 * lives in distributed_refmem_plane.c. */
#include "distributed_refmem.h"

bool refmem_position_reset_admits(uint32_t ring_enabled,
                                  uint32_t adapter_started,
                                  uint32_t config_seq,
                                  uint32_t applied_config_seq)
{
    return refmem_ring_transport_admits(ring_enabled, adapter_started,
                                        config_seq, applied_config_seq);
}

bool refmem_ring_transport_admits(uint32_t ring_enabled,
                                  uint32_t adapter_started,
                                  uint32_t config_seq,
                                  uint32_t applied_config_seq)
{
    return ring_enabled != 0u && adapter_started != 0u &&
           config_seq == applied_config_seq;
}

/* --- Control-plane transport admission (CONSOLE-035) -------------------- */
/* A bootstrap carrier is registered once by its owner (the STOP-state
 * low-speed SPI relay).  Registration is an init-time single writer; readers
 * use acquire/release so a service never observes a half-filled vtable. */
static refmem_bootstrap_transport_t s_bootstrap_transport;
static volatile uint32_t s_bootstrap_registered;

bool refmem_control_transport_register_bootstrap(
    const refmem_bootstrap_transport_t *transport)
{
    if (transport == NULL || transport->submit == NULL ||
        transport->ready == NULL) {
        return false;
    }
    s_bootstrap_transport = *transport;
    __atomic_store_n(&s_bootstrap_registered, 1u, __ATOMIC_RELEASE);
    return true;
}

void refmem_control_transport_unregister_bootstrap(void)
{
    __atomic_store_n(&s_bootstrap_registered, 0u, __ATOMIC_RELEASE);
    s_bootstrap_transport.submit = NULL;
    s_bootstrap_transport.ready = NULL;
}

bool refmem_control_transport_bootstrap_active(void)
{
    if (__atomic_load_n(&s_bootstrap_registered, __ATOMIC_ACQUIRE) == 0u) {
        return false;
    }
    /* The owner publishes readiness from its own handshake state; an ARM or
     * status reply must never be treated as proof that a peer is reachable. */
    return s_bootstrap_transport.ready != NULL && s_bootstrap_transport.ready();
}

refmem_control_transport_t refmem_control_transport_route(
    uint32_t ring_link_ready)
{
    if (ring_link_ready != 0u) {
        return REFMEM_CONTROL_TRANSPORT_TDMA_RING;
    }
    if (refmem_control_transport_bootstrap_active()) {
        return REFMEM_CONTROL_TRANSPORT_BOOTSTRAP_SPI;
    }
    return REFMEM_CONTROL_TRANSPORT_NONE;
}

bool refmem_control_transport_admits(uint32_t ring_ready,
                                     uint32_t bootstrap_ready)
{
    return ring_ready != 0u || bootstrap_ready != 0u;
}

bool refmem_control_transport_submit(const uint8_t *payload,
                                     uint32_t payload_size,
                                     uint8_t type,
                                     uint8_t generation,
                                     uint8_t target_mask)
{
    if (payload == NULL || payload_size == 0u ||
        !refmem_control_transport_bootstrap_active()) {
        return false;
    }
    return s_bootstrap_transport.submit(payload, payload_size, type,
                                        generation, target_mask);
}
