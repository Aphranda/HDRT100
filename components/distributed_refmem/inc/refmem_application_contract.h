#ifndef REFMEM_APPLICATION_CONTRACT_H
#define REFMEM_APPLICATION_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_application_model.h"

bool refmem_application_contract_validate_application_map(
    const refmem_application_map_t *application_map);
bool refmem_application_contract_validate_generic_node_table(
    const refmem_generic_node_table_t *node_table);
bool refmem_application_contract_validate_board_capability_table(
    const refmem_board_capability_table_t *board_table,
    uint32_t node_count);
bool refmem_application_contract_validate_slot_substrate(
    const refmem_generic_node_table_t *node_table,
    const refmem_board_capability_table_t *board_table);

#endif
