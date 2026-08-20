#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tdma_payload_registry.h"

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %u expected %u\n",
            name, actual ? 1u : 0u, expected ? 1u : 0u);
    return 1;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %lu expected %lu\n",
            name, (unsigned long)actual, (unsigned long)expected);
    return 1;
}

int main(void)
{
    int failed = 0;
    tdma_payload_registry_t registry;
    tdma_payload_registry_snapshot_t snapshot;
    const tdma_payload_binding_t delta = {
        .producer_id = 1u,
        .consumer_id = 2u,
        .payload_class = TDMA_PAYLOAD_CLASS_REFMEM_DELTA,
        .frame_class = TDMA_PAYLOAD_FRAME_CLASS_SHORT,
        .max_payload_size = 128u,
    };
    const tdma_payload_binding_t log = {
        .producer_id = 3u,
        .consumer_id = 4u,
        .payload_class = TDMA_PAYLOAD_CLASS_LOG_STREAM,
        .frame_class = TDMA_PAYLOAD_FRAME_CLASS_LONG,
        .max_payload_size = 512u,
    };

    failed += expect_bool("init", tdma_payload_registry_init(&registry, 292u, 1024u), true);
    failed += expect_bool("register delta",
                          tdma_payload_registry_register(&registry, &delta),
                          true);
    failed += expect_bool("configure whitelist",
                          tdma_payload_registry_configure(
                              &registry,
                              TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_REFMEM_DELTA) |
                                  TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_LOG_STREAM),
                              292u,
                              1024u),
                          true);
    failed += expect_bool("register log",
                          tdma_payload_registry_register(&registry, &log),
                          true);
    failed += expect_bool("admit delta",
                          tdma_payload_registry_admit(
                              &registry,
                              TDMA_PAYLOAD_FRAME_CLASS_SHORT,
                              TDMA_PAYLOAD_CLASS_REFMEM_DELTA,
                              64u),
                          true);
    failed += expect_bool("admit empty rx window",
                          tdma_payload_registry_admit(
                              &registry,
                              TDMA_PAYLOAD_FRAME_CLASS_SHORT,
                              TDMA_PAYLOAD_CLASS_REFMEM_DELTA,
                              0u),
                          true);
    failed += expect_bool("reject oversized delta",
                          tdma_payload_registry_admit(
                              &registry,
                              TDMA_PAYLOAD_FRAME_CLASS_SHORT,
                              TDMA_PAYLOAD_CLASS_REFMEM_DELTA,
                              129u),
                          false);
    failed += expect_bool("reject unregistered config",
                          tdma_payload_registry_admit(
                              &registry,
                              TDMA_PAYLOAD_FRAME_CLASS_SHORT,
                              TDMA_PAYLOAD_CLASS_CONFIG_CONTROL,
                              16u),
                          false);
    failed += expect_bool("reject incompatible reconfigure",
                          tdma_payload_registry_configure(
                              &registry,
                              TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_REFMEM_DELTA),
                              292u,
                              1024u),
                          false);
    failed += expect_bool("snapshot",
                          tdma_payload_registry_get_snapshot(&registry, &snapshot),
                          true);
    failed += expect_u32("used count", snapshot.used_count, 2u);
    failed += expect_u32("admitted count", snapshot.admitted_count, 2u);
    failed += expect_u32("reject count", snapshot.reject_count, 3u);
    failed += expect_u32("whitelist preserved",
                         snapshot.payload_whitelist_mask,
                         TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_REFMEM_DELTA) |
                             TDMA_PAYLOAD_BIT(TDMA_PAYLOAD_CLASS_LOG_STREAM));
    registry.guard |= 1u;
    failed += expect_bool("odd snapshot guard is bounded",
                          tdma_payload_registry_get_snapshot(&registry, &snapshot),
                          false);
    registry.guard++;
    failed += expect_bool("snapshot recovers after guard closes",
                          tdma_payload_registry_get_snapshot(&registry, &snapshot),
                          true);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_payload_registry tests passed");
    return 0;
}
