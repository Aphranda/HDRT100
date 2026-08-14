#include "refmem_realtime_contract.h"

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

static void make_pio_spi_contract_inputs(refmem_node_load_entry_t *load,
                                         refmem_fb_instance_entry_t *instance,
                                         refmem_app_node_entry_t *node,
                                         refmem_slot_claim_map_t *claim_map)
{
    (void)memset(load, 0, sizeof(*load));
    (void)memset(instance, 0, sizeof(*instance));
    (void)memset(node, 0, sizeof(*node));
    (void)memset(claim_map, 0, sizeof(*claim_map));

    load->node_id = 0u;
    load->instance_id = 0u;
    load->enabled = 1u;

    instance->instance_id = 0u;
    instance->enable_condition = 1u;
    instance->resource_claim =
        refmem_realtime_contract_transport_resource_claim(REFMEM_APP_TRANSPORT_PIO_SPI);
    instance->io_claim =
        refmem_realtime_contract_transport_io_claim(REFMEM_APP_TRANSPORT_PIO_SPI);
    instance->ip_core_claim =
        refmem_realtime_contract_transport_ip_core_claim(REFMEM_APP_TRANSPORT_PIO_SPI);
    instance->time_budget_us = 50u;

    node->node_id = 0u;
    node->capability_mask =
        REFMEM_APP_CAP_BOARD | REFMEM_APP_CAP_REFMEM | REFMEM_APP_CAP_VDC |
        REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_CORE1_RT;

    claim_map->version = REFMEM_SLOT_CLAIM_VERSION;
    claim_map->claim_epoch = 1u;
    claim_map->slot_count = REFMEM_APP_MODEL_NODE_COUNT;
    claim_map->candidate_count = 1u;
    claim_map->assigned_count = 1u;

    refmem_slot_claim_assignment_t *slot = &claim_map->slot[0];
    slot->slot_id = 0u;
    slot->board_id = 0u;
    slot->board_uuid_crc32 = 0xB0000000u;
    slot->capability_mask = node->capability_mask;
    slot->io_constraint_mask = REFMEM_APP_IO_PIO_SPI_SYNC;
    slot->ip_core_mask = REFMEM_APP_IP_PIO_SPI_SYNC_DELTA;
    slot->claim_count = 1u;
    slot->claim_state = REFMEM_SLOT_CLAIM_CLAIMED;
    slot->reason = REFMEM_SLOT_CLAIM_REASON_OK;
    slot->claim_epoch = claim_map->claim_epoch;
}

static int test_transport_mapping(void)
{
    int failed = 0;

    failed += expect_u32("pio spi resource claim",
                         refmem_realtime_contract_transport_resource_claim(
                             REFMEM_APP_TRANSPORT_PIO_SPI),
                         REFMEM_APP_RESOURCE_PIO |
                             REFMEM_APP_RESOURCE_DMA |
                             REFMEM_APP_RESOURCE_CORE1_RT);
    failed += expect_u32("pio spi io claim",
                         refmem_realtime_contract_transport_io_claim(
                             REFMEM_APP_TRANSPORT_PIO_SPI),
                         REFMEM_APP_IO_PIO_SPI_SYNC);
    failed += expect_u32("pio spi ip claim",
                         refmem_realtime_contract_transport_ip_core_claim(
                             REFMEM_APP_TRANSPORT_PIO_SPI),
                         REFMEM_APP_IP_PIO_SPI_SYNC_DELTA);
    failed += expect_u32("pio spi io capability",
                         refmem_realtime_contract_io_capability_mask(
                             REFMEM_APP_IO_PIO_SPI_SYNC),
                         REFMEM_APP_CAP_PIO |
                             REFMEM_APP_CAP_DMA |
                             REFMEM_APP_CAP_CORE1_RT);
    failed += expect_u32("pio spi ip capability",
                         refmem_realtime_contract_ip_capability_mask(
                             REFMEM_APP_IP_PIO_SPI_SYNC_DELTA),
                         REFMEM_APP_CAP_PIO |
                             REFMEM_APP_CAP_DMA |
                             REFMEM_APP_CAP_CORE1_RT);
    return failed;
}

static int test_pio_spi_contract_accepts_complete_board(void)
{
    int failed = 0;
    refmem_node_load_entry_t load;
    refmem_fb_instance_entry_t instance;
    refmem_app_node_entry_t node;
    refmem_slot_claim_map_t claim_map;
    refmem_realtime_contract_t contract;

    make_pio_spi_contract_inputs(&load, &instance, &node, &claim_map);
    failed += expect_bool("pio spi contract",
                          refmem_realtime_contract_derive_from_claim_map(&load,
                                                                         &instance,
                                                                         &node,
                                                                         &claim_map,
                                                                         &contract),
                          true);
    failed += expect_u32("pio spi contract valid", contract.valid, 1u);
    failed += expect_u32("pio spi contract result", contract.result, REFMEM_RT_CONTRACT_OK);
    failed += expect_u32("pio spi required caps",
                         contract.required_capability_mask,
                         REFMEM_APP_CAP_PIO |
                             REFMEM_APP_CAP_DMA |
                             REFMEM_APP_CAP_CORE1_RT);
    return failed;
}

static int test_pio_spi_contract_rejects_missing_parts(void)
{
    int failed = 0;
    refmem_node_load_entry_t load;
    refmem_fb_instance_entry_t instance;
    refmem_app_node_entry_t node;
    refmem_slot_claim_map_t claim_map;
    refmem_realtime_contract_t contract;

    make_pio_spi_contract_inputs(&load, &instance, &node, &claim_map);
    claim_map.slot[0].capability_mask &= ~REFMEM_APP_CAP_DMA;
    failed += expect_bool("pio spi missing dma",
                          refmem_realtime_contract_derive_from_claim_map(&load,
                                                                         &instance,
                                                                         &node,
                                                                         &claim_map,
                                                                         &contract),
                          false);
    failed += expect_u32("pio spi missing dma result",
                         contract.result,
                         REFMEM_RT_CONTRACT_MISSING_CAPABILITY);

    make_pio_spi_contract_inputs(&load, &instance, &node, &claim_map);
    claim_map.slot[0].ip_core_mask = 0u;
    failed += expect_bool("pio spi missing ip",
                          refmem_realtime_contract_derive_from_claim_map(&load,
                                                                         &instance,
                                                                         &node,
                                                                         &claim_map,
                                                                         &contract),
                          false);
    failed += expect_u32("pio spi missing ip result",
                         contract.result,
                         REFMEM_RT_CONTRACT_MISSING_IP_CORE);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_transport_mapping();
    failed += test_pio_spi_contract_accepts_complete_board();
    failed += test_pio_spi_contract_rejects_missing_parts();

    if (failed != 0) {
        (void)printf("refmem_realtime_contract tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_realtime_contract tests passed\n");
    return 0;
}
