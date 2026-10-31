#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"
#include "ota_tdma_ingress.h"
#include "portable_ota_port.h"

/* --- port stubs: record the forwarding contract ------------------------- */
static unsigned s_open_calls;
static unsigned s_write_calls;
static unsigned s_close_calls;
static pota_stream_ingress_source_t s_last_source;
static uint32_t s_last_offset;
static uint32_t s_last_crc;
static unsigned s_last_size;
static bool s_last_has_crc;
static pota_stream_ingress_result_t s_write_result;
static pota_stream_ingress_result_t s_open_result;
static pota_stream_ingress_result_t s_close_result;
static bool s_status_ok;
static pota_stream_ingress_status_t s_status;


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
    (void)open;
    s_open_calls++;
    s_last_source = source;
    return s_open_result;
}

pota_stream_ingress_result_t portable_ota_port_stream_write(
    pota_stream_ingress_source_t source, uint32_t offset, const uint8_t *data,
    uint32_t size, bool has_crc32, uint32_t crc32)
{
    (void)data;
    s_write_calls++;
    s_last_source = source;
    s_last_offset = offset;
    s_last_size = size;
    s_last_has_crc = has_crc32;
    s_last_crc = crc32;
    return s_write_result;
}

pota_stream_ingress_result_t portable_ota_port_stream_close(
    pota_stream_ingress_source_t source)
{
    s_close_calls++;
    s_last_source = source;
    return s_close_result;
}

bool portable_ota_port_stream_get_status(pota_stream_ingress_status_t *status)
{
    if (!s_status_ok) {
        return false;
    }
    *status = s_status;
    return true;
}

static void reset_stubs(void)
{
    s_open_calls = s_write_calls = s_close_calls = 0u;
    s_last_source = POTA_STREAM_INGRESS_USB_CDC;
    s_last_offset = 0u;
    s_last_crc = 0u;
    s_last_size = 0u;
    s_last_has_crc = false;
    s_write_result = POTA_STREAM_INGRESS_OK;
    s_open_result = POTA_STREAM_INGRESS_OK;
    s_close_result = POTA_STREAM_INGRESS_OK;
    s_status_ok = false;
    memset(&s_status, 0, sizeof(s_status));
}

static ota_tdma_frame_t make_data(uint32_t offset, const uint8_t *payload,
                                  size_t len, uint32_t crc)
{
    ota_tdma_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = (uint8_t)OTA_TDMA_FRAME_DATA;
    frame.flags = OTA_TDMA_FLAG_HAS_CRC;
    frame.offset = offset;
    frame.crc32 = crc;
    frame.payload_len = (uint16_t)len;
    memcpy(frame.payload, payload, len);
    return frame;
}

static void test_open_and_forward(void)
{
    reset_stubs();
    ota_tdma_ingress_t ingress;
    ota_tdma_ingress_init(&ingress);
    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    open.total_size = 8u;

    const uint8_t payload[] = {1u, 2u, 3u, 4u};
    ota_tdma_frame_t frame = make_data(0u, payload, sizeof(payload), 0xAABBCCDDu);
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_NOT_OPEN);

    assert(ota_tdma_ingress_open(&ingress, &open) == OTA_TDMA_INGRESS_OK);
    assert(s_open_calls == 1u && s_last_source == POTA_STREAM_INGRESS_TDMA);
    assert(ingress.open);

    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OK);
    /* Staged: nothing reaches the port until a 512-byte block or CLOSE. */
    assert(s_write_calls == 0u);
    assert(ingress.expected_offset == sizeof(payload));

    frame = make_data(4u, payload, sizeof(payload), 0x11223344u);
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OK);
    assert(ingress.expected_offset == 8u);

    assert(ota_tdma_ingress_close(&ingress) == OTA_TDMA_INGRESS_OK);
    assert(s_write_calls == 1u);
    assert(s_last_source == POTA_STREAM_INGRESS_TDMA);
    assert(s_last_offset == 0u && s_last_size == 8u);
    assert(s_close_calls == 1u && s_last_source == POTA_STREAM_INGRESS_TDMA);
    assert(!ingress.open);
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_NOT_OPEN);
}

static void test_rejections(void)
{
    reset_stubs();
    ota_tdma_ingress_t ingress;
    ota_tdma_ingress_init(&ingress);
    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));

    s_open_result = POTA_STREAM_INGRESS_SESSION;
    assert(ota_tdma_ingress_open(&ingress, &open) == OTA_TDMA_INGRESS_REJECTED);
    assert(!ingress.open);

    s_open_result = POTA_STREAM_INGRESS_OK;
    open.total_size = 4u;
    assert(ota_tdma_ingress_open(&ingress, &open) == OTA_TDMA_INGRESS_OK);

    ota_tdma_frame_t frame = make_data(8u, (const uint8_t *)"x", 1u, 0u);
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OFFSET);
    assert(s_write_calls == 0u);

    frame = make_data(0u, (const uint8_t *)"x", 1u, 0u);
    frame.type = (uint8_t)OTA_TDMA_FRAME_ACK;
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_BAD_FRAME);

    frame = make_data(0u, (const uint8_t *)"x", 1u, 0u);
    s_write_result = POTA_STREAM_INGRESS_CRC_MISMATCH;
    /* Staged, so the port failure surfaces when the block is flushed. */
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OK);
    assert(ota_tdma_ingress_close(&ingress) == OTA_TDMA_INGRESS_REJECTED);
    s_write_result = POTA_STREAM_INGRESS_OK;
    s_close_result = POTA_STREAM_INGRESS_SESSION;
    assert(ota_tdma_ingress_close(&ingress) == OTA_TDMA_INGRESS_REJECTED);
    assert(ingress.open);
}

