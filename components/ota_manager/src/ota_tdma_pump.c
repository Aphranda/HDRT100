#include "ota_tdma_pump.h"

#include <string.h>

static uint32_t ota_tdma_pump_block_end(const ota_tdma_pump_t *pump,
                                        uint32_t start)
{
    const uint32_t block = start == 0u ? pump->block_size : 256u;
    const uint32_t end = start + block;
    return end < pump->image_size ? end : pump->image_size;
}

static uint32_t ota_tdma_pump_chunk_length(const ota_tdma_pump_t *pump,
                                           uint32_t offset)
{
    const uint32_t remaining = pump->image_size - offset;
    return remaining < pump->chunk_size ? remaining : pump->chunk_size;
}

/* Image-backed reader used by the resident-buffer constructor. */
static bool ota_tdma_pump_image_read(void *context, uint32_t offset,
                                     uint8_t *destination, uint32_t length)
{
    const ota_tdma_pump_t *pump = context;
    if (pump->image == NULL || offset + length > pump->image_size) {
        return false;
    }
    memcpy(destination, &pump->image[offset], length);
    return true;
}

static bool ota_tdma_pump_send_data(ota_tdma_pump_t *pump, uint32_t offset,
                                    ota_tdma_pump_send_fn send, void *context)
{
    const uint32_t length = ota_tdma_pump_chunk_length(pump, offset);
    uint8_t payload[OTA_TDMA_DATA_MAX_PAYLOAD];
    if (pump->read == NULL ||
        !pump->read(pump->read_context, offset, payload, length)) {
        pump->failure = OTA_TDMA_PUMP_FAIL_SOURCE;
        return false; /* source not ready yet: keep the current state */
    }
    const uint32_t crc = pump->crc32(payload, length);
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    const size_t frame_len =
        ota_tdma_encode_data(frame, sizeof(frame), offset, crc,
                             payload, length,
                             offset + length == pump->image_size);
    if (frame_len == 0u) {
        pump->state = OTA_TDMA_PUMP_FAILED;
        pump->failure = OTA_TDMA_PUMP_FAIL_PROTOCOL;
        return false;
    }
    if (!send(context, frame, frame_len)) {
        /* No maintenance window right now: transient, not a protocol error.
         * Leave the state untouched so the next tick retries this frame. */
        return false;
    }
    pump->sent_offset = offset;
    pump->sent_length = length;
    pump->state = OTA_TDMA_PUMP_WAIT_ACK;
    return true;
}

/* Sends every frame of one ACK block back-to-back, then waits for the block
 * ACK.  A block is what the receiver can program in one durable step. */
static bool ota_tdma_pump_send_block(ota_tdma_pump_t *pump, uint32_t start,
                                     uint32_t now_ms,
                                     ota_tdma_pump_send_fn send,
                                     void *context)
{
    const uint32_t end = ota_tdma_pump_block_end(pump, start);
    for (uint32_t offset = start; offset < end;
         offset += ota_tdma_pump_chunk_length(pump, offset)) {
        const uint32_t length = ota_tdma_pump_chunk_length(pump, offset);
        if (!ota_tdma_pump_send_data(pump, offset, send, context)) {
            return false;
        }
        if (offset + length >= end) {
            break;
        }
        pump->state = OTA_TDMA_PUMP_SENDING_BLOCK;
    }
    pump->state = OTA_TDMA_PUMP_WAIT_ACK;
    pump->sent_offset = start;
    pump->sent_length = end - start;
    pump->attempts++;
    pump->deadline_ms = now_ms + pump->timeout_ms;
    return true;
}

