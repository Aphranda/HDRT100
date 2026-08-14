#include "refmem_slot_claim.h"
#include "refmem_claim_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static uint32_t test_crc32_update(uint32_t crc, const void *data, size_t size)
{
    return ota_crc32_update(crc, (const uint8_t *)data, size);
}

static uint32_t test_assignment_crc32(const refmem_slot_claim_assignment_t *slot)
{
    if (slot == NULL) {
        return 0u;
    }
    return test_crc32_update(0xFFFFFFFFu,
                             slot,
                             sizeof(*slot) - sizeof(slot->claim_crc32)) ^
           0xFFFFFFFFu;
}

static uint32_t test_map_crc32(const refmem_slot_claim_map_t *map)
{
    uint32_t crc = 0xFFFFFFFFu;
    if (map == NULL) {
        return 0u;
    }

    crc = test_crc32_update(crc, &map->version, sizeof(map->version));
    crc = test_crc32_update(crc, &map->claim_epoch, sizeof(map->claim_epoch));
    crc = test_crc32_update(crc, &map->slot_count, sizeof(map->slot_count));
    crc = test_crc32_update(crc, &map->candidate_count, sizeof(map->candidate_count));
    crc = test_crc32_update(crc, &map->assigned_count, sizeof(map->assigned_count));
    crc = test_crc32_update(crc, &map->conflict_count, sizeof(map->conflict_count));
    crc = test_crc32_update(crc, &map->overflow_count, sizeof(map->overflow_count));
    crc = test_crc32_update(crc, &map->disabled_count, sizeof(map->disabled_count));
    crc = test_crc32_update(crc, &map->evidence_count, sizeof(map->evidence_count));
    for (uint32_t i = 0u; i < map->slot_count && i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        crc = test_crc32_update(crc, &map->slot[i], sizeof(map->slot[i]));
    }
    for (uint32_t i = 0u; i < map->evidence_count && i < REFMEM_SLOT_CLAIM_EVIDENCE_MAX; i++) {
        crc = test_crc32_update(crc, &map->evidence[i], sizeof(map->evidence[i]));
    }
    return crc ^ 0xFFFFFFFFu;
}

static void make_generic_nodes(refmem_generic_node_table_t *table, uint32_t node_count)
{
    (void)memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->node_count = node_count;
    for (uint32_t i = 0u; i < node_count; i++) {
        table->node[i].node_id = i;
        table->node[i].node_uuid_crc32 = 0xB0000000u + i;
        table->node[i].capability_mask =
            REFMEM_APP_CAP_BOARD | REFMEM_APP_CAP_REFMEM | REFMEM_APP_CAP_VDC |
            REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
        table->node[i].claim_policy = REFMEM_APP_CLAIM_STRICT_UUID;
        table->node[i].claim_priority = 100u - i;
        table->node[i].hw_profile_crc32 = 0x1000u + i;
        table->node[i].online_required = i < 4u ? 1u : 0u;
        table->node[i].fail_policy = table->node[i].online_required != 0u
                                         ? REFMEM_APP_FAIL_STOP
                                         : REFMEM_APP_FAIL_REPORT_ONLY;
    }
}

static void make_boards(refmem_board_capability_table_t *table, uint32_t board_count)
{
    (void)memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->board_count = board_count;
    for (uint32_t i = 0u; i < board_count; i++) {
        table->board[i].board_id = i;
        table->board[i].board_uuid_crc32 = 0xB0000000u + i;
        table->board[i].capability_mask =
            REFMEM_APP_CAP_BOARD | REFMEM_APP_CAP_REFMEM | REFMEM_APP_CAP_VDC |
            REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;
        table->board[i].hw_profile_crc32 = 0x1000u + i;
        table->board[i].active_default_slot = i;
        table->board[i].online_required = i < 4u ? 1u : 0u;
    }
}

static void make_loads(refmem_node_load_table_t *loads,
                       refmem_fb_instance_table_t *instances)
{
    (void)memset(loads, 0, sizeof(*loads));
    (void)memset(instances, 0, sizeof(*instances));
    loads->version = REFMEM_APP_MODEL_VERSION;
    loads->load_count = 2u;
    loads->load[0].load_id = 0u;
    loads->load[0].node_id = 0u;
    loads->load[0].instance_id = 0u;
    loads->load[0].enabled = 1u;
    loads->load[1].load_id = 1u;
    loads->load[1].node_id = 2u;
    loads->load[1].instance_id = 1u;
    loads->load[1].enabled = 1u;

    instances->version = REFMEM_APP_MODEL_VERSION;
    instances->instance_count = 2u;
    instances->instance[0].instance_id = 0u;
    instances->instance[0].enable_condition = 1u;
    instances->instance[1].instance_id = 1u;
    instances->instance[1].enable_condition = 1u;
}

