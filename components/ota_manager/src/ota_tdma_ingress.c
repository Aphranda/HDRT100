#include "ota_tdma_ingress.h"

#include <string.h>

#include "portable_ota_port.h"

static ota_tdma_ingress_result_t ota_tdma_ingress_map(
    pota_stream_ingress_result_t result)
{
    return result == POTA_STREAM_INGRESS_OK ? OTA_TDMA_INGRESS_OK
                                            : OTA_TDMA_INGRESS_REJECTED;
}

uint32_t ota_tdma_ingress_last_session_result(void)
{
    return portable_ota_port_stream_last_session_result();
}

void ota_tdma_ingress_init(ota_tdma_ingress_t *ingress)
{
    if (ingress == NULL) {
        return;
    }
    memset(ingress, 0, sizeof(*ingress));
}

ota_tdma_ingress_result_t ota_tdma_ingress_open(ota_tdma_ingress_t *ingress,
                                                const pota_stream_open_t *open)
{
    if (ingress == NULL || open == NULL) {
        return OTA_TDMA_INGRESS_BAD_FRAME;
    }
    if (ingress->open) {
        /* A retried or superseding OPEN must not be rejected with SESSION:
         * release the previous TDMA ingress first (the source retransmits OPEN
         * every 100 ms during the grace window, and a timed-out attempt can
         * leave the ingress open). */
        (void)portable_ota_port_stream_abort(POTA_STREAM_INGRESS_TDMA);
        (void)portable_ota_port_stream_force_reset();
        ingress->open = false;
    }
    pota_stream_ingress_result_t result =
        portable_ota_port_stream_open(POTA_STREAM_INGRESS_TDMA, open);
    if (result == POTA_STREAM_INGRESS_SESSION) {
        /* The portable layer still holds an old lease (its own ingress flag
         * survives our abort when the stored source/state differs).  Rebuild
         * the stream surfaces and retry once. */
        (void)portable_ota_port_stream_force_reset();
        ingress->open = false;
        result = portable_ota_port_stream_open(POTA_STREAM_INGRESS_TDMA, open);
    }
    if (result != POTA_STREAM_INGRESS_OK) {
        return ota_tdma_ingress_map(result);
    }
    ingress->open = true;
    ingress->expected_offset = 0u;
    ingress->durable_offset = 0u;
    ingress->total_size = open->total_size;
    return OTA_TDMA_INGRESS_OK;
}

static ota_tdma_ingress_result_t ota_tdma_ingress_flush(
    ota_tdma_ingress_t *ingress)
{
    if (ingress->stage_length == 0u) {
        return OTA_TDMA_INGRESS_OK;
    }
    const pota_stream_ingress_result_t result =
        portable_ota_port_stream_write(POTA_STREAM_INGRESS_TDMA,
                                       ingress->stage_offset, ingress->stage,
                                       ingress->stage_length, true,
                                       portable_ota_port_crc32_compute(
                                           ingress->stage,
                                           ingress->stage_length));
    if (result != POTA_STREAM_INGRESS_OK) {
        return ota_tdma_ingress_map(result);
    }
    ingress->stage_length = 0u;
    return OTA_TDMA_INGRESS_OK;
}

ota_tdma_ingress_result_t ota_tdma_ingress_data(ota_tdma_ingress_t *ingress,
                                                const ota_tdma_frame_t *frame)
{
    if (ingress == NULL || frame == NULL || !ingress->open) {
        return OTA_TDMA_INGRESS_NOT_OPEN;
    }
    if (frame->type != (uint8_t)OTA_TDMA_FRAME_DATA ||
        frame->payload_len > OTA_TDMA_DATA_MAX_PAYLOAD) {
        return OTA_TDMA_INGRESS_BAD_FRAME;
    }
    if (ingress->has_last && frame->offset == ingress->last_offset &&
        frame->payload_len == ingress->last_length &&
        frame->crc32 == ingress->last_crc32) {
        /* Retransmission of the frame we already accepted: re-ACK instead of
         * failing the session (a lost ACK must not abort the upgrade). */
        return OTA_TDMA_INGRESS_OK;
    }
    if (frame->offset != ingress->expected_offset) {
        return OTA_TDMA_INGRESS_OFFSET;
    }
    if (ingress->stage_length == 0u) {
        ingress->stage_offset = frame->offset;
    } else if (frame->offset != ingress->stage_offset + ingress->stage_length) {
        return OTA_TDMA_INGRESS_OFFSET;
    }
    memcpy(&ingress->stage[ingress->stage_length], frame->payload,
           frame->payload_len);
    ingress->stage_length += frame->payload_len;
    ingress->last_offset = frame->offset;
    ingress->last_length = frame->payload_len;
    ingress->last_crc32 = frame->crc32;
    ingress->has_last = true;
    ingress->expected_offset += frame->payload_len;
    const uint32_t threshold =
        ingress->stage_offset == 0u ? OTA_TDMA_INGRESS_STAGE : 256u;
    if (ingress->stage_length >= threshold ||
        (ingress->total_size != 0u &&
         ingress->expected_offset >= ingress->total_size)) {
        return ota_tdma_ingress_flush(ingress);
    }
    return OTA_TDMA_INGRESS_OK;
}

ota_tdma_ingress_result_t ota_tdma_ingress_close(ota_tdma_ingress_t *ingress)
{
    if (ingress == NULL || !ingress->open) {
        return OTA_TDMA_INGRESS_NOT_OPEN;
    }
    const ota_tdma_ingress_result_t flush = ota_tdma_ingress_flush(ingress);
    if (flush != OTA_TDMA_INGRESS_OK) {
        return flush;
    }
    const pota_stream_ingress_result_t result =
        portable_ota_port_stream_close(POTA_STREAM_INGRESS_TDMA);
    const ota_tdma_ingress_result_t mapped = ota_tdma_ingress_map(result);
    if (mapped == OTA_TDMA_INGRESS_OK) {
        ingress->open = false;
    }
    return mapped;
}

void ota_tdma_ingress_sync_status(ota_tdma_ingress_t *ingress)
{
    if (ingress == NULL || !ingress->open) {
        return;
    }
    pota_stream_ingress_status_t status;
    if (!portable_ota_port_stream_get_status(&status)) {
        return;
    }
    ingress->durable_offset = status.durable_offset;
    if (status.state == POTA_STREAM_STATE_FAILED ||
        status.state == POTA_STREAM_STATE_ABORTED ||
        status.state == POTA_STREAM_STATE_READY_TO_REBOOT) {
        ingress->open = false;
    }
}

bool ota_tdma_ingress_ack_ready(const ota_tdma_ingress_t *ingress)
{
    return ingress != NULL && ingress->open && ingress->expected_offset != 0u &&
           ingress->durable_offset >= ingress->expected_offset;
}

size_t ota_tdma_ingress_encode_ack(const ota_tdma_ingress_t *ingress,
                                   uint8_t result, uint8_t reason,
                                   uint8_t *out, size_t capacity)
{
    if (ingress == NULL) {
        return 0u;
    }
    return ota_tdma_encode_ack(out, capacity, ingress->durable_offset, result,
                               reason);
}
