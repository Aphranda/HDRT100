#include "refmem_realtime_contract.h"

#include <string.h>

#define REFMEM_RT_CONTRACT_BASELINE \
    (REFMEM_APP_CAP_BOARD | REFMEM_APP_CAP_REFMEM | REFMEM_APP_CAP_VDC)

static const refmem_board_capability_entry_t *refmem_realtime_find_board_for_node(
    const refmem_board_capability_table_t *board_table,
    uint32_t node_id)
{
    if (board_table == NULL || board_table->board_count > REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT) {
        return NULL;
    }

    for (uint32_t i = 0u; i < board_table->board_count; i++) {
        const refmem_board_capability_entry_t *board = &board_table->board[i];
        if (board->active_default_slot == node_id) {
            return board;
        }
    }
    return NULL;
}

uint32_t refmem_realtime_contract_resource_capability_mask(uint32_t resource_claim)
{
    uint32_t capability = 0u;
    if ((resource_claim & REFMEM_APP_RESOURCE_FLASH) != 0u) {
        capability |= REFMEM_APP_CAP_FLASH;
    }
    if ((resource_claim & REFMEM_APP_RESOURCE_SD) != 0u) {
        capability |= REFMEM_APP_CAP_SD;
    }
    if ((resource_claim & REFMEM_APP_RESOURCE_USB) != 0u) {
        capability |= REFMEM_APP_CAP_USB;
    }
    if ((resource_claim & REFMEM_APP_RESOURCE_PIO) != 0u) {
        capability |= REFMEM_APP_CAP_PIO;
    }
    if ((resource_claim & REFMEM_APP_RESOURCE_DMA) != 0u) {
        capability |= REFMEM_APP_CAP_DMA;
    }
    if ((resource_claim & REFMEM_APP_RESOURCE_LCD) != 0u) {
        capability |= REFMEM_APP_CAP_LCD;
    }
    if ((resource_claim & REFMEM_APP_RESOURCE_RJ45) != 0u) {
        capability |= REFMEM_APP_CAP_RJ45;
    }
    if ((resource_claim & REFMEM_APP_RESOURCE_CORE1_RT) != 0u) {
        capability |= REFMEM_APP_CAP_CORE1_RT;
    }
    return capability;
}

uint32_t refmem_realtime_contract_io_capability_mask(uint32_t io_claim)
{
    uint32_t capability = 0u;
    if ((io_claim & REFMEM_APP_IO_SMA_IN) != 0u) {
        capability |= REFMEM_APP_CAP_SMA_IN;
    }
    if ((io_claim & REFMEM_APP_IO_SMA_OUT) != 0u) {
        capability |= REFMEM_APP_CAP_SMA_OUT;
    }
    if ((io_claim & REFMEM_APP_IO_RJ45_SYNC) != 0u) {
        capability |= REFMEM_APP_CAP_RJ45;
    }
    if ((io_claim & REFMEM_APP_IO_LINK_CONTROL) != 0u) {
        capability |= REFMEM_APP_CAP_LINK_CONTROL;
    }
    if ((io_claim & REFMEM_APP_IO_BISS_C) != 0u) {
        capability |= REFMEM_APP_CAP_BISS_C;
    }
    if ((io_claim & REFMEM_APP_IO_UART_RS485) != 0u) {
        capability |= REFMEM_APP_CAP_UART_RS485;
    }
    if ((io_claim & REFMEM_APP_IO_PIO_SPI_SYNC) != 0u) {
        capability |= REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
    }
    if ((io_claim & REFMEM_APP_IO_MODEL_SIGNAL_MASK) != 0u) {
        capability |= REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
    }
    return capability;
}

uint32_t refmem_realtime_contract_ip_capability_mask(uint32_t ip_core_claim)
{
    uint32_t capability = 0u;
    if ((ip_core_claim & REFMEM_APP_IP_PULSE_CAPTURE) != 0u) {
        capability |= REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
    }
    if ((ip_core_claim & REFMEM_APP_IP_PULSE_FIRE) != 0u) {
        capability |= REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
    }
    if ((ip_core_claim & REFMEM_APP_IP_LINK_SEQUENCE) != 0u) {
        capability |= REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA |
                      REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_LINK_CONTROL;
    }
    if ((ip_core_claim & REFMEM_APP_IP_BISS_C_CODEC) != 0u) {
        capability |= REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA |
                      REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_BISS_C;
    }
    if ((ip_core_claim & REFMEM_APP_IP_RJ45_SYNC_DELTA) != 0u) {
        capability |= REFMEM_APP_CAP_RJ45;
    }
    if ((ip_core_claim & REFMEM_APP_IP_VDC_DPLL) != 0u) {
        capability |= REFMEM_APP_CAP_CORE1_RT;
    }
    if ((ip_core_claim & REFMEM_APP_IP_PIO_SPI_SYNC_DELTA) != 0u) {
        capability |= REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
    }
    return capability;
}

