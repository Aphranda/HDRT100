#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tdma_process_image_map.h"
#include "tdma_process_image_layout.h"

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr,
            "FAIL %s: got %lu expected %lu\n",
            name,
            (unsigned long)actual,
            (unsigned long)expected);
    return 1;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    return expect_u32(name, actual ? 1u : 0u, expected ? 1u : 0u);
}

/* Independent bit recurrence retained from the wire definition. Testing
 * every state/input pair proves the replacement transition for any stream,
 * including streams produced by a peer running the previous firmware. */
static uint16_t crc16_bit_reference(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8u;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        crc = (crc & 0x8000u) != 0u
            ? (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
    }
    return crc;
}

static int test_mailbox_crc16(void)
{
    for (uint32_t state = 0u; state <= UINT16_MAX; ++state) {
        for (uint32_t byte = 0u; byte <= UINT8_MAX; ++byte) {
            if (tdma_process_image_crc16_update_byte((uint16_t)state, (uint8_t)byte) !=
                crc16_bit_reference((uint16_t)state, (uint8_t)byte)) {
                fprintf(stderr, "CRC transition mismatch: state=%lu byte=%lu\n",
                        (unsigned long)state, (unsigned long)byte);
                return 1;
            }
        }
    }
    int failed = expect_u32("CRC known CCITT-FALSE vector",
        tdma_process_image_crc16_ccitt((const uint8_t *)"123456789", 9u), 0x29b1u);
    failed += expect_u32("CRC NULL empty", tdma_process_image_crc16_ccitt(NULL, 0u), 0u);
    failed += expect_u32("CRC NULL nonempty", tdma_process_image_crc16_ccitt(NULL, 30u), 0u);
    uint8_t payload[512];
    uint32_t random = 0x12a34567u;
    for (uint32_t pattern = 0u; pattern < 8u; ++pattern) {
        for (size_t i = 0u; i < sizeof(payload); ++i) {
            random = random * 1664525u + 1013904223u;
            payload[i] = pattern == 0u ? 0u : pattern == 1u ? 0xffu :
                pattern == 2u ? (uint8_t)i : pattern == 3u ? (uint8_t)~i :
                (uint8_t)(random >> 24u);
        }
        uint16_t reference = 0xffffu;
        for (size_t length = 0u; length <= sizeof(payload); ++length) {
            if (length != 0u) reference = crc16_bit_reference(reference, payload[length - 1u]);
            failed += expect_u32("CRC every prefix, including mailbox boundary",
                tdma_process_image_crc16_ccitt(payload, length), reference);
        }
    }
    if (failed == 0) puts("CRC16: 16777216 state/byte transitions and 4104 stream prefixes passed");
    return failed;
}

static tdma_process_image_map_t make_map(void)
{
    tdma_process_image_map_t map = {
        .version = TDMA_PROCESS_IMAGE_MAP_VERSION,
        .payload_size = 192u,
        .segment_count = 3u,
        .segment = {
            {
                .used = 1u,
                .segment_id = 0u,
                .owner_slot_id = 0u,
                .payload_class = 1u,
                .byte_offset = 0u,
                .byte_length = 64u,
                .flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE |
                         TDMA_PROCESS_SEGMENT_FLAG_COALESCE_LATEST,
            },
            {
                .used = 1u,
                .segment_id = 1u,
                .owner_slot_id = 1u,
                .payload_class = 2u,
                .byte_offset = 64u,
                .byte_length = 96u,
                .flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE |
                         TDMA_PROCESS_SEGMENT_FLAG_ACK_REQUIRED |
                         TDMA_PROCESS_SEGMENT_FLAG_COALESCE_LATEST,
            },
            {
                .used = 1u,
                .segment_id = 2u,
                .owner_slot_id = 1u,
                .payload_class = 3u,
                .byte_offset = 160u,
                .byte_length = 32u,
                .flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE |
                         TDMA_PROCESS_SEGMENT_FLAG_ACK_REQUIRED |
                         TDMA_PROCESS_SEGMENT_FLAG_COMMAND_QUEUE,
            },
        },
    };
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    return map;
}

int main(void)
{
    int failed = test_mailbox_crc16();
    tdma_process_image_map_result_t result =
        TDMA_PROCESS_IMAGE_MAP_BAD_ARGUMENT;
    tdma_process_image_segment_t segment;
    tdma_process_image_map_t map = make_map();

    failed += expect_bool("valid map",
                          tdma_process_image_map_validate(&map, &result),
                          true);
    failed += expect_u32("valid result", result, TDMA_PROCESS_IMAGE_MAP_OK);
    failed += expect_bool("find refmem segment",
                          tdma_process_image_map_find(&map,
                                                      1u,
                                                      &segment,
                                                      &result),
                          true);
    failed += expect_u32("refmem owner", segment.owner_slot_id, 1u);
    failed += expect_u32("refmem offset", segment.byte_offset, 64u);
    failed += expect_bool("owner can publish",
                          tdma_process_image_map_can_publish(&map,
                                                             1u << 1u,
                                                             1u,
                                                             &segment,
                                                             &result),
                          true);
    failed += expect_bool("other slot rejected",
                          tdma_process_image_map_can_publish(&map,
                                                             1u << 2u,
                                                             1u,
                                                             &segment,
                                                             &result),
                          false);
    failed += expect_u32("owner reject result",
                         result,
                         TDMA_PROCESS_IMAGE_MAP_NOT_OWNER);

    tdma_process_image_map_t overlap = make_map();
    overlap.segment[2].byte_offset = 150u;
    overlap.map_crc32 = tdma_process_image_map_crc32(&overlap);
    failed += expect_bool("overlap rejected",
                          tdma_process_image_map_validate(&overlap, &result),
                          false);
    failed += expect_u32("overlap result",
                         result,
                         TDMA_PROCESS_IMAGE_MAP_OVERLAP);

    tdma_process_image_map_t duplicate = make_map();
    duplicate.segment[2].segment_id = 1u;
    duplicate.map_crc32 = tdma_process_image_map_crc32(&duplicate);
    failed += expect_bool("duplicate rejected",
                          tdma_process_image_map_validate(&duplicate, &result),
                          false);
    failed += expect_u32("duplicate result",
                         result,
                         TDMA_PROCESS_IMAGE_MAP_DUPLICATE_ID);

    tdma_process_image_map_t bad_crc = make_map();
    bad_crc.map_crc32 ^= 1u;
    failed += expect_bool("crc rejected",
                          tdma_process_image_map_validate(&bad_crc, &result),
                          false);
    failed += expect_u32("crc result",
                         result,
                         TDMA_PROCESS_IMAGE_MAP_CRC_MISMATCH);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_process_image_map tests passed");
    return 0;
}