static int derive_gate(const refmem_generic_node_table_t *nodes,
                       const refmem_board_capability_table_t *boards,
                       const refmem_node_load_table_t *loads,
                       const refmem_fb_instance_table_t *instances,
                       refmem_slot_claim_map_t *map,
                       refmem_slot_claim_gate_status_t *gate)
{
    int failed = 0;
    failed += expect_bool("derive map",
                          refmem_slot_claim_derive_map(nodes, boards, loads, instances, map),
                          true);
    const bool gate_result = refmem_slot_claim_gate_evaluate(map, gate);
    failed += expect_bool("evaluate gate", gate_result, gate->ready != 0u);
    return failed;
}

static int test_nominal_claim_map(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    make_boards(&boards, REFMEM_APP_MODEL_NODE_COUNT);
    make_loads(&loads, &instances);

    failed += derive_gate(&nodes, &boards, &loads, &instances, &map, &gate);
    failed += expect_u32("nominal ready", gate.ready, 1u);
    failed += expect_u32("nominal candidates", map.candidate_count, 8u);
    failed += expect_u32("nominal assigned", map.assigned_count, 8u);
    failed += expect_u32("nominal conflicts", map.conflict_count, 0u);
    failed += expect_u32("nominal overflows", map.overflow_count, 0u);
    failed += expect_u32("nominal evidence", map.evidence_count, 0u);
    failed += expect_u32("slot0 claimed", map.slot[0].claim_state, REFMEM_SLOT_CLAIM_CLAIMED);
    failed += expect_u32("slot0 load mask", map.slot[0].loaded_instance_mask, 0x01u);
    failed += expect_u32("slot2 load mask", map.slot[2].loaded_instance_mask, 0x02u);
    return failed;
}

static int test_duplicate_claim_blocks_gate(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    for (uint32_t i = 1u; i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        nodes.node[i].online_required = 0u;
    }
    make_boards(&boards, 2u);
    boards.board[1].active_default_slot = 0u;
    make_loads(&loads, &instances);

    (void)derive_gate(&nodes, &boards, &loads, &instances, &map, &gate);
    failed += expect_u32("duplicate ready", gate.ready, 0u);
    failed += expect_u32("duplicate state", map.slot[0].claim_state, REFMEM_SLOT_CLAIM_CONFLICT);
    failed += expect_u32("duplicate reason", map.slot[0].reason, REFMEM_SLOT_CLAIM_REASON_DUPLICATE_SLOT);
    failed += expect_u32("duplicate gate conflicts", gate.conflict_count, 1u);
    failed += expect_u32("duplicate first slot", gate.first_bad_slot, 0u);
    failed += expect_u32("duplicate evidence count", map.evidence_count, 1u);
    failed += expect_u32("duplicate evidence reason",
                         map.evidence[0].reason,
                         REFMEM_SLOT_CLAIM_REASON_DUPLICATE_SLOT);
    return failed;
}

static int test_missing_uuid_blocks_strict_claim(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    for (uint32_t i = 1u; i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        nodes.node[i].online_required = 0u;
    }
    make_boards(&boards, 1u);
    boards.board[0].board_uuid_crc32 = 0u;
    make_loads(&loads, &instances);

    (void)derive_gate(&nodes, &boards, &loads, &instances, &map, &gate);
    failed += expect_u32("missing uuid ready", gate.ready, 0u);
    failed += expect_u32("missing uuid state", map.slot[0].claim_state, REFMEM_SLOT_CLAIM_MISMATCH);
    failed += expect_u32("missing uuid reason", map.slot[0].reason, REFMEM_SLOT_CLAIM_REASON_UUID_MISMATCH);
    failed += expect_u32("missing uuid gate count", gate.mismatch_count, 1u);
    failed += expect_u32("missing uuid evidence count", map.evidence_count, 1u);
    failed += expect_u32("missing uuid evidence board", map.evidence[0].board_id, 0u);
    return failed;
}

