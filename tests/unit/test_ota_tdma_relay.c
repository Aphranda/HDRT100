#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"
#include "ota_tdma_relay.h"
#include "portable_ota_port.h"

/* Fake ring: every DATA frame is acknowledged immediately (durable = end). */

#define QUEUE_DEPTH 128u
typedef struct {
    uint8_t data[OTA_TDMA_FRAME_MAX];
    size_t length;
} message_t;


/* Source-role test: the ingress unit is linked but never reached. */
uint32_t portable_ota_port_crc32_compute(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc = (crc >> 8u) ^ data[i];
    }
    return crc;
}

pota_stream_ingress_result_t portable_ota_port_stream_abort(
    pota_stream_ingress_source_t source)
{
    (void)source;
    return POTA_STREAM_INGRESS_OK;
}

bool portable_ota_port_stream_force_reset(void)
{
    return true;
}

uint32_t portable_ota_port_stream_last_session_result(void)
{
    return 0u;
}

pota_stream_ingress_result_t portable_ota_port_stream_open(
    pota_stream_ingress_source_t source, const pota_stream_open_t *open)
{
    (void)source;
    (void)open;
    return POTA_STREAM_INGRESS_OK;
}

pota_stream_ingress_result_t portable_ota_port_stream_write(
    pota_stream_ingress_source_t source, uint32_t offset, const uint8_t *data,
    uint32_t size, bool has_crc32, uint32_t crc32)
{
    (void)source; (void)offset; (void)data; (void)size;
    (void)has_crc32; (void)crc32;
    return POTA_STREAM_INGRESS_OK;
}

pota_stream_ingress_result_t portable_ota_port_stream_close(
    pota_stream_ingress_source_t source)
{
    (void)source;
    return POTA_STREAM_INGRESS_OK;
}

bool portable_ota_port_stream_get_status(pota_stream_ingress_status_t *status)
{
    (void)status;
    return false;
}

static message_t s_queue[QUEUE_DEPTH];
static unsigned s_head;
static unsigned s_tail;
static unsigned s_written;
static uint32_t s_close_offset;
static uint32_t s_now_ms;

static bool send_fn(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    assert(s_tail - s_head < QUEUE_DEPTH);
    message_t *message = &s_queue[s_tail % QUEUE_DEPTH];
    memcpy(message->data, frame, length);
    message->length = length;
    s_tail++;
    return true;
}

static uint32_t test_crc(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < length; ++i) {
        crc = (crc >> 8u) ^ data[i];
    }
    return crc;
}

/* Pops one frame and, for DATA frames, queues the matching ACK. */
static bool deliver_one(ota_tdma_relay_t *relay)
{
    if (s_head == s_tail) {
        return false;
    }
    const message_t message = s_queue[s_head % QUEUE_DEPTH];
    s_head++;
    ota_tdma_frame_t frame;
    assert(ota_tdma_decode(&frame, message.data, message.length));
    if (frame.type == (uint8_t)OTA_TDMA_FRAME_DATA) {
        s_written += frame.payload_len;
        const uint32_t end = frame.offset + frame.payload_len;
        assert(ota_tdma_relay_on_frame(relay, 0u, message.data, message.length));
        /* The pump waits per 512-byte header block and then per 256-byte
         * page, so the fake ring acknowledges only at those boundaries. */
        if (end == 512u || (end > 512u && ((end - 512u) % 256u) == 0u)) {
            uint8_t ack[OTA_TDMA_FRAME_MAX];
            const size_t ack_len =
                ota_tdma_encode_ack(ack, sizeof(ack), end, 0u, 0u);
            if (!ota_tdma_relay_on_frame(relay, 0u, ack, ack_len)) {
                fprintf(stderr,
                        "ack %u rejected: state=%d pump=%d pfail=%d sent=(%u,+%u) dur=%u\n",
                        end, (int)ota_tdma_session_state(&relay->session),
                        (int)relay->session.pump.state,
                        (int)relay->session.pump.failure,
                        relay->session.pump.sent_offset,
                        relay->session.pump.sent_length,
                        relay->session.pump.durable_offset);
            }
        }
        return true;
    }
    if (frame.type == (uint8_t)OTA_TDMA_FRAME_CLOSE) {
        s_close_offset = frame.offset;
        (void)ota_tdma_relay_on_frame(relay, 0u, message.data, message.length);
        uint8_t ack[OTA_TDMA_FRAME_MAX];
        const size_t ack_len =
            ota_tdma_encode_ack(ack, sizeof(ack), frame.offset, 0u, 0u);
        (void)ota_tdma_relay_on_frame(relay, 0u, ack, ack_len);
        return false;
    }
    return true;
}

