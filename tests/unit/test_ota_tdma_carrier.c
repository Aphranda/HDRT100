#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_carrier.h"
#include "ota_tdma_codec.h"
#include "portable_ota_port.h"


/* The carrier links the ingress/session units; the source-role test never
 * reaches the port, so these are inert stubs. */
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

static unsigned s_submit_calls;
static uint8_t s_last_frame[OTA_TDMA_FRAME_MAX];
static size_t s_last_len;
static uint8_t s_last_stream;
static uint8_t s_last_generation;
static uint8_t s_last_mask;

static bool submit_stub(const uint8_t *frame, size_t frame_size,
                        uint8_t stream_id, uint8_t generation,
                        uint8_t target_mask)
{
    s_submit_calls++;
    memcpy(s_last_frame, frame, frame_size);
    s_last_len = frame_size;
    s_last_stream = stream_id;
    s_last_generation = generation;
    s_last_mask = target_mask;
    return true;
}

static uint8_t s_rx[OTA_TDMA_FRAME_MAX];
static size_t s_rx_len;
static uint8_t s_rx_stream;
static uint8_t s_rx_generation;
static bool s_rx_ready;

static bool take_stub(uint8_t *frame, size_t frame_capacity, size_t *frame_size,
                      uint8_t *stream_id, uint8_t *generation)
{
    if (!s_rx_ready || s_rx_len > frame_capacity) {
        return false;
    }
    memcpy(frame, s_rx, s_rx_len);
    *frame_size = s_rx_len;
    *stream_id = s_rx_stream;
    *generation = s_rx_generation;
    s_rx_ready = false;
    return true;
}

static unsigned s_ack_calls;
static bool s_last_ack_ok;
static uint8_t s_last_ack_source;