static int test_board_identity_does_not_bind_slot(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        nodes.node[i].online_required = 0u;
    }
    make_boards(&boards, 1u);
    boards.board[0].board_uuid_crc32 = nodes.node[0].node_uuid_crc32;
    boards.board[0].hw_profile_crc32 = 0xCAFEBABEu;
    boards.board[0].active_default_slot = 1u;
    make_loads(&loads, &instances);

    (void)derive_gate(&nodes, &boards, &loads, &instances, &map, &gate);
    failed += expect_u32("arbitrary slot ready", gate.ready, 1u);
    failed += expect_u32("arbitrary slot assigned", map.assigned_count, 1u);
    failed += expect_u32("arbitrary slot evidence", map.evidence_count, 0u);
    failed += expect_u32("arbitrary slot0 state", map.slot[0].claim_state, REFMEM_SLOT_CLAIM_UNCLAIMED);
    failed += expect_u32("arbitrary slot1 board", map.slot[1].board_id, 0u);
    failed += expect_u32("arbitrary slot1 state", map.slot[1].claim_state, REFMEM_SLOT_CLAIM_CLAIMED);
    failed += expect_u32("arbitrary slot1 reason", map.slot[1].reason, REFMEM_SLOT_CLAIM_REASON_OK);
    failed += expect_u32("arbitrary slot1 caps", map.slot[1].capability_mask, boards.board[0].capability_mask);
    return failed;
}

static int test_candidate_overflow_blocks_gate(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    make_boards(&boards, 9u);
    boards.board[8].active_default_slot = 0u;
    make_loads(&loads, &instances);

    (void)derive_gate(&nodes, &boards, &loads, &instances, &map, &gate);
    failed += expect_u32("overflow ready", gate.ready, 0u);
    failed += expect_u32("overflow candidates", map.candidate_count, 9u);
    failed += expect_u32("overflow assigned", map.assigned_count, 8u);
    failed += expect_u32("overflow count", map.overflow_count, 1u);
    failed += expect_u32("overflow reason", gate.first_reason, REFMEM_SLOT_CLAIM_REASON_OVERFLOW);
    failed += expect_u32("overflow evidence count", map.evidence_count, 1u);
    failed += expect_u32("overflow evidence candidate", map.evidence[0].candidate_id, 8u);
    return failed;
}

static int test_stale_claim_blocks_gate(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    make_boards(&boards, REFMEM_APP_MODEL_NODE_COUNT);
    make_loads(&loads, &instances);
    (void)refmem_slot_claim_derive_map(&nodes, &boards, &loads, &instances, &map);

    map.slot[0].claim_epoch = map.claim_epoch + 1u;
    map.slot[0].claim_crc32 = test_assignment_crc32(&map.slot[0]);
    map.map_crc32 = test_map_crc32(&map);
    (void)refmem_slot_claim_gate_evaluate(&map, &gate);
    failed += expect_u32("stale ready", gate.ready, 0u);
    failed += expect_u32("stale slot", gate.first_bad_slot, 0u);
    failed += expect_u32("stale reason", gate.first_reason, REFMEM_SLOT_CLAIM_REASON_STALE);
    failed += expect_u32("stale mismatch count", gate.mismatch_count, 1u);
    return failed;
}

static int test_claim_crc_blocks_gate(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    make_boards(&boards, REFMEM_APP_MODEL_NODE_COUNT);
    make_loads(&loads, &instances);
    (void)refmem_slot_claim_derive_map(&nodes, &boards, &loads, &instances, &map);

    map.slot[0].claim_crc32 ^= 0x55AA55AAu;
    map.map_crc32 = test_map_crc32(&map);
    (void)refmem_slot_claim_gate_evaluate(&map, &gate);
    failed += expect_u32("claim crc ready", gate.ready, 0u);
    failed += expect_u32("claim crc slot", gate.first_bad_slot, 0u);
    failed += expect_u32("claim crc reason",
                         gate.first_reason,
                         REFMEM_SLOT_CLAIM_REASON_CLAIM_CRC);
    failed += expect_u32("claim crc mismatch count", gate.mismatch_count, 1u);
    return failed;
}

static int test_map_crc_blocks_gate(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t map;
    refmem_slot_claim_gate_status_t gate;

    make_generic_nodes(&nodes, REFMEM_APP_MODEL_NODE_COUNT);
    make_boards(&boards, REFMEM_APP_MODEL_NODE_COUNT);
    make_loads(&loads, &instances);
    (void)refmem_slot_claim_derive_map(&nodes, &boards, &loads, &instances, &map);

    map.map_crc32 ^= 0x01020304u;
    (void)refmem_slot_claim_gate_evaluate(&map, &gate);
    failed += expect_u32("map crc ready", gate.ready, 0u);
    failed += expect_u32("map crc reason", gate.first_reason, REFMEM_SLOT_CLAIM_REASON_MAP_CRC);
    return failed;
}

