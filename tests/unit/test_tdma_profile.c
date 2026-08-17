#include "tdma_profile.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

static int test_default_foundation_profile(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_profile_result_t result = TDMA_PROFILE_BAD_ARGUMENT;

    failed += expect_bool("default profile",
                          tdma_foundation_profile_default(&profile,
                                                          7u,
                                                          2u,
                                                          0u,
                                                          TDMA_ADAPTER_PIO_SPI),
                          true);
    failed += expect_bool("default profile valid",
                          tdma_foundation_profile_validate(&profile, &result),
                          true);
    failed += expect_u32("default result", result, TDMA_PROFILE_OK);
    failed += expect_u32("owner", profile.owner_instance_id, 7u);
    failed += expect_u32("upstream", profile.ring.upstream_slot_id, 1u);
    failed += expect_u32("downstream", profile.ring.downstream_slot_id, 3u);
    failed += expect_u32("feedback", profile.ring.feedback_slot_id, 0u);
    failed += expect_u32("required payloads",
                         profile.resource.payload_whitelist_mask &
                             TDMA_PAYLOAD_FOUNDATION_REQUIRED_MASK,
                         TDMA_PAYLOAD_FOUNDATION_REQUIRED_MASK);
    return failed;
}

static int test_ring_rejects_direction_and_topology_conflicts(void)
{
    int failed = 0;
    tdma_ring_profile_t ring;
    tdma_profile_result_t result = TDMA_PROFILE_OK;

    (void)tdma_ring_profile_default(&ring, 2u, 0u, 5u);
    ring.down_group_id = ring.up_group_id;
    ring.profile_crc32 = tdma_ring_profile_crc32(&ring);
    failed += expect_bool("same direction group",
                          tdma_ring_profile_validate(&ring, &result),
                          false);
    failed += expect_u32("direction result",
                         result,
                         TDMA_PROFILE_DIRECTION_CONFLICT);

    (void)tdma_ring_profile_default(&ring, 2u, 0u, 5u);
    ring.downstream_slot_id = 4u;
    ring.profile_crc32 = tdma_ring_profile_crc32(&ring);
    failed += expect_bool("bad topology",
                          tdma_ring_profile_validate(&ring, &result),
                          false);
    failed += expect_u32("topology result", result, TDMA_PROFILE_BAD_TOPOLOGY);
    return failed;
}

static int test_foundation_rejects_resource_and_payload_conflicts(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_profile_result_t result = TDMA_PROFILE_OK;

    (void)tdma_foundation_profile_default(&profile,
                                          1u,
                                          0u,
                                          0u,
                                          TDMA_ADAPTER_BISS_C);
    profile.resource.down_state_machine_id =
        profile.resource.up_state_machine_id;
    profile.profile_crc32 = tdma_foundation_profile_crc32(&profile);
    failed += expect_bool("state machine conflict",
                          tdma_foundation_profile_validate(&profile, &result),
                          false);
    failed += expect_u32("resource result",
                         result,
                         TDMA_PROFILE_RESOURCE_CONFLICT);

    (void)tdma_foundation_profile_default(&profile,
                                          1u,
                                          0u,
                                          0u,
                                          TDMA_ADAPTER_BISS_C);
    profile.resource.payload_whitelist_mask &=
        ~TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_IDLE_BEACON);
    profile.profile_crc32 = tdma_foundation_profile_crc32(&profile);
    failed += expect_bool("missing idle payload",
                          tdma_foundation_profile_validate(&profile, &result),
                          false);
    failed += expect_u32("payload result", result, TDMA_PROFILE_PAYLOAD_MISSING);

    (void)tdma_foundation_profile_default(&profile,
                                          1u,
                                          0u,
                                          0u,
                                          TDMA_ADAPTER_BISS_C);
    profile.resource.traffic[TDMA_TRAFFIC_OTA_BULK].payload_mask =
        TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_CONFIG_CONTROL);
    profile.profile_crc32 = tdma_foundation_profile_crc32(&profile);
    failed += expect_bool("duplicate traffic classification",
                          tdma_foundation_profile_validate(&profile, &result),
                          false);
    failed += expect_u32("traffic classification result",
                         result,
                         TDMA_PROFILE_CAPACITY_INVALID);
    return failed;
}

