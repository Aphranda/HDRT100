#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"
#include "ota_tdma_v2_adapter.h"

static unsigned s_begin_calls;
static unsigned s_data_calls;
static unsigned s_end_calls;
static unsigned s_abort_calls;
static uint32_t s_begin_size;
static bool s_begin_package;
static uint32_t s_data_bytes;
static uint8_t s_data[1024];
static unsigned s_ack_calls;
static uint32_t s_ack_offset;
static uint8_t s_ack_result;
static bool s_sink_ok = true;
static bool s_ready = true;

static bool ready_stub(void *context)
{
    (void)context;
    return s_ready;
}

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
        s_begin_calls++;
        s_begin_size = begin->size;
        s_begin_package = begin->package_mode;
        break;
    case OTA_TDMA_BRIDGE_OP_DATA:
        assert(length <= OTA_TDMA_BRIDGE_BLOCK_MAX);
        s_data_calls++;
        assert(length <= sizeof(s_data) - s_data_bytes);
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

static bool capture_send(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    ota_tdma_frame_t decoded;
    assert(ota_tdma_decode(&decoded, frame, length));
    assert(decoded.type == (uint8_t)OTA_TDMA_FRAME_ACK);
    s_ack_calls++;
    s_ack_offset = decoded.offset;
    s_ack_result = decoded.result;
    return true;
}

static void reset_counters(void)
{
    s_begin_calls = 0u;
    s_data_calls = 0u;
    s_end_calls = 0u;
    s_abort_calls = 0u;
    s_begin_size = 0u;
    s_begin_package = false;
    s_data_bytes = 0u;
    s_ack_calls = 0u;
    s_ack_offset = 0u;
    s_ack_result = 0xFFu;
    s_sink_ok = true;
    s_ready = true;
    memset(s_data, 0, sizeof(s_data));
}

