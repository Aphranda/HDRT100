#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_flight_overlay.h"

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) return 0;
    fprintf(stderr, "FAIL %s: got %lu expected %lu\n", name,
            (unsigned long)actual, (unsigned long)expected);
    return 1;
}

int main(void)
{
    int failed = 0;
    uint8_t incoming[12];
    uint8_t processed[12];
    uint32_t script[32];
    tdma_flight_overlay_result_t result;
    for (uint32_t i = 0u; i < sizeof(incoming); i++) incoming[i] = (uint8_t)i;
    memcpy(processed, incoming, sizeof(processed));
    processed[4] = 0xA5u;

    failed += expect_u32(
        "byte aligned build",
        tdma_flight_overlay_build(incoming, processed, sizeof(incoming),
                                  4u, 2u, 0u, 24u, 0u, NULL, 0u,
                                  script, 32u, &result),
        true);
    failed += expect_u32("byte aligned replacement count",
                         result.replacement_byte_count, 1u);
    failed += expect_u32("elastic replacement index opcode",
                         script[2u + 1u + 4u + 4u] & 0xC0000000u,
                         TDMA_FLIGHT_OVERLAY_SCRIPT_REPLACE);
    failed += expect_u32("elastic replacement value",
                         tdma_flight_overlay_script_byte(
                             script[2u + 1u + 4u + 4u]),
                         0xA5u);
    failed += expect_u32("end opcode",
                         script[24] & 0xC0000000u,
                         TDMA_FLIGHT_OVERLAY_SCRIPT_END);

    memcpy(processed, incoming, sizeof(processed));
    processed[4] = 0xA5u;
    failed += expect_u32(
        "shifted build",
        tdma_flight_overlay_build(incoming, processed, sizeof(incoming),
                                  4u, 1u, 3u, 24u, 0u, NULL, 0u,
                                  script, 32u, &result),
        true);
    failed += expect_u32("shifted replacement count",
                         result.replacement_byte_count, 2u);
    const uint32_t first = 1u + 1u + 4u + 4u;
    failed += expect_u32("shifted first boundary",
                         tdma_flight_overlay_script_byte(script[first]),
                         (uint8_t)(((uint32_t)(incoming[3] & 0x07u) << 5u) |
                                   (0xA5u >> 3u)));
    failed += expect_u32("shifted second boundary",
                         tdma_flight_overlay_script_byte(script[first + 1u]),
                         (uint8_t)(((uint32_t)(0xA5u & 0x07u) << 5u) |
                                   (incoming[5] >> 3u)));

    /* Frame N's model has A and the desired frame N+1 output is also A, but
     * the resident wire byte has become B. The owned-byte bitmap must force
     * REPLACE; a value-only comparison would PASS B into the next frame. */
    memcpy(processed, incoming, sizeof(processed));
    const uint8_t resident_wire_b = 0x5Au;
    const uint8_t desired_c = processed[4];
    uint32_t force_replace_bitmap[1] = {1u << 4u};
    failed += expect_u32("stale model differs from resident wire",
                         resident_wire_b != desired_c, true);
    failed += expect_u32(
        "three-frame owned byte build",
        tdma_flight_overlay_build(incoming, processed, sizeof(incoming),
                                  4u, 2u, 0u, 24u, 0u,
                                  force_replace_bitmap, 1u,
                                  script, 32u, &result),
        true);
    failed += expect_u32("three-frame owned byte replacement count",
                         result.replacement_byte_count, 1u);
    failed += expect_u32("three-frame owned byte opcode",
                         script[2u + 1u + 4u + 4u] & 0xC0000000u,
                         TDMA_FLIGHT_OVERLAY_SCRIPT_REPLACE);
    failed += expect_u32("three-frame owned byte value",
                         tdma_flight_overlay_script_byte(
                             script[2u + 1u + 4u + 4u]),
                         desired_c);

    if (failed != 0) return 1;
    puts("tdma_flight_overlay tests passed");
    return 0;
}