uint32_t refmem_realtime_contract_transport_resource_claim(uint32_t transport)
{
    switch (transport) {
    case REFMEM_APP_TRANSPORT_CORE_IPC:
        return REFMEM_APP_RESOURCE_CORE1_RT;
    case REFMEM_APP_TRANSPORT_RJ45_SYNC_RING:
        return REFMEM_APP_RESOURCE_RJ45;
    case REFMEM_APP_TRANSPORT_PIO_SPI:
        return REFMEM_APP_RESOURCE_PIO |
               REFMEM_APP_RESOURCE_DMA |
               REFMEM_APP_RESOURCE_CORE1_RT;
    case REFMEM_APP_TRANSPORT_LOCAL_QUEUE:
    case REFMEM_APP_TRANSPORT_COMMAND_SLOT:
    default:
        return 0u;
    }
}

uint32_t refmem_realtime_contract_transport_io_claim(uint32_t transport)
{
    switch (transport) {
    case REFMEM_APP_TRANSPORT_RJ45_SYNC_RING:
        return REFMEM_APP_IO_RJ45_SYNC;
    case REFMEM_APP_TRANSPORT_PIO_SPI:
        return REFMEM_APP_IO_PIO_SPI_SYNC;
    case REFMEM_APP_TRANSPORT_LOCAL_QUEUE:
    case REFMEM_APP_TRANSPORT_CORE_IPC:
    case REFMEM_APP_TRANSPORT_COMMAND_SLOT:
    default:
        return 0u;
    }
}

uint32_t refmem_realtime_contract_transport_ip_core_claim(uint32_t transport)
{
    switch (transport) {
    case REFMEM_APP_TRANSPORT_RJ45_SYNC_RING:
        return REFMEM_APP_IP_RJ45_SYNC_DELTA;
    case REFMEM_APP_TRANSPORT_PIO_SPI:
        return REFMEM_APP_IP_PIO_SPI_SYNC_DELTA;
    case REFMEM_APP_TRANSPORT_LOCAL_QUEUE:
    case REFMEM_APP_TRANSPORT_CORE_IPC:
    case REFMEM_APP_TRANSPORT_COMMAND_SLOT:
    default:
        return 0u;
    }
}

bool refmem_realtime_contract_derive(const refmem_node_load_entry_t *load,
                                     const refmem_fb_instance_entry_t *instance,
                                     const refmem_app_node_entry_t *node,
                                     const refmem_board_capability_table_t *board_table,
                                     refmem_realtime_contract_t *contract)
{
    if (contract == NULL) {
        return false;
    }

    memset(contract, 0, sizeof(*contract));
    contract->result = REFMEM_RT_CONTRACT_BAD_ARGUMENT;

    if (load == NULL || instance == NULL || node == NULL || board_table == NULL ||
        load->enabled == 0u || instance->enable_condition == 0u) {
        return false;
    }

    const refmem_board_capability_entry_t *board =
        refmem_realtime_find_board_for_node(board_table, load->node_id);

    contract->node_id = load->node_id;
    contract->instance_id = load->instance_id;
    contract->resource_claim = instance->resource_claim;
    contract->io_claim = instance->io_claim;
    contract->ip_core_claim = instance->ip_core_claim;
    contract->time_budget_us = instance->time_budget_us;
    contract->required_capability_mask =
        refmem_realtime_contract_resource_capability_mask(instance->resource_claim) |
        refmem_realtime_contract_io_capability_mask(instance->io_claim) |
        refmem_realtime_contract_ip_capability_mask(instance->ip_core_claim);

    if (board == NULL) {
        contract->result = REFMEM_RT_CONTRACT_BOARD_NOT_FOUND;
        return false;
    }

    contract->board_id = board->board_id;
    contract->target_capability_mask = node->capability_mask & board->capability_mask;
    contract->target_io_constraint_mask = board->io_constraint_mask;
    contract->target_ip_core_mask = board->ip_core_mask;

    if (((node->capability_mask & REFMEM_RT_CONTRACT_BASELINE) != REFMEM_RT_CONTRACT_BASELINE) ||
        ((board->capability_mask & REFMEM_RT_CONTRACT_BASELINE) != REFMEM_RT_CONTRACT_BASELINE)) {
        contract->missing_capability_mask =
            REFMEM_RT_CONTRACT_BASELINE & ~contract->target_capability_mask;
        contract->result = REFMEM_RT_CONTRACT_MISSING_BASELINE;
        return false;
    }

    contract->missing_capability_mask =
        contract->required_capability_mask & ~contract->target_capability_mask;
    if (contract->missing_capability_mask != 0u) {
        contract->result = REFMEM_RT_CONTRACT_MISSING_CAPABILITY;
        return false;
    }

    contract->missing_io_mask = instance->io_claim & ~board->io_constraint_mask;
    if (contract->missing_io_mask != 0u) {
        contract->result = REFMEM_RT_CONTRACT_MISSING_IO;
        return false;
    }

    contract->missing_ip_core_mask = instance->ip_core_claim & ~board->ip_core_mask;
    if (contract->missing_ip_core_mask != 0u) {
        contract->result = REFMEM_RT_CONTRACT_MISSING_IP_CORE;
        return false;
    }

    contract->valid = 1u;
    contract->result = REFMEM_RT_CONTRACT_OK;
    return true;
}

