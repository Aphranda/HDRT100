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
    if (node_load_table == NULL || instance_table == NULL ||
        node_load_table->load_count > REFMEM_APP_MODEL_NODE_LOAD_COUNT ||
        instance_table->instance_count > REFMEM_APP_MODEL_INSTANCE_COUNT) {
        return mask;
    }

    for (uint32_t i = 0u; i < node_load_table->load_count; i++) {
        const refmem_node_load_entry_t *load = &node_load_table->load[i];
        if (load->enabled == 0u ||
            load->node_id != slot_id ||
            load->instance_id >= REFMEM_APP_MODEL_INSTANCE_COUNT ||
            load->instance_id >= 32u) {
            continue;
        }

        const refmem_fb_instance_entry_t *instance = NULL;
        bool duplicate = false;
        for (uint32_t j = 0u; j < instance_table->instance_count; ++j) {
            if (instance_table->instance[j].instance_id != load->instance_id)
                continue;
            if (instance != NULL) {
                duplicate = true;
                break;
            }
            instance = &instance_table->instance[j];
        }
        if (!duplicate && instance != NULL && instance->enable_condition != 0u) {
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
    crc = refmem_slot_claim_crc32_update(crc, &map->evidence_count, sizeof(map->evidence_count));
    for (uint32_t i = 0u; i < map->slot_count && i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        crc = refmem_slot_claim_crc32_update(crc, &map->slot[i], sizeof(map->slot[i]));
    }
    for (uint32_t i = 0u; i < map->evidence_count && i < REFMEM_SLOT_CLAIM_EVIDENCE_MAX; i++) {
        crc = refmem_slot_claim_crc32_update(crc, &map->evidence[i], sizeof(map->evidence[i]));
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t refmem_slot_claim_assignment_crc32(
    const refmem_slot_claim_assignment_t *slot)
{
    if (slot == NULL) {
        return 0u;
    }
    return refmem_slot_claim_crc32_update(0xFFFFFFFFu,
                                          slot,
                                          sizeof(*slot) -
                                              sizeof(slot->claim_crc32)) ^
           0xFFFFFFFFu;
}

static void refmem_slot_claim_record_evidence(refmem_slot_claim_map_t *map,
                                              const refmem_board_capability_entry_t *board,
                                              uint32_t candidate_id,
                                              uint32_t slot_id,
                                              uint32_t claim_state,
                                              uint32_t reason,
                                              uint32_t claim_policy,
                                              uint32_t claim_priority)
{
    if (map == NULL || map->evidence_count >= REFMEM_SLOT_CLAIM_EVIDENCE_MAX) {
        return;
    }

    refmem_slot_claim_evidence_t *evidence = &map->evidence[map->evidence_count];
    evidence->evidence_id = map->evidence_count;
    evidence->candidate_id = candidate_id;
    evidence->slot_id = slot_id;
    evidence->board_id = board != NULL ? board->board_id : UINT32_MAX;
    evidence->board_uuid_crc32 = board != NULL ? board->board_uuid_crc32 : 0u;
    evidence->preferred_slot_id = board != NULL ? board->active_default_slot : UINT32_MAX;
    evidence->claim_state = claim_state;
    evidence->reason = reason;
    evidence->claim_policy = claim_policy;
    evidence->claim_priority = claim_priority;
    evidence->claim_epoch = map->claim_epoch;
    evidence->evidence_crc32 =
        refmem_slot_claim_crc32_update(0xFFFFFFFFu,
                                       evidence,
                                       sizeof(*evidence) -
                                           sizeof(evidence->evidence_crc32)) ^
        0xFFFFFFFFu;
    map->evidence_count++;
}

static bool refmem_slot_claim_add_candidate(refmem_slot_claim_map_t *map,
                                            const refmem_app_node_entry_t *node,
                                            const refmem_board_capability_entry_t *board,
                                            uint32_t candidate_id,
                                            uint32_t target_slot,
                                            const refmem_node_load_table_t *node_load_table,
                                            const refmem_fb_instance_table_t *instance_table)
{
    if (map == NULL || node == NULL || board == NULL) {
        return false;
    }

    if (candidate_id >= REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX) {
        map->overflow_count++;
        refmem_slot_claim_record_evidence(map,
                                          board,
                                          candidate_id,
                                          UINT32_MAX,
                                          REFMEM_SLOT_CLAIM_OVERFLOW,
                                          REFMEM_SLOT_CLAIM_REASON_OVERFLOW,
                                          0u,
                                          0u);
        return true;
    }

    map->candidate_count++;
    if (target_slot >= map->slot_count ||
        target_slot >= REFMEM_APP_MODEL_NODE_COUNT) {
        map->overflow_count++;
        return true;
    }

    refmem_slot_claim_assignment_t *slot = &map->slot[target_slot];
    if (slot->claim_state == REFMEM_SLOT_CLAIM_DISABLED) {
        slot->reason = REFMEM_SLOT_CLAIM_REASON_DISABLED_SLOT;
        refmem_slot_claim_record_evidence(map,
                                          board,
                                          candidate_id,
                                          target_slot,
                                          REFMEM_SLOT_CLAIM_DISABLED,
                                          REFMEM_SLOT_CLAIM_REASON_DISABLED_SLOT,
                                          slot->claim_policy,
                                          slot->claim_priority);
        return true;
    }

    if (slot->claim_count != 0u) {
        slot->claim_count++;
        slot->claim_state = REFMEM_SLOT_CLAIM_CONFLICT;
        slot->reason = REFMEM_SLOT_CLAIM_REASON_DUPLICATE_SLOT;
        map->conflict_count++;
        refmem_slot_claim_record_evidence(map,
                                          board,
                                          candidate_id,
                                          slot->slot_id,
                                          REFMEM_SLOT_CLAIM_CONFLICT,
                                          REFMEM_SLOT_CLAIM_REASON_DUPLICATE_SLOT,
                                          slot->claim_policy,
                                          slot->claim_priority);
        return true;
    }

    slot->board_id = board->board_id;
    slot->board_uuid_crc32 = board->board_uuid_crc32;
    slot->capability_mask = board->capability_mask;
    slot->io_constraint_mask = board->io_constraint_mask;
    slot->ip_core_mask = board->ip_core_mask;
    slot->loaded_instance_mask =
        refmem_slot_claim_loaded_instance_mask(slot->slot_id, node_load_table, instance_table);
    slot->claim_count = 1u;
    slot->claim_state = REFMEM_SLOT_CLAIM_CLAIMED;
    slot->reason = REFMEM_SLOT_CLAIM_REASON_OK;
    slot->claim_policy = node->claim_policy;
    slot->claim_priority = node->claim_priority;
    slot->online_required = node->online_required;
    slot->last_claim_seq = candidate_id + 1u;
    map->assigned_count++;

    if (node->claim_policy == REFMEM_APP_CLAIM_STRICT_UUID &&
        board->board_uuid_crc32 == 0u) {
        slot->claim_state = REFMEM_SLOT_CLAIM_MISMATCH;
        slot->reason = REFMEM_SLOT_CLAIM_REASON_UUID_MISMATCH;
        map->conflict_count++;
        refmem_slot_claim_record_evidence(map,
                                          board,
                                          candidate_id,
                                          slot->slot_id,
                                          REFMEM_SLOT_CLAIM_MISMATCH,
                                          REFMEM_SLOT_CLAIM_REASON_UUID_MISMATCH,
                                          slot->claim_policy,
                                          slot->claim_priority);
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
        slot->online_required = node->online_required;
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
        if (map->candidate_count >= map->slot_count) {
            map->candidate_count++;
            map->overflow_count++;
            refmem_slot_claim_record_evidence(map,
                                              board,
                                              i,
                                              UINT32_MAX,
                                              REFMEM_SLOT_CLAIM_OVERFLOW,
                                              REFMEM_SLOT_CLAIM_REASON_OVERFLOW,
                                              0u,
                                              0u);
            continue;
        }
        if (board->active_default_slot >= node_table->node_count) {
            map->candidate_count++;
            map->overflow_count++;
            refmem_slot_claim_record_evidence(map,
                                              board,
                                              i,
                                              UINT32_MAX,
                                              REFMEM_SLOT_CLAIM_OVERFLOW,
                                              REFMEM_SLOT_CLAIM_REASON_BAD_SLOT,
                                              0u,
                                              0u);
            continue;
        }
        const refmem_app_node_entry_t *node = &node_table->node[board->active_default_slot];
        (void)refmem_slot_claim_add_candidate(map,
                                              node,
                                              board,
                                              i,
                                              board->active_default_slot,
                                              node_load_table,
                                              instance_table);
    }

    for (uint32_t i = 0u; i < map->slot_count; i++) {
        refmem_slot_claim_assignment_t *slot = &map->slot[i];
        slot->claim_crc32 = refmem_slot_claim_assignment_crc32(slot);
    }
    map->map_crc32 = refmem_slot_claim_map_crc32(map);
    return true;
}

static const refmem_board_capability_entry_t *refmem_slot_claim_board_by_id(
    const refmem_board_capability_table_t *boards, uint32_t board_id)
{
    for (uint32_t i = 0u; i < boards->board_count; ++i) {
        if (boards->board[i].board_id == board_id)
            return &boards->board[i];
    }
    return NULL;
}

static bool refmem_slot_claim_proposal_tables_valid(
    const refmem_generic_node_table_t *nodes,
    const refmem_board_capability_table_t *boards,
    const refmem_node_load_table_t *loads,
    const refmem_fb_instance_table_t *instances)
{
    if (nodes == NULL || boards == NULL || loads == NULL || instances == NULL ||
        nodes->version != REFMEM_APP_MODEL_VERSION ||
        boards->version != REFMEM_APP_MODEL_VERSION ||
        loads->version != REFMEM_APP_MODEL_VERSION ||
        instances->version != REFMEM_APP_MODEL_VERSION ||
        nodes->node_count == 0u || nodes->node_count > REFMEM_APP_MODEL_NODE_COUNT ||
        boards->board_count > REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT ||
        loads->load_count > REFMEM_APP_MODEL_NODE_LOAD_COUNT ||
        instances->instance_count > REFMEM_APP_MODEL_INSTANCE_COUNT)
        return false;
    for (uint32_t i = 0u; i < nodes->node_count; ++i) {
        if (nodes->node[i].node_id != i ||
            nodes->node[i].claim_policy > REFMEM_APP_CLAIM_DISABLED ||
            nodes->node[i].online_required > 1u)
            return false;
    }
    for (uint32_t i = 0u; i < boards->board_count; ++i) {
        const refmem_board_capability_entry_t *board = &boards->board[i];
        if (board->board_id >= REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT ||
            board->active_default_slot >= nodes->node_count || board->online_required > 1u)
            return false;
        for (uint32_t j = 0u; j < i; ++j) {
            if (boards->board[j].board_id == board->board_id ||
                (board->board_uuid_crc32 != 0u &&
                 boards->board[j].board_uuid_crc32 == board->board_uuid_crc32))
                return false;
        }
    }
    for (uint32_t i = 0u; i < instances->instance_count; ++i) {
        if (instances->instance[i].instance_id >= REFMEM_APP_MODEL_INSTANCE_COUNT ||
            instances->instance[i].instance_id >= 32u ||
            instances->instance[i].enable_condition > 1u)
            return false;
        for (uint32_t j = 0u; j < i; ++j) {
            if (instances->instance[j].instance_id == instances->instance[i].instance_id)
                return false;
        }
    }
    uint32_t loaded = 0u;
    for (uint32_t i = 0u; i < loads->load_count; ++i) {
        const refmem_node_load_entry_t *load = &loads->load[i];
        if (load->node_id >= nodes->node_count ||
            load->instance_id >= REFMEM_APP_MODEL_INSTANCE_COUNT || load->instance_id >= 32u ||
            load->enabled > 1u || load->required > 1u)
            return false;
        bool found = false;
        for (uint32_t j = 0u; j < instances->instance_count; ++j)
            found |= instances->instance[j].instance_id == load->instance_id;
        if (!found)
            return false;
        if (load->enabled != 0u) {
            const uint32_t bit = 1u << load->instance_id;
            if ((loaded & bit) != 0u)
                return false;
            loaded |= bit;
        }
    }
    return true;
}

bool refmem_slot_claim_derive_proposals(
    const refmem_generic_node_table_t *node_table,
    const refmem_board_capability_table_t *board_table,
    const refmem_node_load_table_t *node_load_table,
    const refmem_fb_instance_table_t *instance_table,
    const refmem_slot_binding_proposal_t *proposals,
    uint32_t proposal_count,
    uint32_t claim_epoch,
    refmem_slot_claim_map_t *map)
{
    if (map == NULL || claim_epoch == 0u ||
        proposal_count > REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX ||
        (proposal_count != 0u && proposals == NULL) ||
        !refmem_slot_claim_proposal_tables_valid(node_table, board_table,
                                                node_load_table, instance_table))
        return false;
    for (uint32_t i = 0u; i < proposal_count; ++i) {
        const refmem_slot_binding_proposal_t *p = &proposals[i];
        if (p->slot_id >= node_table->node_count ||
            refmem_slot_claim_board_by_id(board_table, p->board_id) == NULL)
            return false;
        for (uint32_t j = 0u; j < i; ++j) {
            if (proposals[j].board_id == p->board_id && proposals[j].slot_id != p->slot_id &&
                (node_table->node[p->slot_id].claim_policy != REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT ||
                 node_table->node[proposals[j].slot_id].claim_policy != REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT))
                return false;
        }
    }

    refmem_slot_claim_map_t candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.version = REFMEM_SLOT_CLAIM_VERSION;
    candidate.claim_epoch = claim_epoch;
    candidate.slot_count = node_table->node_count;
    for (uint32_t i = 0u; i < candidate.slot_count; ++i) {
        refmem_slot_claim_assignment_t *slot = &candidate.slot[i];
        const refmem_app_node_entry_t *node = &node_table->node[i];
        slot->slot_id = i;
        slot->board_id = UINT32_MAX;
        slot->claim_epoch = claim_epoch;
        slot->claim_policy = node->claim_policy;
        slot->claim_priority = node->claim_priority;
        slot->online_required = node->online_required;
        if (node->claim_policy == REFMEM_APP_CLAIM_DISABLED) {
            slot->claim_state = REFMEM_SLOT_CLAIM_DISABLED;
            slot->reason = REFMEM_SLOT_CLAIM_REASON_DISABLED_SLOT;
            candidate.disabled_count++;
        }
    }
    for (uint32_t i = 0u; i < proposal_count; ++i) {
        const refmem_slot_binding_proposal_t *p = &proposals[i];
        const refmem_app_node_entry_t *node = &node_table->node[p->slot_id];
        const refmem_board_capability_entry_t *board =
            refmem_slot_claim_board_by_id(board_table, p->board_id);
        (void)refmem_slot_claim_add_candidate(&candidate, node, board, i, p->slot_id,
                                              node_load_table, instance_table);
        refmem_slot_claim_assignment_t *slot = &candidate.slot[p->slot_id];
        if (slot->claim_state != REFMEM_SLOT_CLAIM_CLAIMED)
            continue;
        uint32_t reason = REFMEM_SLOT_CLAIM_REASON_OK;
        if (node->claim_policy == REFMEM_APP_CLAIM_STRICT_UUID &&
            node->node_uuid_crc32 != 0u && node->node_uuid_crc32 != board->board_uuid_crc32)
            reason = REFMEM_SLOT_CLAIM_REASON_UUID_MISMATCH;
        else if (node->hw_profile_crc32 != 0u && node->hw_profile_crc32 != board->hw_profile_crc32)
            reason = REFMEM_SLOT_CLAIM_REASON_HW_PROFILE_MISMATCH;
        else if ((node->capability_mask & ~board->capability_mask) != 0u)
            reason = REFMEM_SLOT_CLAIM_REASON_CAPABILITY_MISMATCH;
        if (reason != REFMEM_SLOT_CLAIM_REASON_OK) {
            slot->claim_state = REFMEM_SLOT_CLAIM_MISMATCH;
            slot->reason = reason;
            candidate.conflict_count++;
        }
        refmem_slot_claim_record_evidence(&candidate, board, i, p->slot_id,
                                          slot->claim_state, slot->reason,
                                          slot->claim_policy, slot->claim_priority);
    }
    for (uint32_t i = 0u; i < candidate.slot_count; ++i)
        candidate.slot[i].claim_crc32 = refmem_slot_claim_assignment_crc32(&candidate.slot[i]);
    candidate.map_crc32 = refmem_slot_claim_map_crc32(&candidate);
    *map = candidate;
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

const refmem_slot_claim_evidence_t *refmem_slot_claim_find_evidence(
    const refmem_slot_claim_map_t *map,
    uint32_t evidence_id)
{
    if (map == NULL || evidence_id >= map->evidence_count ||
        evidence_id >= REFMEM_SLOT_CLAIM_EVIDENCE_MAX) {
        return NULL;
    }
    return &map->evidence[evidence_id];
}

bool refmem_slot_claim_gate_evaluate(const refmem_slot_claim_map_t *map,
                                     refmem_slot_claim_gate_status_t *status)
{
    if (status == NULL) {
        return false;
    }

    memset(status, 0, sizeof(*status));
    status->version = REFMEM_SLOT_CLAIM_VERSION;
    status->ready = 1u;
    status->first_bad_slot = UINT32_MAX;

    if (map == NULL || map->version != REFMEM_SLOT_CLAIM_VERSION ||
        map->slot_count > REFMEM_APP_MODEL_NODE_COUNT) {
        status->ready = 0u;
        status->first_reason = REFMEM_SLOT_CLAIM_REASON_BAD_ARGUMENT;
        return false;
    }

    status->overflow_count = map->overflow_count;
    status->map_crc32 = map->map_crc32;
    if (map->map_crc32 != refmem_slot_claim_map_crc32(map)) {
        status->ready = 0u;
        status->first_reason = REFMEM_SLOT_CLAIM_REASON_MAP_CRC;
    }
    if (map->overflow_count != 0u) {
        status->ready = 0u;
        if (status->first_reason == REFMEM_SLOT_CLAIM_REASON_OK) {
            status->first_reason = REFMEM_SLOT_CLAIM_REASON_OVERFLOW;
        }
    }

    for (uint32_t i = 0u; i < map->slot_count; i++) {
        const refmem_slot_claim_assignment_t *slot = &map->slot[i];
        bool slot_ok = true;
        uint32_t slot_reason = slot->reason;

        if (slot->claim_crc32 != refmem_slot_claim_assignment_crc32(slot)) {
            status->mismatch_count++;
            slot_ok = false;
            slot_reason = REFMEM_SLOT_CLAIM_REASON_CLAIM_CRC;
        } else if (slot->claim_state == REFMEM_SLOT_CLAIM_STALE ||
                   ((slot->claim_state == REFMEM_SLOT_CLAIM_CLAIMED ||
                     slot->claim_state == REFMEM_SLOT_CLAIM_CONFLICT ||
                     slot->claim_state == REFMEM_SLOT_CLAIM_MISMATCH) &&
                    slot->claim_epoch != map->claim_epoch)) {
            status->mismatch_count++;
            slot_ok = false;
            slot_reason = REFMEM_SLOT_CLAIM_REASON_STALE;
        } else if (slot->claim_state == REFMEM_SLOT_CLAIM_CONFLICT) {
            status->conflict_count++;
            slot_ok = false;
        } else if (slot->claim_state == REFMEM_SLOT_CLAIM_MISMATCH) {
            status->mismatch_count++;
            slot_ok = false;
        } else if (slot->online_required != 0u &&
                   slot->claim_state != REFMEM_SLOT_CLAIM_CLAIMED) {
            status->required_missing_count++;
            slot_ok = false;
        }

        if (!slot_ok) {
            status->ready = 0u;
            if (status->first_bad_slot == UINT32_MAX) {
                status->first_bad_slot = slot->slot_id;
                status->first_reason = slot_reason;
            }
        }
    }

    if (status->ready != 0u) {
        status->first_reason = REFMEM_SLOT_CLAIM_REASON_OK;
    } else if (status->first_bad_slot == UINT32_MAX) {
        status->first_bad_slot = 0u;
    }
    return status->ready != 0u;
}
