#ifndef TDMA_MAINTENANCE_LOAD_H
#define TDMA_MAINTENANCE_LOAD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * HAOFV load descriptor for the TDMA maintenance payload.
 *
 * The load declares a payload and a bounded window; it does not own the TDMA
 * transport, PIO, DMA or core1.  The origin TX scheduler selects between the
 * T2 fast path and this load from these facts.
 */

#define TDMA_MAINTENANCE_LOAD_VERSION 1u

typedef struct {
    uint32_t load_id;
    uint32_t node_id;
    uint32_t instance_id;
    uint32_t payload_class;
    uint32_t window_period_cycles;
    uint32_t window_bytes;
    uint32_t max_frame_size;
    uint32_t required_mask;
} tdma_maintenance_load_t;

typedef enum {
    TDMA_ORIGIN_TX_NONE = 0u,
    TDMA_ORIGIN_TX_T2 = 1u,
    TDMA_ORIGIN_TX_MAINTENANCE = 2u,
} tdma_origin_tx_selector_t;

typedef struct {
    bool t2_pending;
    bool maintenance_pending;
    bool window_open;
    uint32_t window_credit_bytes;
} tdma_origin_tx_facts_t;

bool tdma_maintenance_load_valid(const tdma_maintenance_load_t *load);

tdma_origin_tx_selector_t tdma_maintenance_select_origin_tx(
    const tdma_maintenance_load_t *load,
    const tdma_origin_tx_facts_t *facts);

#endif /* TDMA_MAINTENANCE_LOAD_H */
