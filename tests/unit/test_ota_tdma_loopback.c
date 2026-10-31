#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"
#include "ota_tdma_ingress.h"
#include "ota_tdma_pump.h"
#include "portable_ota_port.h"

/* Loopback: pump → wire frame → ingress → fake session → ACK → pump. */

static uint8_t s_target_image[64];
static uint32_t s_target_size;
static uint32_t s_durable;
static pota_stream_state_t s_state = POTA_STREAM_STATE_RECEIVING;
static unsigned s_write_calls;
static unsigned s_close_calls;

static ota_tdma_pump_t s_pump;
static ota_tdma_ingress_t s_ingress;


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
    if (offset + size > sizeof(s_target_image)) {
        return POTA_STREAM_INGRESS_FRAME_TOO_LARGE;
    }
    memcpy(&s_target_image[offset], data, size);
    if (offset + size > s_target_size) {
        s_target_size = offset + size;
    }
    s_durable = offset + size;
    s_write_calls++;
    return POTA_STREAM_INGRESS_OK;
}

pota_stream_ingress_result_t portable_ota_port_stream_close(
    pota_stream_ingress_source_t source)
{
    (void)source;
    s_close_calls++;
    s_state = POTA_STREAM_STATE_READY_TO_REBOOT;
    return POTA_STREAM_INGRESS_OK;
}

bool portable_ota_port_stream_get_status(pota_stream_ingress_status_t *status)
{
    status->source = POTA_STREAM_INGRESS_TDMA;
    status->state = s_state;
    status->durable_offset = s_durable;
    status->stream_token = 0u;
    status->last_result = POTA_STREAM_INGRESS_OK;
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

/* Source send callback: deliver the frame to the target side.  It must not
 * call back into the pump (the pump documents a submit-only send), so a ready
 * ACK is parked for the loop below. */
static bool s_has_ack;
static ota_tdma_frame_t s_pending_ack;

static bool pump_send(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    ota_tdma_frame_t decoded;
    if (!ota_tdma_decode(&decoded, frame, length)) {
        return false;
    }
    if (decoded.type == (uint8_t)OTA_TDMA_FRAME_CLOSE) {
        assert(ota_tdma_ingress_close(&s_ingress) == OTA_TDMA_INGRESS_OK);
        uint8_t ack_wire[OTA_TDMA_FRAME_MAX];
        const size_t ack_len = ota_tdma_ingress_encode_ack(
            &s_ingress, 0u, 0u, ack_wire, sizeof(ack_wire));
        assert(ack_len != 0u);
        assert(ota_tdma_decode(&s_pending_ack, ack_wire, ack_len));
        s_has_ack = true;
        return true;
    }
    assert(decoded.type == (uint8_t)OTA_TDMA_FRAME_DATA);
    if (ota_tdma_ingress_data(&s_ingress, &decoded) != OTA_TDMA_INGRESS_OK) {
        return false;
    }
    ota_tdma_ingress_sync_status(&s_ingress);
    if (!ota_tdma_ingress_ack_ready(&s_ingress)) {
        return true;
    }
    uint8_t ack_wire[OTA_TDMA_FRAME_MAX];
    const size_t ack_len =
        ota_tdma_ingress_encode_ack(&s_ingress, 0u, 0u, ack_wire,
                                    sizeof(ack_wire));
    assert(ack_len != 0u);
    assert(ota_tdma_decode(&s_pending_ack, ack_wire, ack_len));
    s_has_ack = true;
    return true;
}

int main(void)
{
    uint8_t image[20];
    for (size_t i = 0u; i < sizeof(image); ++i) {
        image[i] = (uint8_t)(0x40u + i);
    }
    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    open.total_size = sizeof(image);
    ota_tdma_ingress_init(&s_ingress);
    assert(ota_tdma_ingress_open(&s_ingress, &open) == OTA_TDMA_INGRESS_OK);

    assert(ota_tdma_pump_init(&s_pump, image, sizeof(image), 0x12345678u, 8u, 24u, 3u, 100u, test_crc));
    assert(ota_tdma_pump_start(&s_pump, 0u, pump_send, NULL));

    /* Three DATA frames (8+8+4) plus CLOSE, driven one ACK at a time. */
    unsigned guard = 0u;
    while (ota_tdma_pump_state(&s_pump) != OTA_TDMA_PUMP_DONE && guard++ < 16u) {
        assert(s_has_ack);
        s_has_ack = false;
        assert(ota_tdma_pump_on_ack(&s_pump, 0u, &s_pending_ack, pump_send,
                                    NULL));
    }
    assert(ota_tdma_pump_state(&s_pump) == OTA_TDMA_PUMP_DONE);
    assert(ota_tdma_pump_durable(&s_pump) == sizeof(image));
    assert(s_write_calls == 1u);
    assert(s_close_calls == 1u);
    assert(s_target_size == sizeof(image));
    assert(memcmp(s_target_image, image, sizeof(image)) == 0);
    assert(s_ingress.expected_offset == sizeof(image));
    assert(s_ingress.durable_offset == sizeof(image));

    printf("OTA TDMA loopback host unit tests passed\n");
    return 0;
}
