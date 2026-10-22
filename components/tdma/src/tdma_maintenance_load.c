#include "tdma_maintenance_load.h"

#include <stddef.h>

bool tdma_maintenance_load_valid(const tdma_maintenance_load_t *load)
{
    return load != NULL &&
        load->load_id != 0u &&
        load->node_id != 0u &&
        load->instance_id != 0u &&
        load->payload_class != 0u &&
        load->window_period_cycles != 0u &&
        load->window_bytes != 0u &&
        load->max_frame_size != 0u &&
        load->max_frame_size <= load->window_bytes &&
        load->required_mask != 0u;
}

tdma_origin_tx_selector_t tdma_maintenance_select_origin_tx(
    const tdma_maintenance_load_t *load,
    const tdma_origin_tx_facts_t *facts)
{
    if (load == NULL || facts == NULL || !tdma_maintenance_load_valid(load)) {
        return TDMA_ORIGIN_TX_NONE;
    }
    /* T2 always wins the shared TX opportunity. */
    if (facts->t2_pending) {
        return TDMA_ORIGIN_TX_T2;
    }
    if (!facts->maintenance_pending || !facts->window_open ||
        facts->window_credit_bytes < load->max_frame_size) {
        return TDMA_ORIGIN_TX_NONE;
    }
    return TDMA_ORIGIN_TX_MAINTENANCE;
}
