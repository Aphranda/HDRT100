#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_bridge.h"

static unsigned s_begin_calls;
static unsigned s_data_calls;
static unsigned s_end_calls;
static unsigned s_abort_calls;
static uint32_t s_begin_size;
static uint32_t s_begin_crc32;
static bool s_begin_package;
static uint32_t s_data_bytes;
static uint8_t s_data[2048];
static uint32_t s_last_block_size;
static bool s_sink_ok = true;

static bool recording_sink(void *context, ota_tdma_bridge_op_t operation,
                           const ota_tdma_bridge_begin_t *begin,
                           const uint8_t *data, uint32_t length)
{
    (void)context;
    if (!s_sink_ok) {
        return false;
    }
    switch (operation) {
    case OTA_TDMA_BRIDGE_OP_BEGIN:
        assert(begin != NULL);
        s_begin_calls++;
        s_begin_size = begin->size;
        s_begin_crc32 = begin->crc32;
        s_begin_package = begin->package_mode;
        break;
    case OTA_TDMA_BRIDGE_OP_DATA:
        assert(data != NULL && length <= OTA_TDMA_BRIDGE_BLOCK_MAX);
        s_data_calls++;
        s_last_block_size = length;
        memcpy(&s_data[s_data_bytes], data, length);
        s_data_bytes += length;
        break;
    case OTA_TDMA_BRIDGE_OP_END:
        s_end_calls++;
        break;
    case OTA_TDMA_BRIDGE_OP_ABORT:
        s_abort_calls++;
        break;
    default:
        assert(false);
    }
    return true;
}

static void reset_counters(void)
{
    s_begin_calls = 0u;
    s_data_calls = 0u;
    s_end_calls = 0u;
    s_abort_calls = 0u;
    s_begin_size = 0u;
    s_begin_crc32 = 0u;
    s_begin_package = false;
    s_data_bytes = 0u;
    s_last_block_size = 0u;
    s_sink_ok = true;
    memset(s_data, 0, sizeof(s_data));
}

/* Feeds one block of 64-byte chunks; returns the advanced offset. */
static uint32_t feed_block(ota_tdma_bridge_t *bridge, uint32_t offset,
                           uint32_t size)
{
    uint8_t chunk[64];
    memset(chunk, 0xA5u, sizeof(chunk));
    for (uint32_t sent = 0u; sent < size; sent += sizeof(chunk)) {
        assert(ota_tdma_bridge_data(bridge, offset + sent, chunk,
                                    (uint32_t)sizeof(chunk)));
    }
    return offset + size;
}

static void test_block_staging_and_release(void)
{
    ota_tdma_bridge_t bridge;
    assert(ota_tdma_bridge_init(&bridge, recording_sink, NULL));
    reset_counters();
    assert(ota_tdma_bridge_begin(&bridge, 768u, 0xCAFEBABEu, true));
    assert(s_begin_calls == 1u && s_begin_size == 768u &&
           s_begin_crc32 == 0xCAFEBABEu && s_begin_package);

    uint32_t offset = feed_block(&bridge, 0u, OTA_TDMA_BRIDGE_FIRST_BLOCK);
    assert(offset == 512u);
    assert(s_data_calls == 1u && s_last_block_size == 512u);
    assert(ota_tdma_bridge_block_in_flight(&bridge));
    assert(!ota_tdma_bridge_data(&bridge, offset, (const uint8_t *)"x", 1u));
    assert(ota_tdma_bridge_release(&bridge) == 512u);
    assert(!ota_tdma_bridge_block_in_flight(&bridge));

    offset = feed_block(&bridge, offset, OTA_TDMA_BRIDGE_BLOCK);
    assert(offset == 768u);
    assert(s_data_calls == 2u && s_last_block_size == 256u);
    assert(ota_tdma_bridge_release(&bridge) == 768u);

    assert(ota_tdma_bridge_end(&bridge));
    assert(s_end_calls == 1u);
    assert(ota_tdma_bridge_state(&bridge) == OTA_TDMA_BRIDGE_DONE);
    assert(ota_tdma_bridge_release(&bridge) == 0u); /* nothing in flight */
}

