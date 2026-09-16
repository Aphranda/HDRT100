#include "refmem_slot_claim.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Host CRC backend; the production claim serializer and gate are linked whole. */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

typedef struct {
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_binding_proposal_t proposals[REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX];
    uint32_t count;
    uint32_t epoch;
} fixture_t;

static fixture_t fixture(void)
{
    fixture_t f = {0};
    f.nodes.version = f.boards.version = f.loads.version = f.instances.version = REFMEM_APP_MODEL_VERSION;
    f.nodes.node_count = REFMEM_APP_MODEL_NODE_COUNT;
    f.boards.board_count = 2u;
    f.loads.load_count = f.instances.instance_count = 2u;
    f.count = 2u;
    f.epoch = 17u;
    for (uint32_t i = 0u; i < f.nodes.node_count; ++i) {
        f.nodes.node[i].node_id = i;
        f.nodes.node[i].capability_mask = REFMEM_APP_CAP_BASELINE;
        f.nodes.node[i].claim_policy = REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT;
        f.nodes.node[i].online_required = i < 2u;
    }
    for (uint32_t i = 0u; i < f.boards.board_count; ++i) {
        f.boards.board[i].board_id = i;
        f.boards.board[i].board_uuid_crc32 = 100u + i;
        f.boards.board[i].active_default_slot = i;
        f.boards.board[i].capability_mask = REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_SMA_IN | REFMEM_APP_CAP_SMA_OUT;
        f.boards.board[i].io_constraint_mask = REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT;
        f.boards.board[i].ip_core_mask = REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE;
    }
    /* Independent IDs, reordered table and a physical board with two slots. */
    f.loads.load[0] = (refmem_node_load_entry_t){ .node_id = 0u, .instance_id = 5u, .enabled = 1u };
    f.loads.load[1] = (refmem_node_load_entry_t){ .node_id = 1u, .instance_id = 7u, .enabled = 1u };
    f.instances.instance[0] = (refmem_fb_instance_entry_t){ .instance_id = 7u, .enable_condition = 1u };
    f.instances.instance[1] = (refmem_fb_instance_entry_t){ .instance_id = 5u, .enable_condition = 1u };
    f.proposals[0] = (refmem_slot_binding_proposal_t){ .slot_id = 0u, .board_id = 1u };
    f.proposals[1] = (refmem_slot_binding_proposal_t){ .slot_id = 1u, .board_id = 1u };
    return f;
}

static bool derive(fixture_t *f, refmem_slot_claim_map_t *map)
{
    return refmem_slot_claim_derive_proposals(&f->nodes, &f->boards, &f->loads,
                                             &f->instances, f->proposals,
                                             f->count, f->epoch, map);
}

static void rejected(fixture_t *f)
{
    refmem_slot_claim_map_t map, before;
    memset(&map, 0xa5, sizeof(map));
    memcpy(&before, &map, sizeof(map));
    assert(!derive(f, &map));
    assert(memcmp(&map, &before, sizeof(map)) == 0);
}

#define REJECT(change) do { fixture_t f = fixture(); change; rejected(&f); } while (0)

static void valid_multi_slot_and_crc(void)
{
    fixture_t f = fixture();
    fixture_t original = f;
    refmem_slot_claim_map_t map, repeated, newer;
    refmem_slot_claim_gate_status_t gate;
    assert(derive(&f, &map));
    assert(memcmp(&f, &original, sizeof(f)) == 0);
    assert(refmem_slot_claim_gate_evaluate(&map, &gate));
    assert(map.claim_epoch == 17u && map.assigned_count == 2u && map.candidate_count == 2u);
    assert(map.slot[0].board_id == 1u && map.slot[1].board_id == 1u);
    assert(map.slot[0].board_uuid_crc32 == 101u);
    assert(map.slot[0].capability_mask == f.boards.board[1].capability_mask);
    assert(map.slot[0].io_constraint_mask == f.boards.board[1].io_constraint_mask);
    assert(map.slot[0].ip_core_mask == f.boards.board[1].ip_core_mask);
    assert(map.slot[0].loaded_instance_mask == (1u << 5));
    assert(map.slot[1].loaded_instance_mask == (1u << 7));
    assert(map.evidence_count == f.count);
    for (uint32_t i = 0u; i < map.evidence_count; ++i) {
        const refmem_slot_claim_evidence_t *e = &map.evidence[i];
        assert(e->claim_epoch == 17u && e->candidate_id == i && e->board_id == 1u);
        assert(e->preferred_slot_id == 1u); /* No mutation of board's default. */
        assert(e->evidence_crc32 == (ota_crc32_update(UINT32_MAX, (const uint8_t *)e,
                                                    offsetof(refmem_slot_claim_evidence_t, evidence_crc32)) ^ UINT32_MAX));
    }
    assert(derive(&f, &repeated));
    assert(memcmp(&map, &repeated, sizeof(map)) == 0);
    f.epoch++;
    assert(derive(&f, &newer));
    assert(newer.map_crc32 != map.map_crc32);
    assert(newer.slot[0].claim_crc32 != map.slot[0].claim_crc32);
    newer.slot[0].board_id = 0u;
    assert(!refmem_slot_claim_gate_evaluate(&newer, &gate));
    assert(gate.first_reason == REFMEM_SLOT_CLAIM_REASON_CLAIM_CRC);
    f = fixture();
    f.instances.instance[0].enable_condition = 0u;
    assert(derive(&f, &map));
    assert(map.slot[1].loaded_instance_mask == 0u);
    /* Legacy default-slot behavior and constant epoch remain intact. */
    assert(refmem_slot_claim_derive_map(&f.nodes, &f.boards, &f.loads, &f.instances, &map));
    assert(map.claim_epoch == 1u && map.slot[0].board_id == 0u && map.slot[1].board_id == 1u);
    assert(map.slot[0].loaded_instance_mask == (1u << 5));
}