int main(void)
{
    uint8_t open_wire[88];
    memset(open_wire, 0, sizeof(open_wire));
    open_wire[0] = 1u; /* session id */
    open_wire[8] = 3u; /* capability mask */

    const uint32_t block = 512u;
    const uint32_t total = block + 256u * 2u;
    ota_tdma_relay_t relay;
    const ota_tdma_transport_t transport = {.send = send_fn, .context = NULL};
    assert(ota_tdma_relay_open(&relay, &transport, open_wire, total, 0u, 64u,
                               3u, 100u, test_crc));
    assert(ota_tdma_relay_active(&relay));

    uint8_t block_data[512];
    for (uint32_t index = 0u; index < 3u; ++index) {
        const uint32_t size = index == 0u ? 512u : 256u;
        for (uint32_t i = 0u; i < size; ++i) {
            block_data[i] = (uint8_t)(index * 31u + i);
        }
        /* The previous block must be durable before the next feed. */
        assert(ota_tdma_relay_feed(&relay, index == 0u ? 0u : 512u + (index - 1u) * 256u, block_data, size));
        /* The relay holds DATA back for a 600 ms OPEN-only grace window, so the
         * pump only starts after the clock crosses it. */
        const uint32_t expected_durable =
            index == 0u ? 512u : 512u + index * 256u;
        for (unsigned round = 0u; round < 16u; ++round) {
            assert(ota_tdma_relay_tick(&relay, s_now_ms));
            unsigned guard = 0u;
            while (s_head != s_tail && guard++ < 64u) {
                (void)deliver_one(&relay);
            }
            if (ota_tdma_relay_durable(&relay) == expected_durable) {
                break;
            }
            s_now_ms += 100u;
        }
        if (ota_tdma_relay_durable(&relay) !=
            (index == 0u ? 512u : 512u + index * 256u)) {
            fprintf(stderr,
                    "block %u durable=%u session=%d pump=%d sent=(%u,+%u)\n",
                    index, ota_tdma_relay_durable(&relay),
                    (int)ota_tdma_session_state(&relay.session),
                    (int)relay.session.pump.state,
                    relay.session.pump.sent_offset, relay.session.pump.sent_length);
        }
        assert(ota_tdma_relay_durable(&relay) ==
               (index == 0u ? 512u : 512u + index * 256u));
    }

    assert(ota_tdma_relay_done(&relay));
    assert(!ota_tdma_relay_active(&relay));
    assert(s_written == total);
    assert(s_close_offset == total);

    /* A non-contiguous or overlapping block is refused. */
    ota_tdma_relay_t second;
    assert(ota_tdma_relay_open(&second, &transport, open_wire, block, 0u, 64u,
                               3u, 100u, test_crc));
    assert(ota_tdma_relay_feed(&second, 0u, block_data, block));
    assert(!ota_tdma_relay_feed(&second, block, block_data, block));

    /* Regression: the relay must own a copy of the OPEN descriptor.  The
     * production caller builds it on the SCPI handler's stack and relay_tick()
     * runs later from the AO service stack, so a stored pointer would emit
     * whatever later reused that frame.  On the bench this surfaced as the
     * target decoding an all-zero OPEN and rejecting the relay with MISMATCH. */
    {
        uint8_t scratch_wire[88];
        memset(scratch_wire, 0, sizeof(scratch_wire));
        scratch_wire[0] = 0x5Au;  /* session id */
        scratch_wire[8] = 0x03u;  /* capability mask */
        scratch_wire[12] = 0x02u; /* map version */
        ota_tdma_relay_t lifetime;
        assert(ota_tdma_relay_open(&lifetime, &transport, scratch_wire, block,
                                   0u, 64u, 3u, 100u, test_crc));
        /* The caller's frame is gone before the first OPEN is transmitted. */
        memset(scratch_wire, 0xFF, sizeof(scratch_wire));
        s_head = s_tail; /* drop frames queued by the earlier flow */
        assert(ota_tdma_relay_feed(&lifetime, 0u, block_data, block));
        assert(ota_tdma_relay_tick(&lifetime, 0u));
        assert(s_head != s_tail);
        const message_t *first = &s_queue[s_head % QUEUE_DEPTH];
        ota_tdma_frame_t open_frame;
        assert(ota_tdma_decode(&open_frame, first->data, first->length));
        assert(open_frame.type == (uint8_t)OTA_TDMA_FRAME_OPEN);
        assert(open_frame.payload_len > 12u);
        assert(open_frame.payload[0] == 0x5Au);
        assert(open_frame.payload[8] == 0x03u);
        assert(open_frame.payload[12] == 0x02u);
    }

    printf("OTA TDMA relay host unit tests passed\n");
    return 0;
}