bool ota_tdma_pump_init(ota_tdma_pump_t *pump, const uint8_t *image,
                        uint32_t image_size, uint32_t image_crc32,
                        uint32_t chunk_size, uint32_t block_size,
                        uint8_t max_attempts, uint32_t timeout_ms,
                        ota_tdma_pump_crc_fn crc32)
{
    if (pump == NULL || image == NULL || image_size == 0u ||
        chunk_size == 0u || chunk_size > OTA_TDMA_DATA_MAX_PAYLOAD ||
        block_size < chunk_size || (block_size % chunk_size) != 0u ||
        max_attempts == 0u || crc32 == NULL) {
        return false;
    }
    memset(pump, 0, sizeof(*pump));
    pump->image = image;
    pump->read = ota_tdma_pump_image_read;
    pump->read_context = pump;
    pump->image_size = image_size;
    pump->image_crc32 = image_crc32;
    pump->chunk_size = chunk_size;
    pump->block_size = block_size;
    pump->max_attempts = max_attempts;
    pump->timeout_ms = timeout_ms;
    pump->crc32 = crc32;
    pump->state = OTA_TDMA_PUMP_IDLE;
    return true;
}

bool ota_tdma_pump_init_streaming(ota_tdma_pump_t *pump, uint32_t image_size,
                                  uint32_t image_crc32, uint32_t chunk_size,
                                  uint32_t block_size, uint8_t max_attempts,
                                  uint32_t timeout_ms,
                                  ota_tdma_pump_crc_fn crc32,
                                  ota_tdma_pump_read_fn read,
                                  void *read_context)
{
    if (pump == NULL || image_size == 0u || chunk_size == 0u ||
        chunk_size > OTA_TDMA_DATA_MAX_PAYLOAD || block_size < chunk_size ||
        (block_size % chunk_size) != 0u || max_attempts == 0u ||
        crc32 == NULL || read == NULL) {
        return false;
    }
    memset(pump, 0, sizeof(*pump));
    pump->read = read;
    pump->read_context = read_context;
    pump->image_size = image_size;
    pump->image_crc32 = image_crc32;
    pump->chunk_size = chunk_size;
    pump->block_size = block_size;
    pump->max_attempts = max_attempts;
    pump->timeout_ms = timeout_ms;
    pump->crc32 = crc32;
    pump->state = OTA_TDMA_PUMP_IDLE;
    return true;
}

bool ota_tdma_pump_start(ota_tdma_pump_t *pump, uint32_t now_ms,
                         ota_tdma_pump_send_fn send, void *context)
{
    if (pump == NULL || send == NULL || pump->state != OTA_TDMA_PUMP_IDLE ||
        pump->read == NULL) {
        return false;
    }
    pump->next_offset = 0u;
    pump->durable_offset = 0u;
    return ota_tdma_pump_send_block(pump, 0u, now_ms, send, context);
}

bool ota_tdma_pump_tick(ota_tdma_pump_t *pump, uint32_t now_ms,
                        ota_tdma_pump_send_fn send, void *context)
{
    if (pump == NULL || send == NULL ||
        (pump->state != OTA_TDMA_PUMP_WAIT_ACK &&
         pump->state != OTA_TDMA_PUMP_WAIT_CLOSE_ACK)) {
        return false;
    }
    if ((int32_t)(now_ms - pump->deadline_ms) < 0) {
        return false;
    }
    if (pump->attempts >= pump->max_attempts) {
        pump->state = OTA_TDMA_PUMP_FAILED;
        pump->failure = OTA_TDMA_PUMP_FAIL_RETRIES;
        return false;
    }
    if (pump->state == OTA_TDMA_PUMP_WAIT_CLOSE_ACK) {
        uint8_t close_frame[OTA_TDMA_FRAME_MAX];
        const size_t close_len =
            ota_tdma_encode_close(close_frame, sizeof(close_frame),
                                  pump->image_size, pump->image_crc32);
        if (close_len == 0u || !send(context, close_frame, close_len)) {
            pump->state = OTA_TDMA_PUMP_FAILED;
            pump->failure = OTA_TDMA_PUMP_FAIL_PROTOCOL;
            return false;
        }
        pump->attempts++;
        pump->deadline_ms = now_ms + pump->timeout_ms;
        return true;
    }
    return ota_tdma_pump_send_block(pump, pump->sent_offset, now_ms, send,
                                    context);
}

