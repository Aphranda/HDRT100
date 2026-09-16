#ifndef TDMA_PRIORITY_TX_H
#define TDMA_PRIORITY_TX_H

#include "tdma_ring_runtime.h"
#include "tdma_flight_engine.h"

typedef enum {
    TDMA_PRIORITY_TX_DISABLED = 0,
    TDMA_PRIORITY_TX_EMPTY,
    TDMA_PRIORITY_TX_READY
} tdma_priority_tx_result_t;

/* Registered while STOPPED. Invoked once by the Core1 origin owner, outside
 * the IRQ window. READY supplies an immutable complete local mailbox; EMPTY
 * reserves the slot without granting a new record. No ordinary FIFO or
 * Core0 overlay work may replace an enabled priority slot. A NULL config is
 * STOP retirement (mailbox is NULL too), not a request to produce data.
 * A READY return is an offer, not proof of DMA selection or wire delivery. */
typedef tdma_priority_tx_result_t (*tdma_priority_tx_provider_t)(
    const tdma_ring_runtime_config_t *config,
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE]);

#endif