static void test_status_sync(void)
{
    reset_stubs();
    ota_tdma_ingress_t ingress;
    ota_tdma_ingress_init(&ingress);
    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    assert(ota_tdma_ingress_open(&ingress, &open) == OTA_TDMA_INGRESS_OK);

    s_status_ok = false;
    ota_tdma_ingress_sync_status(&ingress);
    assert(ingress.durable_offset == 0u && ingress.open);

    s_status_ok = true;
    s_status.state = POTA_STREAM_STATE_RECEIVING;
    s_status.durable_offset = 4096u;
    ota_tdma_ingress_sync_status(&ingress);
    assert(ingress.durable_offset == 4096u && ingress.open);

    s_status.state = POTA_STREAM_STATE_FAILED;
    ota_tdma_ingress_sync_status(&ingress);
    assert(!ingress.open);
}

static void test_staging_flushes_header_and_pages(void)
{
    reset_stubs();
    ota_tdma_ingress_t ingress;
    ota_tdma_ingress_init(&ingress);
    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    open.total_size = 1024u;
    assert(ota_tdma_ingress_open(&ingress, &open) == OTA_TDMA_INGRESS_OK);

    uint8_t payload[64];
    for (uint32_t i = 0u; i < 16u; ++i) {
        memset(payload, (int)i, sizeof(payload));
        const ota_tdma_frame_t frame =
            make_data(i * 64u, payload, sizeof(payload), (uint32_t)i);
        assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OK);
        if (i == 7u) {
            assert(s_write_calls == 1u);
            assert(s_last_size == 512u && s_last_offset == 0u);
        }
        if (i == 11u) {
            assert(s_write_calls == 2u);
            assert(s_last_size == 256u && s_last_offset == 512u);
        }
        if (i < 15u) {
            assert(s_write_calls <= 2u);
        }
    }
    assert(s_write_calls == 3u);
    assert(s_last_offset == 768u && s_last_size == 256u);
    assert(ota_tdma_ingress_close(&ingress) == OTA_TDMA_INGRESS_OK);
    assert(s_write_calls == 3u); /* nothing staged at close */
}

static void test_duplicate_data_is_idempotent(void)
{
    reset_stubs();
    ota_tdma_ingress_t ingress;
    ota_tdma_ingress_init(&ingress);
    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    const uint8_t payload[] = {1u, 2u, 3u, 4u};
    open.total_size = sizeof(payload);
    assert(ota_tdma_ingress_open(&ingress, &open) == OTA_TDMA_INGRESS_OK);

    const ota_tdma_frame_t frame = make_data(0u, payload, sizeof(payload),
                                             0xCAFEu);
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OK);
    assert(ingress.expected_offset == sizeof(payload));
    assert(s_write_calls == 1u); /* image complete -> flushed */

    /* Lost ACK -> sender retransmits the same frame: re-ACK, do not fail. */
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OK);
    assert(ingress.expected_offset == sizeof(payload));

    /* A conflicting frame at the same offset still fails closed. */
    const uint8_t other[] = {9u, 9u, 9u, 9u};
    const ota_tdma_frame_t conflicting = make_data(0u, other, sizeof(other),
                                                   0xBEEFu);
    assert(ota_tdma_ingress_data(&ingress, &conflicting) ==
           OTA_TDMA_INGRESS_OFFSET);
}

static void test_ack_generation(void)
{
    reset_stubs();
    ota_tdma_ingress_t ingress;
    ota_tdma_ingress_init(&ingress);
    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    assert(ota_tdma_ingress_open(&ingress, &open) == OTA_TDMA_INGRESS_OK);
    assert(!ota_tdma_ingress_ack_ready(&ingress));

    const uint8_t payload[] = {9u, 8u, 7u, 6u};
    ota_tdma_frame_t frame = make_data(0u, payload, sizeof(payload), 0u);
    assert(ota_tdma_ingress_data(&ingress, &frame) == OTA_TDMA_INGRESS_OK);

    s_status_ok = true;
    s_status.state = POTA_STREAM_STATE_RECEIVING;
    s_status.durable_offset = 0u;
    ota_tdma_ingress_sync_status(&ingress);
    assert(!ota_tdma_ingress_ack_ready(&ingress));

    s_status.durable_offset = sizeof(payload);
    ota_tdma_ingress_sync_status(&ingress);
    assert(ota_tdma_ingress_ack_ready(&ingress));

    uint8_t wire[OTA_TDMA_FRAME_MAX];
    size_t len = ota_tdma_ingress_encode_ack(&ingress, 0u, 0u, wire,
                                             sizeof(wire));
    assert(len != 0u);
    ota_tdma_frame_t ack;
    assert(ota_tdma_decode(&ack, wire, len));
    assert(ack.type == (uint8_t)OTA_TDMA_FRAME_ACK);
    assert(ack.offset == sizeof(payload) && ack.result == 0u);

    len = ota_tdma_ingress_encode_ack(&ingress, 5u, 13u, wire, sizeof(wire));
    assert(len != 0u && ota_tdma_decode(&ack, wire, len));
    assert(ack.result == 5u && ack.reason == 13u);
}

int main(void)
{
    test_open_and_forward();
    test_rejections();
    test_status_sync();
    test_ack_generation();
    test_duplicate_data_is_idempotent();
    test_staging_flushes_header_and_pages();
    printf("OTA TDMA ingress host unit tests passed\n");
    return 0;
}