bool refmem_realtime_contract_derive_from_claim_map(
    const refmem_node_load_entry_t *load,
    const refmem_fb_instance_entry_t *instance,
    const refmem_app_node_entry_t *node,
    const refmem_slot_claim_map_t *claim_map,
    refmem_realtime_contract_t *contract)
{
    if (contract == NULL) {
        return false;
    }

    memset(contract, 0, sizeof(*contract));
    contract->result = REFMEM_RT_CONTRACT_BAD_ARGUMENT;

    if (load == NULL || instance == NULL || node == NULL || claim_map == NULL ||
        load->enabled == 0u || instance->enable_condition == 0u) {
        return false;
    }

    const refmem_slot_claim_assignment_t *assignment =
        refmem_slot_claim_find_assignment(claim_map, load->node_id);

    contract->node_id = load->node_id;
    contract->instance_id = load->instance_id;
    contract->resource_claim = instance->resource_claim;
    contract->io_claim = instance->io_claim;
    contract->ip_core_claim = instance->ip_core_claim;
    contract->time_budget_us = instance->time_budget_us;
    contract->required_capability_mask =
        refmem_realtime_contract_resource_capability_mask(instance->resource_claim) |
        refmem_realtime_contract_io_capability_mask(instance->io_claim) |
        refmem_realtime_contract_ip_capability_mask(instance->ip_core_claim);

    if (assignment == NULL ||
        assignment->claim_state != REFMEM_SLOT_CLAIM_CLAIMED ||
        assignment->claim_count == 0u) {
        contract->result = REFMEM_RT_CONTRACT_SLOT_CLAIM_INVALID;
        return false;
    }

    contract->board_id = assignment->board_id;
    contract->target_capability_mask = assignment->capability_mask;
    contract->target_io_constraint_mask = assignment->io_constraint_mask;
    contract->target_ip_core_mask = assignment->ip_core_mask;

    if ((assignment->capability_mask & REFMEM_RT_CONTRACT_BASELINE) !=
        REFMEM_RT_CONTRACT_BASELINE) {
        contract->missing_capability_mask =
            REFMEM_RT_CONTRACT_BASELINE & ~assignment->capability_mask;
        contract->result = REFMEM_RT_CONTRACT_MISSING_BASELINE;
        return false;
    }

    contract->missing_capability_mask =
        contract->required_capability_mask & ~assignment->capability_mask;
    if (contract->missing_capability_mask != 0u) {
        contract->result = REFMEM_RT_CONTRACT_MISSING_CAPABILITY;
        return false;
    }

    contract->missing_io_mask = instance->io_claim & ~assignment->io_constraint_mask;
    if (contract->missing_io_mask != 0u) {
        contract->result = REFMEM_RT_CONTRACT_MISSING_IO;
        return false;
    }

    contract->missing_ip_core_mask = instance->ip_core_claim & ~assignment->ip_core_mask;
    if (contract->missing_ip_core_mask != 0u) {
        contract->result = REFMEM_RT_CONTRACT_MISSING_IP_CORE;
        return false;
    }

    contract->valid = 1u;
    contract->result = REFMEM_RT_CONTRACT_OK;
    return true;
}