static int test_non_pio_adapter_does_not_claim_state_machines(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_profile_result_t result = TDMA_PROFILE_BAD_ARGUMENT;

    failed += expect_bool("uart default",
                          tdma_foundation_profile_default(&profile,
                                                          2u,
                                                          0u,
                                                          0u,
                                                          TDMA_ADAPTER_UART),
                          true);
    failed += expect_bool("uart profile valid",
                          tdma_foundation_profile_validate(&profile, &result),
                          true);
    failed += expect_u32("uart pio unused",
                         profile.resource.pio_block_id,
                         TDMA_RESOURCE_ID_UNUSED);
    failed += expect_u32("uart up sm unused",
                         profile.resource.up_state_machine_id,
                         TDMA_RESOURCE_ID_UNUSED);
    failed += expect_u32("uart down sm unused",
                         profile.resource.down_state_machine_id,
                         TDMA_RESOURCE_ID_UNUSED);
    return failed;
}

static int test_foundation_profile_wire_roundtrip(void)
{
    int failed = 0;
    uint8_t wire[TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE];
    tdma_foundation_profile_t source;
    tdma_foundation_profile_t decoded;
    tdma_profile_result_t result = TDMA_PROFILE_BAD_ARGUMENT;

    (void)tdma_foundation_profile_default(&source, 11u, 0u, 0u,
                                          TDMA_ADAPTER_PIO_SPI);
    source.resource.io_claim_mask = 0x40u;
    source.resource.ip_core_claim_mask = 0xC0u;
    source.profile_crc32 = tdma_foundation_profile_crc32(&source);
    failed += expect_bool("wire encode",
                          tdma_foundation_profile_encode_table(&source,
                                                               wire,
                                                               sizeof(wire)),
                          true);
    failed += expect_bool("wire decode",
                          tdma_foundation_profile_decode_table(wire,
                                                               sizeof(wire),
                                                               &decoded,
                                                               &result),
                          true);
    failed += expect_u32("wire owner", decoded.owner_instance_id, 11u);
    failed += expect_u32("wire profile crc", decoded.profile_crc32,
                         source.profile_crc32);
    failed += expect_bool("wire exact profile",
                          memcmp(&source, &decoded, sizeof(source)) == 0,
                          true);

    wire[12] ^= 1u;
    failed += expect_bool("wire crc rejection",
                          tdma_foundation_profile_decode_table(wire,
                                                               sizeof(wire),
                                                               &decoded,
                                                               &result),
                          false);
    return failed;
}

static int test_foundation_profile_rejects_budget_overcommit(void)
{
    int failed = 0;
    tdma_foundation_profile_t profile;
    tdma_profile_result_t result = TDMA_PROFILE_OK;

    (void)tdma_foundation_profile_default(&profile, 11u, 0u, 0u,
                                          TDMA_ADAPTER_PIO_SPI);
    profile.resource.cycle_capacity_bytes = 800u;
    profile.profile_crc32 = tdma_foundation_profile_crc32(&profile);
    failed += expect_bool("cycle budget overcommit",
                          tdma_foundation_profile_validate(&profile, &result),
                          false);
    failed += expect_u32("cycle budget result", result,
                         TDMA_PROFILE_CAPACITY_INVALID);

    (void)tdma_foundation_profile_default(&profile, 11u, 0u, 0u,
                                          TDMA_ADAPTER_PIO_SPI);
    profile.resource.queue_memory_capacity_bytes = 4096u;
    profile.profile_crc32 = tdma_foundation_profile_crc32(&profile);
    failed += expect_bool("queue memory overcommit",
                          tdma_foundation_profile_validate(&profile, &result),
                          false);
    failed += expect_u32("queue memory result", result,
                         TDMA_PROFILE_CAPACITY_INVALID);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_default_foundation_profile();
    failed += test_ring_rejects_direction_and_topology_conflicts();
    failed += test_foundation_rejects_resource_and_payload_conflicts();
    failed += test_non_pio_adapter_does_not_claim_state_machines();
    failed += test_foundation_profile_wire_roundtrip();
    failed += test_foundation_profile_rejects_budget_overcommit();

    if (failed != 0) {
        (void)printf("tdma_profile tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("tdma_profile tests passed\n");
    return 0;
}
