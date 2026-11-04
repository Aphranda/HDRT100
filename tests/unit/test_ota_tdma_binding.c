#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The firmware allocates the binding state from the FreeRTOS heap. */
void *pvPortMalloc(size_t size)
{
    return malloc(size);
}

#include "ota_tdma_binding.h"
#include "tdma_second_class_parser.h"
#include "ota_tdma_codec.h"
#include "portable_ota_port.h"
#include "drv_watchdog.h"

/* --- watchdog stubs ------------------------------------------------------ */
static uint32_t s_ota_phase;

void drv_watchdog_mark_ota_phase(uint32_t phase)
{
    s_ota_phase = phase;
}

void drv_watchdog_get_reset_snapshot(drv_watchdog_reset_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->ota_phase = s_ota_phase;
}

/* --- TDMA maintenance runtime stubs ------------------------------------- */
static uint32_t s_local_slot = 0u;
static bool s_slot_ok = true;
static unsigned s_submit_calls;
static uint8_t s_submit_mask;
static unsigned s_ack_calls;
static bool s_last_ack_ok;

bool tdma_maintenance_runtime_get_local_slot(uint32_t *slot)
{
    if (!s_slot_ok) {
        return false;
    }
    *slot = s_local_slot;
    return true;
}

bool tdma_maintenance_runtime_submit(const uint8_t *frame, size_t frame_size,
                                     uint8_t stream_id, uint8_t generation,
                                     uint8_t target_mask)
{
    (void)frame;
    (void)frame_size;
    (void)stream_id;
    (void)generation;
    s_submit_calls++;
    s_submit_mask = target_mask;
    return true;
}

static uint8_t s_rx[OTA_TDMA_FRAME_MAX];
static size_t s_rx_len;
static bool s_rx_ready;
static uint8_t s_rx_stream = 0x4Fu;
static uint8_t s_rx_generation = 1u;

bool tdma_maintenance_runtime_take(uint8_t *frame, size_t frame_capacity,
                                   size_t *frame_size, uint8_t *stream_id,
                                   uint8_t *generation)
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

bool tdma_maintenance_runtime_ack(uint8_t stream_id, uint8_t generation,
                                  uint8_t source_slot, bool ok)
{
    (void)stream_id;
    (void)generation;
    (void)source_slot;
    s_ack_calls++;
    s_last_ack_ok = ok;
    return true;
}

/* --- portable OTA port stubs ------------------------------------------- */
static uint8_t s_image[64];
static uint32_t s_image_size;
static uint32_t s_durable;

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

uint32_t portable_ota_port_stream_platform_map_version(void)
{
    return 2u;
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
    if (offset + size > sizeof(s_image)) {
        return POTA_STREAM_INGRESS_FRAME_TOO_LARGE;
    }
    memcpy(&s_image[offset], data, size);
    if (offset + size > s_image_size) {
        s_image_size = offset + size;
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

bool tdma_second_class_parser_register(uint8_t type,
                                       tdma_second_class_parser_fn parser,
                                       void *context)
{
    (void)type;
    (void)parser;
    (void)context;
    return true;
}

uint32_t board_uptime_ms(void)
{
    return 0u;
}

static unsigned s_admission_requests;
static unsigned s_admission_releases;

bool resource_arbiter_request_ota_admission(void)
{
    s_admission_requests++;
    return true;
}

void resource_arbiter_release_ota_admission(void)
{
    s_admission_releases++;
}

uint32_t portable_ota_port_crc32_compute(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc = (crc >> 8u) ^ data[i];
    }
    return crc;
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
    put_le32(&wire[0], 1u);
    put_le32(&wire[4], 1u);
    put_le32(&wire[8], 3u);
    put_le32(&wire[12], 1u);
    put_le32(&wire[16], 2u);
    put_le32(&wire[20], 1u);
    put_le32(&wire[24], 5u);
    put_le32(&wire[28], size);
    put_le32(&wire[32], 0u);
}

int main(void)
{
    uint8_t open_wire[88];
    build_open_wire(open_wire, 8u);

    /* Slot 2 is outside the v1 single-peer binding. */
    s_local_slot = 2u;
    assert(!ota_tdma_binding_init());
    assert(!ota_tdma_binding_active());

    /* Slot getter failure is fail-closed. */
    s_slot_ok = false;
    assert(!ota_tdma_binding_init());
    s_slot_ok = true;

    /* Slot 0 -> peer 1 -> mask 0x02. */
    s_local_slot = 0u;
    assert(ota_tdma_binding_init());
    assert(ota_tdma_binding_active());
    assert(s_submit_calls == 0u);
    ota_tdma_binding_service(0u, 2u); /* nothing queued */
    assert(s_submit_calls == 0u);

    /* Deliver OPEN fragments, one DATA frame and CLOSE through the binding. */
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    size_t len = ota_tdma_encode_open(frame, sizeof(frame), open_wire, 88u, 0u,
                                      2u);
    assert(ota_tdma_binding_handle_maintenance_frame(frame, len));

    len = ota_tdma_encode_open(frame, sizeof(frame), open_wire, 88u, 1u, 2u);
    assert(ota_tdma_binding_handle_maintenance_frame(frame, len));
    /* The target takes the OTA admission lease itself: its OPEN arrives over
     * the maintenance stream, so no SCPI command acquired it. */
    assert(s_admission_requests == 1u);

    const uint8_t payload[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    len = ota_tdma_encode_data(frame, sizeof(frame), 0u, 0u, payload,
                               sizeof(payload), true);
    assert(ota_tdma_binding_handle_maintenance_frame(frame, len));
    assert(s_image_size == sizeof(payload));
    assert(s_submit_calls == 1u && s_submit_mask == 0x02u);

    len = ota_tdma_encode_close(frame, sizeof(frame), sizeof(payload), 0u);
    assert(ota_tdma_binding_handle_maintenance_frame(frame, len));
    assert(s_image_size == sizeof(payload));
    assert(memcmp(s_image, payload, sizeof(payload)) == 0);
    assert(s_submit_calls == 2u && s_submit_mask == 0x02u);
    ota_tdma_binding_service(0u, 2u); /* DONE -> re-arm + release admission */
    assert(s_admission_releases == 1u);
    assert(ota_tdma_binding_active()); /* target role re-armed after DONE */

    printf("OTA TDMA binding host unit tests passed\n");
    return 0;
}
