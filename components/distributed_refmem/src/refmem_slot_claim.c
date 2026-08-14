#include "refmem_slot_claim.h"

#include <string.h>

#include "ota_crc32.h"

static uint32_t refmem_slot_claim_crc32_update(uint32_t crc, const void *data, size_t size)
{
    return ota_crc32_update(crc, (const uint8_t *)data, size);
}

static uint32_t refmem_slot_claim_loaded_instance_mask(
    uint32_t slot_id,
    const refmem_node_load_table_t *node_load_table,
    const refmem_fb_instance_table_t *instance_table)
{
    uint32_t mask = 0u;
    if (node_load_table == NULL || instance_table == NULL) {
        return mask;
    }

    for (uint32_t i = 0u; i < node_load_table->load_count; i++) {
        const refmem_node_load_entry_t *load = &node_load_table->load[i];
        if (load->enabled == 0u ||
            load->node_id != slot_id ||
            load->instance_id >= instance_table->instance_count ||
            load->instance_id >= 32u) {
            continue;
        }

        const refmem_fb_instance_entry_t *instance =
            &instance_table->instance[load->instance_id];
        if (instance->instance_id == load->instance_id &&
            instance->enable_condition != 0u) {
            mask |= (1u << load->instance_id);
        }
    }
    return mask;
}