static void conflict_disabled_missing(void)
{
    fixture_t f = fixture();
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;
    f.proposals[1] = (refmem_slot_binding_proposal_t){ .slot_id = 0u, .board_id = 0u };
    assert(derive(&f, &map));
    assert(map.slot[0].claim_state == REFMEM_SLOT_CLAIM_CONFLICT);
    assert(map.slot[0].board_id == 1u && map.slot[0].claim_count == 2u);
    assert(map.evidence[1].board_id == 0u && map.evidence[1].claim_state == REFMEM_SLOT_CLAIM_CONFLICT);
    assert(!refmem_slot_claim_gate_evaluate(&map, &gate));
    f.proposals[1] = f.proposals[0]; /* Repeated identical proposal is also a conflict. */
    assert(derive(&f, &map));
    assert(map.slot[0].claim_state == REFMEM_SLOT_CLAIM_CONFLICT);
    f = fixture();
    f.count = 1u;
    f.nodes.node[0].claim_policy = REFMEM_APP_CLAIM_DISABLED;
    assert(derive(&f, &map));
    assert(map.slot[0].claim_state == REFMEM_SLOT_CLAIM_DISABLED);
    assert(map.slot[0].board_id == UINT32_MAX && map.slot[0].claim_count == 0u);
    assert(map.evidence[0].reason == REFMEM_SLOT_CLAIM_REASON_DISABLED_SLOT);
    assert(!refmem_slot_claim_gate_evaluate(&map, &gate));
    f = fixture();
    f.count = 0u;
    assert(refmem_slot_claim_derive_proposals(&f.nodes, &f.boards, &f.loads, &f.instances,
                                             NULL, 0u, f.epoch, &map));
    assert(map.candidate_count == 0u && map.slot[0].claim_state == REFMEM_SLOT_CLAIM_UNCLAIMED);
    assert(!refmem_slot_claim_gate_evaluate(&map, &gate));
    assert(gate.required_missing_count == 2u);
    f.nodes.node[0].online_required = f.nodes.node[1].online_required = 0u;
    assert(derive(&f, &map));
    assert(refmem_slot_claim_gate_evaluate(&map, &gate));
    f = fixture();
    f.count = REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX;
    f.epoch = UINT32_MAX;
    for (uint32_t i = 1u; i < f.count; ++i)
        f.proposals[i] = f.proposals[0];
    assert(derive(&f, &map));
    assert(map.candidate_count == f.count && map.evidence_count == f.count);
    assert(map.claim_epoch == UINT32_MAX && map.slot[0].claim_state == REFMEM_SLOT_CLAIM_CONFLICT);
    assert(map.slot[0].claim_count == f.count && map.assigned_count == 1u);
    assert(!refmem_slot_claim_gate_evaluate(&map, &gate));
}