static void test_rejections(void)
{
    ota_tdma_bridge_t bridge;
    assert(!ota_tdma_bridge_init(NULL, recording_sink, NULL));
    assert(!ota_tdma_bridge_init(&bridge, NULL, NULL));
    assert(ota_tdma_bridge_init(&bridge, recording_sink, NULL));
    reset_counters();

    const uint8_t chunk[64] = {0};
    assert(!ota_tdma_bridge_data(&bridge, 0u, chunk, sizeof(chunk)));
    assert(!ota_tdma_bridge_end(&bridge));
    assert(!ota_tdma_bridge_abort(&bridge));
    assert(!ota_tdma_bridge_begin(&bridge, 0u, 0u, true));
    assert(ota_tdma_bridge_begin(&bridge, 256u, 0u, true));
    assert(!ota_tdma_bridge_begin(&bridge, 256u, 0u, true)); /* already open */
    /* A gap in the stream offset is rejected. */
    assert(!ota_tdma_bridge_data(&bridge, 64u, chunk, sizeof(chunk)));
    /* End before the image is complete (and with a partial block). */
    assert(!ota_tdma_bridge_end(&bridge));
    assert(ota_tdma_bridge_state(&bridge) == OTA_TDMA_BRIDGE_RECEIVING);
}

static void test_partial_block_end_rejected(void)
{
    ota_tdma_bridge_t bridge;
    assert(ota_tdma_bridge_init(&bridge, recording_sink, NULL));
    reset_counters();
    /* 64-byte image: the block target is 512, so END must reject leftovers. */
    assert(ota_tdma_bridge_begin(&bridge, 64u, 0u, false));
    const uint8_t chunk[64] = {1u};
    assert(ota_tdma_bridge_data(&bridge, 0u, chunk, sizeof(chunk)));
    assert(!ota_tdma_bridge_end(&bridge));
    assert(ota_tdma_bridge_state(&bridge) == OTA_TDMA_BRIDGE_RECEIVING);
    assert(s_end_calls == 0u);
}

static void test_sink_failure_is_fail_closed(void)
{
    ota_tdma_bridge_t bridge;
    assert(ota_tdma_bridge_init(&bridge, recording_sink, NULL));
    reset_counters();
    assert(ota_tdma_bridge_begin(&bridge, 512u, 0u, true));
    s_sink_ok = false;
    uint8_t chunk[64] = {0};
    bool failed = false;
    for (uint32_t offset = 0u; offset < OTA_TDMA_BRIDGE_FIRST_BLOCK;
         offset += sizeof(chunk)) {
        if (!ota_tdma_bridge_data(&bridge, offset, chunk, sizeof(chunk))) {
            failed = true;
            break;
        }
    }
    assert(failed);
    assert(ota_tdma_bridge_state(&bridge) == OTA_TDMA_BRIDGE_FAILED);
}

static void test_abort(void)
{
    ota_tdma_bridge_t bridge;
    assert(ota_tdma_bridge_init(&bridge, recording_sink, NULL));
    reset_counters();
    assert(ota_tdma_bridge_begin(&bridge, 512u, 0u, true));
    assert(ota_tdma_bridge_abort(&bridge));
    assert(s_abort_calls == 1u);
    assert(ota_tdma_bridge_state(&bridge) == OTA_TDMA_BRIDGE_IDLE);
    assert(ota_tdma_bridge_received(&bridge) == 0u);
    assert(!ota_tdma_bridge_abort(&bridge)); /* already idle */
}

static void test_block_bound(void)
{
    ota_tdma_bridge_t bridge;
    assert(ota_tdma_bridge_init(&bridge, recording_sink, NULL));
    reset_counters();
    assert(ota_tdma_bridge_begin(&bridge, OTA_TDMA_BRIDGE_FIRST_BLOCK + 64u, 0u,
                                 true));
    uint8_t chunk[OTA_TDMA_BRIDGE_BLOCK_MAX + 1u];
    /* A single frame larger than the staging block is rejected. */
    assert(!ota_tdma_bridge_data(&bridge, 0u, chunk, sizeof(chunk)));
}

int main(void)
{
    test_block_staging_and_release();
    test_rejections();
    test_partial_block_end_rejected();
    test_sink_failure_is_fail_closed();
    test_abort();
    test_block_bound();
    printf("OTA TDMA bridge host unit tests passed\n");
    return 0;
}