static int test_claim_propose_frame_crc_validation(void)
{
    int failed = 0;
    refmem_slot_claim_proposal_t proposal[2];
    refmem_claim_propose_frame_t frame;

    (void)memset(proposal, 0, sizeof(proposal));
    proposal[0].candidate_id = 0u;
    proposal[0].board_id = 0u;
    proposal[0].board_uuid_crc32 = 0xB0000000u;
    proposal[0].preferred_slot_id = 0u;
    proposal[1].candidate_id = 1u;
    proposal[1].board_id = 1u;
    proposal[1].board_uuid_crc32 = 0xB0000001u;
    proposal[1].preferred_slot_id = 1u;

    failed += expect_bool("claim propose init",
                          refmem_claim_propose_frame_init(&frame,
                                                          2u,
                                                          100u,
                                                          0u,
                                                          0xB0000000u,
                                                          proposal,
                                                          2u),
                          true);
    failed += expect_u32("claim propose validate",
                         (uint32_t)refmem_claim_propose_frame_validate(&frame),
                         REFMEM_CLAIM_FRAME_OK);

    frame.proposal[1].preferred_slot_id = 7u;
    failed += expect_u32("claim propose payload crc detects mutation",
                         (uint32_t)refmem_claim_propose_frame_validate(&frame),
                         REFMEM_CLAIM_FRAME_BAD_PAYLOAD_CRC);

    frame.proposal[1].preferred_slot_id = 1u;
    (void)refmem_claim_propose_frame_init(&frame, 2u, 100u, 0u, 0xB0000000u, proposal, 2u);
    frame.header.claim_seq = 101u;
    failed += expect_u32("claim propose header crc detects mutation",
                         (uint32_t)refmem_claim_propose_frame_validate(&frame),
                         REFMEM_CLAIM_FRAME_BAD_HEADER_CRC);

    failed += expect_bool("claim propose rejects too many",
                          refmem_claim_propose_frame_init(&frame,
                                                          2u,
                                                          100u,
                                                          0u,
                                                          0xB0000000u,
                                                          proposal,
                                                          REFMEM_CLAIM_FRAME_PROPOSAL_MAX + 1u),
                          false);
    return failed;
}

static int test_claim_hello_and_commit_frames(void)
{
    int failed = 0;
    refmem_claim_hello_payload_t hello;
    refmem_claim_hello_frame_t hello_frame;
    refmem_claim_commit_payload_t commit;
    refmem_claim_commit_frame_t commit_frame;

    (void)memset(&hello, 0, sizeof(hello));
    hello.board_id = 1u;
    hello.board_uuid_crc32 = 0xB0000001u;
    hello.capability_mask = REFMEM_APP_CAP_BOARD | REFMEM_APP_CAP_REFMEM |
                            REFMEM_APP_CAP_VDC;
    hello.active_slot_id = 1u;
    hello.baseline_ready = 1u;
    hello.vdc_ready = 1u;
    hello.claim_crc32 = 0x12345678u;

    failed += expect_bool("claim hello init",
                          refmem_claim_hello_frame_init(&hello_frame, 3u, 10u, &hello),
                          true);
    failed += expect_u32("claim hello validate",
                         (uint32_t)refmem_claim_hello_frame_validate(&hello_frame),
                         REFMEM_CLAIM_FRAME_OK);
    hello_frame.hello.vdc_ready = 0u;
    failed += expect_u32("claim hello payload crc detects mutation",
                         (uint32_t)refmem_claim_hello_frame_validate(&hello_frame),
                         REFMEM_CLAIM_FRAME_BAD_PAYLOAD_CRC);

    (void)memset(&commit, 0, sizeof(commit));
    commit.map_crc32 = 0xA5A55A5Au;
    commit.slot_count = 8u;
    commit.assigned_count = 8u;
    commit.committed_node_mask = 0xFFu;
    commit.gate_ready = 1u;

    failed += expect_bool("claim commit init",
                          refmem_claim_commit_frame_init(&commit_frame,
                                                         3u,
                                                         11u,
                                                         0u,
                                                         0xB0000000u,
                                                         &commit),
                          true);
    failed += expect_u32("claim commit validate",
                         (uint32_t)refmem_claim_commit_frame_validate(&commit_frame),
                         REFMEM_CLAIM_FRAME_OK);
    commit_frame.header.frame_type = REFMEM_CLAIM_FRAME_RESOLVE;
    failed += expect_u32("claim commit rejects wrong type",
                         (uint32_t)refmem_claim_commit_frame_validate(&commit_frame),
                         REFMEM_CLAIM_FRAME_BAD_TYPE);
    return failed;
}

