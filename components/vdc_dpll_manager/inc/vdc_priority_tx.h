#ifndef VDC_PRIORITY_TX_H
#define VDC_PRIORITY_TX_H

#include "tdma_priority_tx.h"

typedef enum {
    VDC_PRIORITY_TX_REJECT_NONE = 0,
    VDC_PRIORITY_TX_REJECT_STOP,
    VDC_PRIORITY_TX_REJECT_RETIRED,
    VDC_PRIORITY_TX_REJECT_CONFIG,
    VDC_PRIORITY_TX_REJECT_SOURCE,
    VDC_PRIORITY_TX_REJECT_MODEL,
    VDC_PRIORITY_TX_REJECT_BINDING,
    VDC_PRIORITY_TX_REJECT_SEQUENCE,
    VDC_PRIORITY_TX_REJECT_PROJECTION,
    VDC_PRIORITY_TX_REJECT_WIDTH,
    VDC_PRIORITY_TX_REJECT_CHANGED
} vdc_priority_tx_reject_t;

/* Core1 offer diagnostics, retained after STOP. Counts describe software
 * offers only, never DMA selection, wire delivery or timestamp accuracy. */
typedef struct {
    uint32_t schema, generation, active, retired, have_offer;
    uint32_t calls, encoded, repeated, rejected, last_reject;
    uint32_t source_epoch, event_sequence, source_identity, published_version;
    uint32_t model_token, session, role_generation, clock_epoch, clock_run;
    uint32_t local_slot, node_count, schedule_crc32, profile_crc32;
    uint32_t uncertainty_width;
    uint64_t event_time_lower;
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
} vdc_priority_tx_snapshot_t;

/* Core0, STOP only. A nonzero request requires an existing feedback session
 * and a generation strictly newer than every previous nonzero request.
 * Zero disables. This never changes the existing feedback session. */
bool vdc_dpll_manager_set_priority_sync(uint32_t generation);
uint32_t vdc_dpll_manager_priority_sync_generation(void);
/* One atomic-word snapshot attempt; failure leaves *out unchanged. */
bool vdc_dpll_manager_get_priority_tx(vdc_priority_tx_snapshot_t *out);

/* Registered Core1 TDMA callback. NULL config retires the current request;
 * only a new generation may bind again. EMPTY keeps the priority reservation.
 * Every non-READY result preserves caller mailbox bytes. */
tdma_priority_tx_result_t vdc_priority_tx_core1(
    const tdma_ring_runtime_config_t *config,
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE]);

#endif