static void identity_and_capability(void)
{
    fixture_t f = fixture();
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;
    f.count = 1u;
    f.nodes.node[0].claim_policy = REFMEM_APP_CLAIM_STRICT_UUID;
    f.nodes.node[0].node_uuid_crc32 = 999u;
    assert(derive(&f, &map));
    assert(map.slot[0].reason == REFMEM_SLOT_CLAIM_REASON_UUID_MISMATCH);
    assert(!refmem_slot_claim_gate_evaluate(&map, &gate));
    f.nodes.node[0].node_uuid_crc32 = 101u;
    f.nodes.node[0].hw_profile_crc32 = 77u;
    assert(derive(&f, &map));
    assert(map.slot[0].reason == REFMEM_SLOT_CLAIM_REASON_HW_PROFILE_MISMATCH);
    f.nodes.node[0].hw_profile_crc32 = 0u;
    f.nodes.node[0].capability_mask |= REFMEM_APP_CAP_LINK_CONTROL;
    assert(derive(&f, &map));
    assert(map.slot[0].reason == REFMEM_SLOT_CLAIM_REASON_CAPABILITY_MISMATCH);
    assert(map.slot[0].claim_state == REFMEM_SLOT_CLAIM_MISMATCH);
    /* A later conflicting board must not replace the mismatching first owner. */
    f.proposals[1] = (refmem_slot_binding_proposal_t){ .slot_id = 0u, .board_id = 0u };
    f.count = 2u;
    assert(derive(&f, &map));
    assert(map.slot[0].board_id == 1u && map.slot[0].claim_state == REFMEM_SLOT_CLAIM_CONFLICT);
}

static void rejection_atomicity(void)
{
    REJECT(f.epoch = 0u);
    REJECT(f.count = REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX + 1u);
    REJECT(f.proposals[0].slot_id = f.nodes.node_count);
    REJECT(f.proposals[0].board_id = UINT32_MAX);
    REJECT(f.nodes.node[0].claim_policy = REFMEM_APP_CLAIM_STRICT_UUID);
    REJECT(f.nodes.node[1].claim_policy = REFMEM_APP_CLAIM_SPARE_DYNAMIC);
    REJECT(f.nodes.version++);
    REJECT(f.boards.version++);
    REJECT(f.loads.version++);
    REJECT(f.instances.version++);
    REJECT(f.nodes.node_count = REFMEM_APP_MODEL_NODE_COUNT + 1u);
    REJECT(f.nodes.node_count = 0u);
    REJECT(f.boards.board_count = REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT + 1u);
    REJECT(f.loads.load_count = REFMEM_APP_MODEL_NODE_LOAD_COUNT + 1u);
    REJECT(f.instances.instance_count = REFMEM_APP_MODEL_INSTANCE_COUNT + 1u);
    REJECT(f.nodes.node[0].node_id = 1u);
    REJECT(f.boards.board[0].board_id = 1u);
    REJECT(f.boards.board[0].board_uuid_crc32 = 101u);
    REJECT(f.boards.board[0].active_default_slot = UINT32_MAX);
    REJECT(f.instances.instance[0].instance_id = 5u);
    REJECT(f.instances.instance[0].instance_id = 32u);
    REJECT(f.instances.instance[0].enable_condition = 2u);
    REJECT(f.loads.load[0].instance_id = 9u);
    REJECT(f.loads.load[0].instance_id = 7u);
    REJECT(f.loads.load[0].node_id = UINT32_MAX);
    REJECT(f.loads.load[0].enabled = 2u);
    fixture_t f = fixture();
    refmem_slot_claim_map_t map, before;
    memset(&map, 0xa5, sizeof(map));
    memcpy(&before, &map, sizeof(map));
    assert(!refmem_slot_claim_derive_proposals(NULL, &f.boards, &f.loads, &f.instances, f.proposals, f.count, f.epoch, &map));
    assert(!refmem_slot_claim_derive_proposals(&f.nodes, NULL, &f.loads, &f.instances, f.proposals, f.count, f.epoch, &map));
    assert(!refmem_slot_claim_derive_proposals(&f.nodes, &f.boards, NULL, &f.instances, f.proposals, f.count, f.epoch, &map));
    assert(!refmem_slot_claim_derive_proposals(&f.nodes, &f.boards, &f.loads, NULL, f.proposals, f.count, f.epoch, &map));
    assert(!refmem_slot_claim_derive_proposals(&f.nodes, &f.boards, &f.loads, &f.instances, NULL, f.count, f.epoch, &map));
    assert(!refmem_slot_claim_derive_proposals(&f.nodes, &f.boards, &f.loads, &f.instances, f.proposals, f.count, f.epoch, NULL));
    assert(memcmp(&map, &before, sizeof(map)) == 0);
}

int main(void)
{
    valid_multi_slot_and_crc();
    conflict_disabled_missing();
    identity_and_capability();
    rejection_atomicity();
    puts("slot proposals: identity, epoch, conflicts, policies, CRC and rollback passed");
    return 0;
}