static int test_claim_conflict_release_resolve_frames(void)
{
    int failed = 0;
    refmem_claim_resolution_entry_t entries[2];
    refmem_claim_resolution_frame_t resolution_frame;
    refmem_claim_release_payload_t release;
    refmem_claim_release_frame_t release_frame;

    (void)memset(entries, 0, sizeof(entries));
    entries[0].candidate_id = 0u;
    entries[0].slot_id = 0u;
    entries[0].board_id = 1u;
    entries[0].board_uuid_crc32 = 0xB0000001u;
    entries[0].assigned_slot_id = 5u;
    entries[0].claim_state = REFMEM_SLOT_CLAIM_CONFLICT;
    entries[0].reason = REFMEM_SLOT_CLAIM_REASON_DUPLICATE_SLOT;
    entries[0].evidence_id = 0u;
    entries[0].claim_crc32 = 0x11111111u;
    entries[1] = entries[0];
    entries[1].candidate_id = 1u;
    entries[1].slot_id = 2u;
    entries[1].assigned_slot_id = 6u;
    entries[1].evidence_id = 1u;

    failed += expect_bool("claim conflict init",
                          refmem_claim_conflict_frame_init(&resolution_frame,
                                                           4u,
                                                           20u,
                                                           0u,
                                                           0xB0000000u,
                                                           entries,
                                                           2u),
                          true);
    failed += expect_u32("claim conflict validate",
                         (uint32_t)refmem_claim_conflict_frame_validate(&resolution_frame),
                         REFMEM_CLAIM_FRAME_OK);
    resolution_frame.entry[1].assigned_slot_id = 7u;
    failed += expect_u32("claim conflict payload crc detects mutation",
                         (uint32_t)refmem_claim_conflict_frame_validate(&resolution_frame),
                         REFMEM_CLAIM_FRAME_BAD_PAYLOAD_CRC);

    failed += expect_bool("claim resolve init",
                          refmem_claim_resolve_frame_init(&resolution_frame,
                                                          4u,
                                                          21u,
                                                          0u,
                                                          0xB0000000u,
                                                          entries,
                                                          2u),
                          true);
    failed += expect_u32("claim resolve validate",
                         (uint32_t)refmem_claim_resolve_frame_validate(&resolution_frame),
                         REFMEM_CLAIM_FRAME_OK);
    failed += expect_u32("claim resolve rejects conflict validator",
                         (uint32_t)refmem_claim_conflict_frame_validate(&resolution_frame),
                         REFMEM_CLAIM_FRAME_BAD_TYPE);

    (void)memset(&release, 0, sizeof(release));
    release.slot_id = 6u;
    release.board_id = 1u;
    release.board_uuid_crc32 = 0xB0000001u;
    release.release_seq = 22u;
    release.claim_crc32 = 0x22222222u;
    failed += expect_bool("claim release init",
                          refmem_claim_release_frame_init(&release_frame,
                                                          4u,
                                                          22u,
                                                          1u,
                                                          0xB0000001u,
                                                          &release),
                          true);
    failed += expect_u32("claim release validate",
                         (uint32_t)refmem_claim_release_frame_validate(&release_frame),
                         REFMEM_CLAIM_FRAME_OK);
    release_frame.header.payload_count = 2u;
    failed += expect_u32("claim release rejects bad count",
                         (uint32_t)refmem_claim_release_frame_validate(&release_frame),
                         REFMEM_CLAIM_FRAME_BAD_COUNT);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_nominal_claim_map();
    failed += test_duplicate_claim_blocks_gate();
    failed += test_missing_uuid_blocks_strict_claim();
    failed += test_board_identity_does_not_bind_slot();
    failed += test_candidate_overflow_blocks_gate();
    failed += test_stale_claim_blocks_gate();
    failed += test_claim_crc_blocks_gate();
    failed += test_map_crc_blocks_gate();
    failed += test_claim_propose_frame_crc_validation();
    failed += test_claim_hello_and_commit_frames();
    failed += test_claim_conflict_release_resolve_frames();

    if (failed != 0) {
        (void)printf("refmem_slot_claim tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_slot_claim tests passed\n");
    return 0;
}
