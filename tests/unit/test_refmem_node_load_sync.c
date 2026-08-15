#include "refmem_node_load_sync.h"
#include "refmem_sync.h"

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

static refmem_node_load_entry_t make_entry(uint32_t node_id,
                                           uint32_t instance_id)
{
    refmem_node_load_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.load_id = instance_id;
    entry.application_id = 1u;
    entry.profile_id = 1u;
    entry.node_id = node_id;
    entry.instance_id = instance_id;
    entry.role_mask = REFMEM_APP_ROLE_MODEL_TURNTABLE | REFMEM_APP_ROLE_TEST_AGENT;
    entry.persona_mask = REFMEM_APP_PERSONA_MODEL_INSTRUMENTS;
    entry.enabled = 1u;
    entry.required = 0u;
    entry.fail_policy = REFMEM_APP_FAIL_REPORT_ONLY;
    entry.load_order = node_id;
    return entry;
}

static int test_payload_roundtrip(void)
{
    int failed = 0;
    const refmem_node_load_entry_t source = make_entry(4u, 10u);
    refmem_node_load_entry_t decoded;
    refmem_sync_delta_header_t delta;
    uint8_t payload[96];
    uint16_t payload_size = 0u;

    failed += expect_bool("build payload",
                          refmem_node_load_sync_build_delta_payload(&source,
                                                                    55u,
                                                                    7u,
                                                                    payload,
                                                                    sizeof(payload),
                                                                    &payload_size),
                          true);
    failed += expect_bool("decode payload",
                          refmem_node_load_sync_decode_delta_payload(payload,
                                                                    payload_size,
                                                                    &delta,
                                                                    &decoded),
                          true);
    failed += expect_u32("delta id", delta.delta_id, 7u);
    failed += expect_u32("delta slot", delta.slot_id, 4u);
    failed += expect_u32("delta kind", delta.payload_kind, REFMEM_NODE_LOAD_SYNC_PAYLOAD_KIND_ENTRY);
    failed += expect_u32("delta seq", delta.slot_seq, 55u);
    failed += expect_u32("decoded node", decoded.node_id, source.node_id);
    failed += expect_u32("decoded instance", decoded.instance_id, source.instance_id);
    failed += expect_u32("decoded role", decoded.role_mask, source.role_mask);
    failed += expect_u32("decoded persona", decoded.persona_mask, source.persona_mask);
    return failed;
}

static int test_frame_roundtrip_updates_mirror(void)
{
    int failed = 0;
    const refmem_node_load_entry_t source = make_entry(2u, 9u);
    refmem_node_load_entry_t decoded;
    uint8_t frame[128];
    size_t frame_size = 0u;
    refmem_sync_frame_header_t header;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;
    refmem_sync_context_t context;

    failed += expect_bool("build frame",
                          refmem_node_load_sync_build_delta_frame(&source,
                                                                  0u,
                                                                  0x02u,
                                                                  3u,
                                                                  4u,
                                                                  12u,
                                                                  77u,
                                                                  99u,
                                                                  frame,
                                                                  sizeof(frame),
                                                                  &frame_size),
                          true);
    failed += expect_u32("validate frame",
                         refmem_sync_frame_validate(frame,
                                                    frame_size,
                                                    &header,
                                                    &payload,
                                                    &payload_size),
                         REFMEM_SYNC_FRAME_OK);
    failed += expect_bool("decode frame payload",
                          refmem_node_load_sync_decode_delta_payload(payload,
                                                                    payload_size,
                                                                    NULL,
                                                                    &decoded),
                          true);
    failed += expect_u32("decoded node from frame", decoded.node_id, source.node_id);

    failed += expect_bool("sync init", refmem_sync_init(&context, 1u, 3u, 4u), true);
    failed += expect_u32("receive frame",
                         refmem_sync_receive_frame(&context, frame, frame_size, NULL),
                         REFMEM_SYNC_RX_ACCEPTED);
    const refmem_sync_mirror_snapshot_t *mirror = refmem_sync_get_mirror(&context, 0u);
    failed += expect_bool("mirror exists", mirror != NULL, true);
    if (mirror != NULL) {
        failed += expect_u32("mirror visible", mirror->visible, 1u);
        failed += expect_u32("mirror slot", mirror->slot_id, source.node_id);
        failed += expect_u32("mirror kind", mirror->payload_kind, REFMEM_NODE_LOAD_SYNC_PAYLOAD_KIND_ENTRY);
        failed += expect_u32("mirror field", mirror->field_id, REFMEM_NODE_LOAD_SYNC_FIELD_ENTRY);
        failed += expect_u32("mirror seq", mirror->slot_seq, 77u);
    }
    return failed;
}

static int test_rejects_bad_entry(void)
{
    int failed = 0;
    refmem_node_load_entry_t entry = make_entry(0u, 1u);
    uint8_t payload[96];
    uint16_t payload_size = 1u;

    entry.node_id = REFMEM_APP_MODEL_NODE_COUNT;
    failed += expect_bool("reject bad node",
                          refmem_node_load_sync_build_delta_payload(&entry,
                                                                    1u,
                                                                    1u,
                                                                    payload,
                                                                    sizeof(payload),
                                                                    &payload_size),
                          false);
    failed += expect_u32("bad payload size reset", payload_size, 0u);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_payload_roundtrip();
    failed += test_frame_roundtrip_updates_mirror();
    failed += test_rejects_bad_entry();

    if (failed != 0) {
        (void)printf("refmem_node_load_sync tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_node_load_sync tests passed\n");
    return 0;
}
