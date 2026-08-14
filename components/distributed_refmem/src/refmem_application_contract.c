#include "refmem_application_contract.h"

#include <stddef.h>

#include "refmem_realtime_contract.h"

bool refmem_application_contract_validate_application_map(
    const refmem_application_map_t *application_map)
{
    const uint32_t allowed_node_mask =
        (1u << REFMEM_APP_MODEL_NODE_COUNT) - 1u;
    if (application_map == NULL ||
        application_map->version != REFMEM_APP_MODEL_VERSION ||
        application_map->application_id == 0u ||
        application_map->application_version == 0u ||
        application_map->profile_id == 0u ||
        application_map->layout_version != DISTRIBUTED_REFMEM_LAYOUT_VERSION ||
        application_map->target_node_mask == 0u ||
        (application_map->target_node_mask & ~allowed_node_mask) != 0u) {
        return false;
    }

    return true;
}

bool refmem_application_contract_validate_generic_node_table(
    const refmem_generic_node_table_t *node_table)
{
    if (node_table == NULL ||
        node_table->version != REFMEM_APP_MODEL_VERSION ||
        node_table->node_count != REFMEM_APP_MODEL_NODE_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < node_table->node_count; i++) {
        const refmem_app_node_entry_t *node = &node_table->node[i];
        if (node->node_id != i ||
            (node->capability_mask & REFMEM_APP_CAP_BASELINE) !=
                REFMEM_APP_CAP_BASELINE ||
            node->claim_policy > REFMEM_APP_CLAIM_DISABLED ||
            node->online_required > 1u ||
            node->fail_policy > REFMEM_APP_FAIL_REPORT_ONLY) {
            return false;
        }

        if (node->online_required != 0u &&
            node->claim_policy != REFMEM_APP_CLAIM_STRICT_UUID &&
            node->claim_policy != REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT) {
            return false;
        }

        if (node->claim_policy == REFMEM_APP_CLAIM_SPARE_DYNAMIC &&
            (node->online_required != 0u || node->fail_policy != REFMEM_APP_FAIL_REPORT_ONLY)) {
            return false;
        }

        if (node->claim_policy == REFMEM_APP_CLAIM_DISABLED &&
            (node->online_required != 0u || node->claim_priority != 0u)) {
            return false;
        }
    }

    return true;
}

bool refmem_application_contract_validate_board_capability_table(
    const refmem_board_capability_table_t *board_table,
    uint32_t node_count)
{
    if (board_table == NULL ||
        node_count == 0u ||
        node_count > REFMEM_APP_MODEL_NODE_COUNT ||
        board_table->version != REFMEM_APP_MODEL_VERSION ||
        board_table->board_count < node_count ||
        board_table->board_count > REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < board_table->board_count; i++) {
        const refmem_board_capability_entry_t *board = &board_table->board[i];
        if (board->board_id != i ||
            (board->capability_mask & REFMEM_APP_CAP_BASELINE) !=
                REFMEM_APP_CAP_BASELINE ||
            board->active_default_slot >= node_count ||
            board->online_required > 1u) {
            return false;
        }

        const uint32_t io_capability =
            refmem_realtime_contract_io_capability_mask(board->io_constraint_mask);
        const uint32_t ip_capability =
            refmem_realtime_contract_ip_capability_mask(board->ip_core_mask);
        if (((io_capability | ip_capability) & ~board->capability_mask) != 0u) {
            return false;
        }
    }

    return true;
}

bool refmem_application_contract_validate_slot_substrate(
    const refmem_generic_node_table_t *node_table,
    const refmem_board_capability_table_t *board_table)
{
    if (!refmem_application_contract_validate_generic_node_table(node_table)) {
        return false;
    }

    return refmem_application_contract_validate_board_capability_table(
        board_table,
        node_table->node_count);
}