static bool ack_stub(uint8_t stream_id, uint8_t generation, uint8_t source_slot,
                     bool ok)
{
    (void)stream_id;
    (void)generation;
    s_ack_calls++;
    s_last_ack_ok = ok;
    s_last_ack_source = source_slot;
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

int main(void)
{
    ota_tdma_carrier_t carrier;
    assert(ota_tdma_carrier_init(&carrier, submit_stub, take_stub, ack_stub,
                                 0x4Fu, 0x03u, 1u));
    const uint8_t frame[] = {0x03u, 0x00u, 0x00u, 0x01u, 0, 0, 0, 0, 0, 0};
    assert(ota_tdma_carrier_send(&carrier, frame, sizeof(frame)));
    assert(s_submit_calls == 1u && s_last_len == sizeof(frame));
    assert(s_last_stream == 0x4Fu && s_last_mask == 0x03u);
    assert(s_last_generation == 1u);
    assert(ota_tdma_carrier_send(&carrier, frame, sizeof(frame)));
    assert(s_last_generation == 2u);
    assert(!ota_tdma_carrier_send(&carrier, NULL, 4u));
    assert(!ota_tdma_carrier_send(&carrier, frame, 0u));
    uint8_t oversized[OTA_TDMA_FRAME_MAX + 1u] = {0};
    assert(!ota_tdma_carrier_send(&carrier, oversized, sizeof(oversized)));

    /* Fresh carrier so the session's three submits start at generation 1. */
    assert(ota_tdma_carrier_init(&carrier, submit_stub, take_stub, ack_stub,
                                 0x4Fu, 0x03u, 1u));

    /* Source session driven through the carrier; a queued ACK advances it. */
    uint8_t image[20];
    for (size_t i = 0u; i < sizeof(image); ++i) {
        image[i] = (uint8_t)i;
    }
    uint8_t open_wire[88];
    memset(open_wire, 0, sizeof(open_wire));
    ota_tdma_session_t source;
    const ota_tdma_transport_t transport = {.send = ota_tdma_carrier_send,
                                            .context = &carrier};
    assert(ota_tdma_session_init_source(&source, &transport, open_wire, image,
                                        sizeof(image),
                                        test_crc(image, sizeof(image)), 8u, 3u,
                                        100u, test_crc));
    s_submit_calls = 0u;
    assert(ota_tdma_session_start(&source, 0u));
    /* start() submits the OPEN fragments only. */
    assert(s_submit_calls == 2u);
    assert(ota_tdma_session_resume(&source, 0u));
    /* OPEN x2 plus the whole first ACK block (20 B in 8/8/4 frames). */
    assert(s_submit_calls == 5u);
    assert(s_last_generation == 5u);

    /* Queue the durable ACK for the first DATA frame (8 bytes). */
    uint8_t ack[OTA_TDMA_FRAME_MAX];
    const size_t ack_len =
        ota_tdma_encode_ack(ack, sizeof(ack), 20u, 0u, 0u);
    memcpy(s_rx, ack, ack_len);
    s_rx_len = ack_len;
    s_rx_stream = 0x4Fu;
    s_rx_generation = 2u;
    s_rx_ready = true;
    assert(ota_tdma_carrier_poll(&carrier, &source, 1u));
    assert(s_ack_calls == 1u && s_last_ack_ok && s_last_ack_source == 1u);

    /* Malformed frame fails the session and NACKs the runtime frame. */
    const uint8_t bad[] = {0x09u, 0x00u, 0x00u, 0x01u};
    memcpy(s_rx, bad, sizeof(bad));
    s_rx_len = sizeof(bad);
    s_rx_stream = 0x4Fu;
    s_rx_generation = 3u;
    s_rx_ready = true;
    assert(!ota_tdma_carrier_poll(&carrier, &source, 2u));
    assert(s_ack_calls == 2u && !s_last_ack_ok);

    /* Bounded service loop drains several queued ACKs in one call. */
    {
        ota_tdma_carrier_t svc_carrier;
        assert(ota_tdma_carrier_init(&svc_carrier, submit_stub, take_stub,
                                     ack_stub, 0x4Fu, 0x03u, 1u));
        ota_tdma_session_t svc_source;
        const ota_tdma_transport_t svc_transport = {.send = ota_tdma_carrier_send,
                                                    .context = &svc_carrier};
        assert(ota_tdma_session_init_source(&svc_source, &svc_transport,
                                            open_wire, image, sizeof(image),
                                            test_crc(image, sizeof(image)), 8u,
                                            3u, 100u, test_crc));
        assert(ota_tdma_session_start(&svc_source, 0u));
        assert(ota_tdma_session_resume(&svc_source, 0u));
        /* Queue the first two durable ACKs (8 and 16 bytes). */
        static uint8_t rx_backup[OTA_TDMA_FRAME_MAX];
        (void)rx_backup;
        s_ack_calls = 0u;
        uint8_t first_ack[OTA_TDMA_FRAME_MAX];
        const size_t first_len =
            ota_tdma_encode_ack(first_ack, sizeof(first_ack), 20u, 0u, 0u);
        memcpy(s_rx, first_ack, first_len);
        s_rx_len = first_len;
        s_rx_stream = 0x4Fu;
        s_rx_generation = 5u;
        s_rx_ready = true;
        assert(ota_tdma_carrier_service(&svc_carrier, &svc_source, 3u, 1u));
        assert(s_ack_calls == 1u);
        /* The source now waits for the CLOSE confirmation. */
        assert(ota_tdma_session_state(&svc_source) !=
               OTA_TDMA_SESSION_DONE);
        s_ack_calls = 0u;
        memcpy(s_rx, first_ack, first_len);
        s_rx_len = first_len;
        s_rx_ready = true;
        assert(ota_tdma_carrier_service(&svc_carrier, &svc_source, 4u, 1u));
        assert(s_ack_calls == 1u);
        assert(ota_tdma_session_state(&svc_source) ==
               OTA_TDMA_SESSION_DONE);
    }

    printf("OTA TDMA carrier host unit tests passed\n");
    return 0;
}
