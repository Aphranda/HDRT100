#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"
#include "ota_tdma_session.h"
#include "portable_ota_port.h"

/* Two role-aware sessions connected by a frame queue. ---------------------- */

#define QUEUE_DEPTH 64u
typedef struct {
    uint8_t data[OTA_TDMA_FRAME_MAX];
    size_t length;
    bool to_target;
} message_t;

static message_t s_queue[QUEUE_DEPTH];
static unsigned s_head;
static unsigned s_tail;

static uint8_t s_written[64];
static uint32_t s_written_size;
static uint32_t s_durable;


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
    (void)source;
    (void)has_crc32;
    (void)crc32;
    if (offset + size > sizeof(s_written)) {
        return POTA_STREAM_INGRESS_FRAME_TOO_LARGE;
    }
    memcpy(&s_written[offset], data, size);
    if (offset + size > s_written_size) {
        s_written_size = offset + size;
    }
    s_durable = offset + size;
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
    status->source = POTA_STREAM_INGRESS_TDMA;
    status->state = POTA_STREAM_STATE_RECEIVING;
    status->durable_offset = s_durable;
    status->stream_token = 0u;
    status->last_result = POTA_STREAM_INGRESS_OK;
    return true;
}

static bool push(const uint8_t *frame, size_t length, bool to_target)
{
    assert(s_tail - s_head < QUEUE_DEPTH);
    message_t *message = &s_queue[s_tail % QUEUE_DEPTH];
    memcpy(message->data, frame, length);
    message->length = length;
    message->to_target = to_target;
    s_tail++;
    return true;
}

static bool source_send(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    return push(frame, length, true);
}

static bool target_send(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    return push(frame, length, false);
}

static uint32_t test_crc(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < length; ++i) {
        crc = (crc >> 8u) ^ data[i];
    }
    return crc;
}

