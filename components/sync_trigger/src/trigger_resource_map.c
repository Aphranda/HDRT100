#include "trigger_resource_map.h"

#include "resource_arbiter.h"

uint32_t trigger_resource_map_from_mode_resources(uint32_t mode_resources)
{
    uint32_t arbiter_resources = 0u;

    if ((mode_resources & SYNC_IO_MODE_RESOURCE_PIO_WAVE) != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_PIO1;
    }
    if ((mode_resources & SYNC_IO_MODE_RESOURCE_PIO_AUX) != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_PIO2;
    }
    if ((mode_resources & SYNC_IO_MODE_RESOURCE_DMA) != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_DMA;
    }
    if ((mode_resources & (SYNC_IO_MODE_RESOURCE_AUX_RX |
                           SYNC_IO_MODE_RESOURCE_AUX_TX)) != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_AUX;
    }

    return arbiter_resources;
}

uint32_t trigger_resource_map_from_mode_hw(const sync_io_mode_hw_resources_t *hw)
{
    if (hw == NULL) {
        return 0u;
    }

    uint32_t arbiter_resources = 0u;

    if ((hw->pio_mask & SYNC_IO_MODE_HW_PIO0) != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_PIO0;
    }
    if ((hw->pio_mask & SYNC_IO_MODE_HW_PIO1) != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_PIO1;
    }
    if ((hw->pio_mask & SYNC_IO_MODE_HW_PIO2) != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_PIO2;
    }
    if (hw->dma_channel_mask != 0u) {
        arbiter_resources |= RESOURCE_ARBITER_RESOURCE_DMA;
    }

    return arbiter_resources;
}

uint32_t trigger_resource_map_for_mode(sync_io_mode_id_t mode_id)
{
    const sync_io_mode_ops_t *ops = sync_io_mode_get_ops(mode_id);
    if (ops == NULL) {
        return 0u;
    }

    return trigger_resource_map_from_mode_resources(ops->resources) |
           trigger_resource_map_from_mode_hw(&ops->hw);
}

uint32_t trigger_resource_map_for_state(trig_state_t state)
{
    switch (state) {
    case TRIG_STATE_SEQ_CONFIGURED:
    case TRIG_STATE_SEQ_ARMED:
        return trigger_resource_map_for_mode(SYNC_IO_MODE_ID_SEQ_STEP);
    case TRIG_STATE_ENC_CONFIGURED:
    case TRIG_STATE_ENC_ARMED:
        return trigger_resource_map_for_mode(SYNC_IO_MODE_ID_ENC_COUNT);
    case TRIG_STATE_BISS_CONFIGURED:
    case TRIG_STATE_BISS_ARMED:
        return trigger_resource_map_for_mode(SYNC_IO_MODE_ID_BISS_TAP);
    default:
        return 0u;
    }
}