bool ota_tdma_pump_on_ack(ota_tdma_pump_t *pump, uint32_t now_ms,
                          const ota_tdma_frame_t *ack,
                          ota_tdma_pump_send_fn send, void *context)
{
    if (pump == NULL || ack == NULL || send == NULL ||
        (pump->state != OTA_TDMA_PUMP_WAIT_ACK &&
         pump->state != OTA_TDMA_PUMP_WAIT_CLOSE_ACK) ||
        ack->type != (uint8_t)OTA_TDMA_FRAME_ACK) {
        return false;
    }
    if (pump->state == OTA_TDMA_PUMP_WAIT_CLOSE_ACK) {
        if (ack->result != 0u) {
            pump->state = OTA_TDMA_PUMP_FAILED;
            pump->failure = OTA_TDMA_PUMP_FAIL_REJECTED;
            return false;
        }
        if (ack->offset != pump->image_size) {
            pump->state = OTA_TDMA_PUMP_FAILED;
            pump->failure = OTA_TDMA_PUMP_FAIL_PROTOCOL;
            return false;
        }
        pump->state = OTA_TDMA_PUMP_DONE;
        return true;
    }
    if (ack->result != 0u) {
        pump->state = OTA_TDMA_PUMP_FAILED;
        pump->failure = OTA_TDMA_PUMP_FAIL_REJECTED;
        return false;
    }
    const uint32_t acknowledged = pump->sent_offset + pump->sent_length;
    if (ack->offset == pump->durable_offset) {
        return false; /* stale ACK for the previous frame: keep waiting */
    }
    if (ack->offset != acknowledged) {
        pump->state = OTA_TDMA_PUMP_FAILED;
        pump->failure = OTA_TDMA_PUMP_FAIL_PROTOCOL;
        return false;
    }

    pump->durable_offset = acknowledged;
    pump->next_offset = acknowledged;
    if (acknowledged >= pump->image_size) {
        uint8_t close_frame[OTA_TDMA_FRAME_MAX];
        const size_t close_len =
            ota_tdma_encode_close(close_frame, sizeof(close_frame),
                                  pump->image_size, pump->image_crc32);
        if (close_len == 0u || !send(context, close_frame, close_len)) {
            pump->state = OTA_TDMA_PUMP_FAILED;
            pump->failure = OTA_TDMA_PUMP_FAIL_PROTOCOL;
            return false;
        }
        /* CLOSE is confirmed by the target before the source declares READY:
         * otherwise a dropped CLOSE would look like success. */
        pump->state = OTA_TDMA_PUMP_WAIT_CLOSE_ACK;
        pump->sent_offset = pump->image_size;
        pump->sent_length = 0u;
        pump->attempts = 1u;
        pump->deadline_ms = now_ms + pump->timeout_ms;
        return true;
    }
    if (!ota_tdma_pump_send_block(pump, acknowledged, now_ms, send, context)) {
        /* The source has no data for this block yet: stay in WAIT_ACK and
         * let resume() continue once the window is fed. */
        return pump->state != OTA_TDMA_PUMP_FAILED;
    }
    return true;
}

bool ota_tdma_pump_resume(ota_tdma_pump_t *pump, uint32_t now_ms,
                          ota_tdma_pump_send_fn send, void *context)
{
    if (pump == NULL || send == NULL ||
        pump->state != OTA_TDMA_PUMP_WAIT_ACK) {
        return false;
    }
    if (pump->sent_offset + pump->sent_length != pump->durable_offset) {
        return false; /* a frame is still in flight */
    }
    if (!ota_tdma_pump_send_block(pump, pump->durable_offset, now_ms, send,
                                  context)) {
        return false;
    }
    return true;
}

ota_tdma_pump_state_t ota_tdma_pump_state(const ota_tdma_pump_t *pump)
{
    return pump == NULL ? OTA_TDMA_PUMP_FAILED : pump->state;
}

ota_tdma_pump_failure_t ota_tdma_pump_failure(const ota_tdma_pump_t *pump)
{
    return pump == NULL ? OTA_TDMA_PUMP_FAIL_PROTOCOL : pump->failure;
}

uint32_t ota_tdma_pump_durable(const ota_tdma_pump_t *pump)
{
    return pump == NULL ? 0u : pump->durable_offset;
}