static void put_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8u) & 0xFFu);
    out[2] = (uint8_t)((value >> 16u) & 0xFFu);
    out[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void build_open(uint8_t *wire, uint32_t size, uint32_t crc)
{
    memset(wire, 0, 88u);
    put_le32(&wire[0], 0x1234u);      /* session id */
    put_le32(&wire[4], 7u);           /* generation */
    put_le32(&wire[8], 3u);           /* capability mask */
    put_le32(&wire[12], 1u);          /* map version */
    put_le32(&wire[16], 2u);          /* partition */
    put_le32(&wire[20], 1u);          /* slot */
    put_le32(&wire[24], 5u);          /* object */
    put_le32(&wire[28], size);
    put_le32(&wire[32], crc);
    wire[36] = 0u;                    /* raw image, not package */
}

int main(void)
{
    uint8_t image[20];
    for (size_t i = 0u; i < sizeof(image); ++i) {
        image[i] = (uint8_t)(0x80u + i);
    }
    uint8_t open_wire[88];
    build_open(open_wire, sizeof(image), test_crc(image, sizeof(image)));

    ota_tdma_session_t source;
    ota_tdma_session_t target;
    const ota_tdma_transport_t source_transport = {.send = source_send,
                                                   .context = NULL};
    const ota_tdma_transport_t target_transport = {.send = target_send,
                                                   .context = NULL};
    assert(ota_tdma_session_init_source(&source, &source_transport, open_wire,
                                        image, sizeof(image),
                                        test_crc(image, sizeof(image)), 8u, 3u,
                                        100u, test_crc));
    assert(ota_tdma_session_init_target(&target, &target_transport));
    assert(ota_tdma_session_start(&source, 0u));
    /* start() emits OPEN only; the relay/grace owner starts the pump. */
    assert(ota_tdma_session_resume(&source, 0u));

    /* Pump the queue until both roles settle; no recursion involved. */
    unsigned guard = 0u;
    while (s_head != s_tail && guard++ < 128u) {
        const message_t message = s_queue[s_head % QUEUE_DEPTH];
        s_head++;
        if (message.to_target) {
            (void)ota_tdma_session_on_frame(&target, 0u, message.data,
                                            message.length);
        } else {
            (void)ota_tdma_session_on_frame(&source, 0u, message.data,
                                            message.length);
        }
    }
    assert(s_head == s_tail);
    assert(ota_tdma_session_state(&source) == OTA_TDMA_SESSION_DONE);
    assert(ota_tdma_session_state(&target) == OTA_TDMA_SESSION_DONE);
    assert(s_written_size == sizeof(image));
    assert(memcmp(s_written, image, sizeof(image)) == 0);

    /* DATA before OPEN is ignored, not fatal: the maintenance window reorders
     * frames and the source retransmits OPEN. */
    {
        ota_tdma_session_t target2;
        assert(ota_tdma_session_init_target(&target2, &target_transport));
        uint8_t wire[OTA_TDMA_FRAME_MAX];
        const size_t len = ota_tdma_encode_data(wire, sizeof(wire), 0u, 0u,
                                                image, 4u, false);
        assert(len != 0u);
        assert(ota_tdma_session_on_frame(&target2, 0u, wire, len));
        assert(ota_tdma_session_state(&target2) == OTA_TDMA_SESSION_IDLE);
        assert(ota_tdma_session_ignored_opens(&target2) == 1u);
    }

    /* ABORT on the source fails the session as rejected. */
    {
        ota_tdma_session_t source2;
        assert(ota_tdma_session_init_source(&source2, &source_transport,
                                            open_wire, image, sizeof(image),
                                            0u, 8u, 3u, 100u, test_crc));
        uint8_t wire[OTA_TDMA_FRAME_MAX];
        const size_t len = ota_tdma_encode_abort(wire, sizeof(wire), 2u);
        assert(len != 0u);
        assert(!ota_tdma_session_on_frame(&source2, 0u, wire, len));
        assert(ota_tdma_session_failure(&source2) ==
               OTA_TDMA_SESSION_FAIL_REJECTED);
    }

    /* Relay routing: OPEN carries CAP_RELAY + relay_node. */
    {
        uint8_t relay_wire[88];
        memcpy(relay_wire, open_wire, sizeof(relay_wire));
        const uint32_t cap = 3u | 4u; /* INACTIVE_WRITE | DURABLE_ACK | RELAY */
        put_le32(&relay_wire[8], cap);
        relay_wire[37] = 0u; /* node 0 */

        pota_stream_open_t decoded;
        assert(pota_stream_open_decode_le(relay_wire, sizeof(relay_wire),
                                          &decoded));
        assert((decoded.capability_mask & 4u) != 0u && decoded.relay_node == 0u);

        /* Reserved byte without the capability must be rejected. */
        uint8_t bad[88];
        memcpy(bad, open_wire, sizeof(bad));
        bad[37] = 1u;
        assert(!pota_stream_open_decode_le(bad, sizeof(bad), &decoded));

        /* Node 0 OPEN is ignored by node 1 and accepted by node 0. */
        uint8_t frame0[OTA_TDMA_FRAME_MAX];
        uint8_t frame1[OTA_TDMA_FRAME_MAX];
        ota_tdma_session_t other;
        assert(ota_tdma_session_init_target_for_node(&other,
                                                     &target_transport, 1u));
        const size_t len0 = ota_tdma_encode_open(frame0, sizeof(frame0),
                                                 relay_wire, 88u, 0u, 2u);
        const size_t len1 = ota_tdma_encode_open(frame1, sizeof(frame1),
                                                 relay_wire, 88u, 1u, 2u);
        assert(len0 != 0u && len1 != 0u);
        assert(ota_tdma_session_on_frame(&other, 0u, frame0, len0));
        assert(ota_tdma_session_on_frame(&other, 0u, frame1, len1));
        assert(ota_tdma_session_state(&other) == OTA_TDMA_SESSION_IDLE);
        assert(ota_tdma_session_ignored_opens(&other) == 1u);

        ota_tdma_session_t owner;
        assert(ota_tdma_session_init_target_for_node(&owner,
                                                     &target_transport, 0u));
        const size_t own0 = ota_tdma_encode_open(frame0, sizeof(frame0),
                                                 relay_wire, 88u, 0u, 2u);
        const size_t own1 = ota_tdma_encode_open(frame1, sizeof(frame1),
                                                 relay_wire, 88u, 1u, 2u);
        assert(own0 != 0u && own1 != 0u);
        assert(ota_tdma_session_on_frame(&owner, 0u, frame0, own0));
        assert(ota_tdma_session_on_frame(&owner, 0u, frame1, own1));
        assert(ota_tdma_session_state(&owner) == OTA_TDMA_SESSION_RUNNING);
    }

    printf("OTA TDMA session host unit tests passed\n");
    return 0;
}