static uint32_t refmem_slot_claim_map_crc32(const refmem_slot_claim_map_t *map)
{
    uint32_t crc = 0xFFFFFFFFu;
    if (map == NULL) {
        return 0u;
    }

    crc = refmem_slot_claim_crc32_update(crc, &map->version, sizeof(map->version));
    crc = refmem_slot_claim_crc32_update(crc, &map->claim_epoch, sizeof(map->claim_epoch));
    crc = refmem_slot_claim_crc32_update(crc, &map->slot_count, sizeof(map->slot_count));
    crc = refmem_slot_claim_crc32_update(crc, &map->candidate_count, sizeof(map->candidate_count));
    crc = refmem_slot_claim_crc32_update(crc, &map->assigned_count, sizeof(map->assigned_count));
    crc = refmem_slot_claim_crc32_update(crc, &map->conflict_count, sizeof(map->conflict_count));
    crc = refmem_slot_claim_crc32_update(crc, &map->overflow_count, sizeof(map->overflow_count));
    crc = refmem_slot_claim_crc32_update(crc, &map->disabled_count, sizeof(map->disabled_count));
    for (uint32_t i = 0u; i < map->slot_count && i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        crc = refmem_slot_claim_crc32_update(crc, &map->slot[i], sizeof(map->slot[i]));
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool refmem_slot_claim_add_candidate(refmem_slot_claim_map_t *map,
                                            const refmem_app_node_entry_t *node,
                                            const refmem_board_capability_entry_t *board,
                                            uint32_t candidate_id,
                                            const refmem_node_load_table_t *node_load_table,
                                            const refmem_fb_instance_table_t *instance_table)
{
    if (map == NULL || node == NULL || board == NULL) {
        return false;
    }

    if (candidate_id >= REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX) {
        map->overflow_count++;
        return true;
    }

    map->candidate_count++;
    if (board->active_default_slot >= map->slot_count ||
        board->active_default_slot >= REFMEM_APP_MODEL_NODE_COUNT) {
        map->overflow_count++;
        return true;
    }

    refmem_slot_claim_assignment_t *slot = &map->slot[board->active_default_slot];
    if (slot->claim_state == REFMEM_SLOT_CLAIM_DISABLED) {
        slot->reason = REFMEM_SLOT_CLAIM_REASON_DISABLED_SLOT;
        return true;
    }

    if (slot->claim_count != 0u) {
        slot->claim_count++;
        slot->claim_state = REFMEM_SLOT_CLAIM_CONFLICT;
        slot->reason = REFMEM_SLOT_CLAIM_REASON_DUPLICATE_SLOT;
        map->conflict_count++;
        return true;
    }

    slot->board_id = board->board_id;
    slot->board_uuid_crc32 = board->board_uuid_crc32;
    slot->capability_mask = node->capability_mask & board->capability_mask;
    slot->io_constraint_mask = board->io_constraint_mask;
    slot->ip_core_mask = board->ip_core_mask;
    slot->loaded_instance_mask =
        refmem_slot_claim_loaded_instance_mask(slot->slot_id, node_load_table, instance_table);
    slot->claim_count = 1u;
    slot->claim_state = REFMEM_SLOT_CLAIM_CLAIMED;
    slot->reason = REFMEM_SLOT_CLAIM_REASON_OK;
    slot->claim_policy = node->claim_policy;
    slot->claim_priority = node->claim_priority;
    slot->last_claim_seq = candidate_id + 1u;
    map->assigned_count++;

    if (node->node_uuid_crc32 != board->board_uuid_crc32) {
        slot->claim_state = REFMEM_SLOT_CLAIM_MISMATCH;
        slot->reason = REFMEM_SLOT_CLAIM_REASON_UUID_MISMATCH;
        map->conflict_count++;
    } else if (node->hw_profile_crc32 != board->hw_profile_crc32) {
        slot->claim_state = REFMEM_SLOT_CLAIM_MISMATCH;
        slot->reason = REFMEM_SLOT_CLAIM_REASON_HW_PROFILE_MISMATCH;
        map->conflict_count++;
    }

    return true;
}

bool refmem_slot_claim_derive_map(const refmem_generic_node_table_t *node_table,
                                  const refmem_board_capability_table_t *board_table,
                                  const refmem_node_load_table_t *node_load_table,
                                  const refmem_fb_instance_table_t *instance_table,
                                  refmem_slot_claim_map_t *map)
{
    if (node_table == NULL || board_table == NULL || map == NULL ||
        node_table->node_count > REFMEM_APP_MODEL_NODE_COUNT ||
        board_table->board_count > REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT) {
        return false;
    }

    memset(map, 0, sizeof(*map));
    map->version = REFMEM_SLOT_CLAIM_VERSION;
    map->claim_epoch = 1u;
    map->slot_count = node_table->node_count;

    for (uint32_t i = 0u; i < map->slot_count; i++) {
        refmem_slot_claim_assignment_t *slot = &map->slot[i];
        const refmem_app_node_entry_t *node = &node_table->node[i];
        slot->slot_id = i;
        slot->claim_epoch = map->claim_epoch;
        slot->claim_policy = node->claim_policy;
        slot->claim_priority = node->claim_priority;
        if (node->claim_policy == REFMEM_APP_CLAIM_DISABLED) {
            slot->claim_state = REFMEM_SLOT_CLAIM_DISABLED;
            slot->reason = REFMEM_SLOT_CLAIM_REASON_DISABLED_SLOT;
            map->disabled_count++;
        } else {
            slot->claim_state = REFMEM_SLOT_CLAIM_UNCLAIMED;
            slot->reason = REFMEM_SLOT_CLAIM_REASON_OK;
        }
    }

    for (uint32_t i = 0u; i < board_table->board_count; i++) {
        const refmem_board_capability_entry_t *board = &board_table->board[i];
        if (i >= REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX) {
            map->overflow_count++;
            continue;
        }
        if (board->active_default_slot >= node_table->node_count) {
            map->candidate_count++;
            map->overflow_count++;
            continue;
        }
        const refmem_app_node_entry_t *node = &node_table->node[board->active_default_slot];
        (void)refmem_slot_claim_add_candidate(map,
                                              node,
                                              board,
                                              i,
                                              node_load_table,
                                              instance_table);
    }

    for (uint32_t i = 0u; i < map->slot_count; i++) {
        refmem_slot_claim_assignment_t *slot = &map->slot[i];
        slot->claim_crc32 = refmem_slot_claim_crc32_update(0xFFFFFFFFu,
                                                           slot,
                                                           sizeof(*slot) -
                                                               sizeof(slot->claim_crc32)) ^
                            0xFFFFFFFFu;
    }
    map->map_crc32 = refmem_slot_claim_map_crc32(map);
    return true;
}

const refmem_slot_claim_assignment_t *refmem_slot_claim_find_assignment(
    const refmem_slot_claim_map_t *map,
    uint32_t slot_id)
{
    if (map == NULL || slot_id >= map->slot_count || slot_id >= REFMEM_APP_MODEL_NODE_COUNT) {
        return NULL;
    }
    return &map->slot[slot_id];
}
