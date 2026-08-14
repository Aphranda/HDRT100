#ifndef REFMEM_REALTIME_CONTRACT_H
#define REFMEM_REALTIME_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_application_model.h"
#include "refmem_slot_claim.h"

typedef enum {
    REFMEM_RT_CONTRACT_OK = 0u,
    REFMEM_RT_CONTRACT_BAD_ARGUMENT = 1u,
    REFMEM_RT_CONTRACT_BOARD_NOT_FOUND = 2u,
    REFMEM_RT_CONTRACT_MISSING_BASELINE = 3u,
    REFMEM_RT_CONTRACT_MISSING_CAPABILITY = 4u,
    REFMEM_RT_CONTRACT_MISSING_IO = 5u,
    REFMEM_RT_CONTRACT_MISSING_IP_CORE = 6u,
    REFMEM_RT_CONTRACT_SLOT_CLAIM_INVALID = 7u,
} refmem_realtime_contract_result_t;

typedef struct {
    uint32_t node_id;
    uint32_t board_id;
    uint32_t instance_id;
    uint32_t resource_claim;
    uint32_t io_claim;
    uint32_t ip_core_claim;
    uint32_t target_capability_mask;
    uint32_t target_io_constraint_mask;
    uint32_t target_ip_core_mask;
    uint32_t required_capability_mask;
    uint32_t missing_capability_mask;
    uint32_t missing_io_mask;
    uint32_t missing_ip_core_mask;
    uint32_t time_budget_us;
    uint32_t valid;
    uint32_t result;
} refmem_realtime_contract_t;

uint32_t refmem_realtime_contract_resource_capability_mask(uint32_t resource_claim);
uint32_t refmem_realtime_contract_io_capability_mask(uint32_t io_claim);
uint32_t refmem_realtime_contract_ip_capability_mask(uint32_t ip_core_claim);
uint32_t refmem_realtime_contract_transport_resource_claim(uint32_t transport);
uint32_t refmem_realtime_contract_transport_io_claim(uint32_t transport);
uint32_t refmem_realtime_contract_transport_ip_core_claim(uint32_t transport);
bool refmem_realtime_contract_derive(const refmem_node_load_entry_t *load,
                                     const refmem_fb_instance_entry_t *instance,
                                     const refmem_app_node_entry_t *node,
                                     const refmem_board_capability_table_t *board_table,
                                     refmem_realtime_contract_t *contract);
bool refmem_realtime_contract_derive_from_claim_map(
    const refmem_node_load_entry_t *load,
    const refmem_fb_instance_entry_t *instance,
    const refmem_app_node_entry_t *node,
    const refmem_slot_claim_map_t *claim_map,
    refmem_realtime_contract_t *contract);

#endif