static void put_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8u) & 0xFFu);
    out[2] = (uint8_t)((value >> 16u) & 0xFFu);
    out[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void build_open_wire(uint8_t *wire, uint32_t size)
{
    memset(wire, 0, 88u);
    put_le32(&wire[0], 1u);   /* session */
    put_le32(&wire[4], 1u);   /* generation */
    put_le32(&wire[8], 3u);   /* capabilities: inactive write + durable ack */
    put_le32(&wire[12], 1u);  /* map version */
    put_le32(&wire[16], 2u);  /* partition id */
    put_le32(&wire[20], 1u);  /* destination slot */
    put_le32(&wire[24], 5u);  /* object id */
    put_le32(&wire[28], size);
    put_le32(&wire[32], 0x12345678u);
    wire[36] = 1u;            /* package mode */
}

static bool init_adapter(ota_tdma_v2_adapter_t *adapter)
{
    return ota_tdma_v2_adapter_init(adapter, recording_sink, NULL, capture_send,
                                    NULL, ready_stub, NULL);
}

static bool feed_open(ota_tdma_v2_adapter_t *adapter, const uint8_t *wire)
{
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    for (uint8_t seq = 0u; seq < 2u; ++seq) {
        const size_t len = ota_tdma_encode_open(frame, sizeof(frame), wire, 88u,
                                                seq, 2u);
        assert(len != 0u);
        if (!ota_tdma_v2_adapter_frame(adapter, frame, len)) {
            return false;
        }
    }
    return true;
}

static bool feed_data(ota_tdma_v2_adapter_t *adapter, uint32_t offset,
                      const uint8_t *payload, uint32_t length)
{
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    const size_t len = ota_tdma_encode_data(frame, sizeof(frame), offset, 0u,
                                            payload, length, false);
    assert(len != 0u);
    return ota_tdma_v2_adapter_frame(adapter, frame, len);
}

static uint32_t feed_block(ota_tdma_v2_adapter_t *adapter, uint32_t offset,
                           uint32_t size)
{
    uint8_t chunk[64];
    memset(chunk, 0x5Au, sizeof(chunk));
    for (uint32_t sent = 0u; sent < size; sent += sizeof(chunk)) {
        const uint32_t length = (size - sent) < sizeof(chunk)
                                    ? (size - sent)
                                    : (uint32_t)sizeof(chunk);
        assert(feed_data(adapter, offset + sent, chunk, length));
    }
    return offset + size;
}

static void test_full_flow(void)
{
    ota_tdma_v2_adapter_t adapter;
    uint8_t wire[88];
    build_open_wire(wire, 768u);
    reset_counters();
    assert(init_adapter(&adapter));
    assert(feed_open(&adapter, wire));
    assert(s_begin_calls == 1u && s_begin_size == 768u && s_begin_package);
    /* Opening handshake ACK(0) starts the source pump. */
    assert(s_ack_calls == 1u && s_ack_offset == 0u);

    uint32_t offset = feed_block(&adapter, 0u, OTA_TDMA_BRIDGE_FIRST_BLOCK);
    assert(s_data_calls == 1u && s_data_bytes == 512u);
    assert(s_ack_calls == 1u); /* block ACK only after the AO consumed it */

    assert(feed_data(&adapter, offset, (const uint8_t *)"x", 1u));
    assert(s_data_calls == 1u);

    assert(ota_tdma_v2_adapter_release(&adapter));
    assert(s_ack_calls == 2u && s_ack_offset == 512u && s_ack_result == 0u);

    offset = feed_block(&adapter, offset, OTA_TDMA_BRIDGE_BLOCK);
    assert(offset == 768u);
    assert(s_data_calls == 2u && s_data_bytes == 768u);
    assert(ota_tdma_v2_adapter_release(&adapter));
    assert(s_ack_calls == 3u && s_ack_offset == 768u);

    uint8_t close[OTA_TDMA_FRAME_MAX];
    const size_t close_len = ota_tdma_encode_close(close, sizeof(close), 768u,
                                                   0x12345678u);
    assert(close_len != 0u);
    assert(ota_tdma_v2_adapter_frame(&adapter, close, close_len));
    assert(s_end_calls == 1u);
    assert(s_ack_calls == 4u && s_ack_offset == 768u);
    assert(ota_tdma_bridge_state(&adapter.bridge) == OTA_TDMA_BRIDGE_DONE);
}

static void test_data_staged_before_receiving(void)
{
    ota_tdma_v2_adapter_t adapter;
    uint8_t wire[88];
    build_open_wire(wire, 512u);
    reset_counters();
    assert(init_adapter(&adapter));
    assert(feed_open(&adapter, wire));
    /* The AB pipeline is still erasing: the first block is still staged and
     * handed to the AO, which consumes the event once it reaches RECEIVING.
     * Dropping the frames here made the first block impossible to complete. */
    s_ready = false;
    assert(feed_block(&adapter, 0u, OTA_TDMA_BRIDGE_FIRST_BLOCK) == 512u);
    assert(s_data_calls == 1u);
    assert(s_data_bytes == OTA_TDMA_BRIDGE_FIRST_BLOCK);
    assert(adapter.deferred_data >= 1u);
    assert(ota_tdma_bridge_state(&adapter.bridge) == OTA_TDMA_BRIDGE_RECEIVING);
    assert(ota_tdma_bridge_block_in_flight(&adapter.bridge));
    /* A retransmission while the block is in flight is backpressure. */
    assert(feed_block(&adapter, 0u, OTA_TDMA_BRIDGE_FIRST_BLOCK) == 512u);
    assert(s_data_calls == 1u);
}

static void test_duplicate_and_abort(void)
{
    ota_tdma_v2_adapter_t adapter;
    uint8_t wire[88];
    build_open_wire(wire, 512u);
    reset_counters();
    assert(init_adapter(&adapter));
    assert(feed_open(&adapter, wire));
    assert(feed_block(&adapter, 0u, OTA_TDMA_BRIDGE_FIRST_BLOCK) == 512u);
    assert(s_data_calls == 1u);
    assert(ota_tdma_v2_adapter_release(&adapter));
    /* A retransmission of the block already handed to the AO is ignored. */
    assert(feed_block(&adapter, 0u, OTA_TDMA_BRIDGE_FIRST_BLOCK) == 512u);
    assert(s_data_calls == 1u);

    uint8_t abort_frame[OTA_TDMA_FRAME_MAX];
    const size_t abort_len = ota_tdma_encode_abort(abort_frame,
                                                   sizeof(abort_frame), 2u);
    assert(abort_len != 0u);
    assert(ota_tdma_v2_adapter_frame(&adapter, abort_frame, abort_len));
    assert(s_abort_calls == 1u);
    assert(ota_tdma_bridge_state(&adapter.bridge) == OTA_TDMA_BRIDGE_IDLE);
}

static void test_sink_failure_is_fatal(void)
{
    ota_tdma_v2_adapter_t adapter;
    uint8_t wire[88];
    build_open_wire(wire, 512u);
    reset_counters();
    assert(init_adapter(&adapter));
    assert(feed_open(&adapter, wire));
    s_sink_ok = false;
    uint8_t chunk[64] = {0};
    bool failed = false;
    for (uint32_t offset = 0u; offset < OTA_TDMA_BRIDGE_FIRST_BLOCK;
         offset += sizeof(chunk)) {
        if (!feed_data(&adapter, offset, chunk, sizeof(chunk))) {
            failed = true;
            break;
        }
    }
    assert(failed);
    assert(ota_tdma_bridge_state(&adapter.bridge) == OTA_TDMA_BRIDGE_FAILED);
}

static void test_rejects(void)
{
    ota_tdma_v2_adapter_t adapter;
    reset_counters();
    assert(!ota_tdma_v2_adapter_init(NULL, recording_sink, NULL, capture_send,
                                     NULL, ready_stub, NULL));
    assert(!ota_tdma_v2_adapter_init(&adapter, NULL, NULL, capture_send, NULL,
                                     ready_stub, NULL));
    assert(!ota_tdma_v2_adapter_init(&adapter, recording_sink, NULL, NULL, NULL,
                                     ready_stub, NULL));
    assert(init_adapter(&adapter));
    uint8_t bogus[8] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0u, 0u, 0u, 0u};
    assert(!ota_tdma_v2_adapter_frame(&adapter, bogus, sizeof(bogus)));
    /* DATA without an OPEN is a protocol violation. */
    const uint8_t block[4] = {0};
    assert(!feed_data(&adapter, 0u, block, sizeof(block)));
}

int main(void)
{
    test_full_flow();
    test_data_staged_before_receiving();
    test_duplicate_and_abort();
    test_sink_failure_is_fatal();
    test_rejects();
    printf("OTA TDMA v2 adapter host unit tests passed\n");
    return 0;
}
