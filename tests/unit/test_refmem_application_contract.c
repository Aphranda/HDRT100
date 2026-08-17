#include "refmem_application_contract.h"

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

static uint32_t baseline_caps(void)
{
    return REFMEM_APP_CAP_BASELINE;
}

static void make_generic_nodes(refmem_generic_node_table_t *table)
{
    (void)memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->node_count = REFMEM_APP_MODEL_NODE_COUNT;
    for (uint32_t i = 0u; i < table->node_count; i++) {
        table->node[i].node_id = i;
        table->node[i].node_uuid_crc32 = 0xA0000000u + i;
        table->node[i].capability_mask = baseline_caps() |
                                         REFMEM_APP_CAP_PIO |
                                         REFMEM_APP_CAP_DMA |
                                         REFMEM_APP_CAP_CORE1_RT;
        table->node[i].claim_policy = REFMEM_APP_CLAIM_STRICT_UUID;
        table->node[i].claim_priority = 100u - i;
        table->node[i].default_persona_mask = REFMEM_APP_PERSONA_SPARE;
        table->node[i].hw_profile_crc32 = 0x10000000u + i;
        table->node[i].online_required = 1u;
        table->node[i].fail_policy = REFMEM_APP_FAIL_STOP;
    }
}

static void make_board_capabilities(refmem_board_capability_table_t *table)
{
    (void)memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->board_count = REFMEM_APP_MODEL_NODE_COUNT;
    for (uint32_t i = 0u; i < table->board_count; i++) {
        table->board[i].board_id = i;
        table->board[i].board_uuid_crc32 = 0xB0000000u + i;
        table->board[i].capability_mask = baseline_caps() |
                                          REFMEM_APP_CAP_PIO |
                                          REFMEM_APP_CAP_DMA |
                                          REFMEM_APP_CAP_CORE1_RT;
        table->board[i].io_constraint_mask = REFMEM_APP_IO_PIO_SPI_SYNC;
        table->board[i].ip_core_mask = REFMEM_APP_IP_PIO_SPI_SYNC_DELTA;
        table->board[i].default_persona_mask = REFMEM_APP_PERSONA_MODEL_INSTRUMENTS;
        table->board[i].hw_profile_crc32 = 0x20000000u + i;
        table->board[i].active_default_slot = i;
        table->board[i].online_required = 1u;
    }
}

static int test_slot_substrate_accepts_decoupled_board_defaults(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;

    make_generic_nodes(&nodes);
    make_board_capabilities(&boards);

    boards.board[0].active_default_slot = 1u;
    boards.board[1].active_default_slot = 0u;
    boards.board[0].board_uuid_crc32 = 0xC0FFEE00u;
    boards.board[0].default_persona_mask = REFMEM_APP_PERSONA_GATEWAY;
    boards.board[0].hw_profile_crc32 = 0xDEADBEEFu;

    failed += expect_bool("decoupled board defaults",
                          refmem_application_contract_validate_slot_substrate(&nodes,
                                                                             &boards),
                          true);
    return failed;
}

static int test_slot_substrate_rejects_bad_node_contract(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;

    make_generic_nodes(&nodes);
    make_board_capabilities(&boards);
    nodes.node[2].capability_mask &= ~REFMEM_APP_CAP_VDC;
    failed += expect_bool("node missing baseline",
                          refmem_application_contract_validate_slot_substrate(&nodes,
                                                                             &boards),
                          false);

    make_generic_nodes(&nodes);
    make_board_capabilities(&boards);
    nodes.node[3].claim_policy = REFMEM_APP_CLAIM_DISABLED;
    nodes.node[3].online_required = 1u;
    failed += expect_bool("required disabled node",
                          refmem_application_contract_validate_slot_substrate(&nodes,
                                                                             &boards),
                          false);
    return failed;
}

static int test_slot_substrate_rejects_bad_board_contract(void)
{
    int failed = 0;
    refmem_generic_node_table_t nodes;
    refmem_board_capability_table_t boards;

    make_generic_nodes(&nodes);
    make_board_capabilities(&boards);
    boards.board[4].capability_mask &= ~REFMEM_APP_CAP_REFMEM;
    failed += expect_bool("board missing baseline",
                          refmem_application_contract_validate_slot_substrate(&nodes,
                                                                             &boards),
                          false);

    make_generic_nodes(&nodes);
    make_board_capabilities(&boards);
    boards.board[5].active_default_slot = REFMEM_APP_MODEL_NODE_COUNT;
    failed += expect_bool("board default slot range",
                          refmem_application_contract_validate_slot_substrate(&nodes,
                                                                             &boards),
                          false);

    make_generic_nodes(&nodes);
    make_board_capabilities(&boards);
    boards.board[6].capability_mask &= ~REFMEM_APP_CAP_DMA;
    failed += expect_bool("board io capability coverage",
                          refmem_application_contract_validate_slot_substrate(&nodes,
                                                                             &boards),
                          false);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_slot_substrate_accepts_decoupled_board_defaults();
    failed += test_slot_substrate_rejects_bad_node_contract();
    failed += test_slot_substrate_rejects_bad_board_contract();

    if (failed != 0) {
        (void)printf("refmem_application_contract tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_application_contract tests passed\n");
    return 0;
}
