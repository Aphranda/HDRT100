#include "refmem_sync_frame.h"

#include <string.h>

#include "ota_crc32.h"

static void refmem_sync_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void refmem_sync_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t refmem_sync_get_u16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8u);
}

static uint32_t refmem_sync_get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

static uint16_t refmem_sync_crc16_ccitt(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xFFFFu;
    if (data == NULL && size != 0u) {
        return 0u;
    }

    for (size_t i = 0u; i < size; i++) {
        crc ^= (uint16_t)data[i] << 8u;
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1u) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1u);
            }
        }
    }
    return crc;
}

static void refmem_sync_frame_encode_header_raw(const refmem_sync_frame_header_t *header,
                                                uint16_t header_crc16,
                                                uint8_t *frame)
{
    refmem_sync_put_u16(&frame[0], header->magic);
    frame[2] = header->protocol_version;
    frame[3] = header->frame_type;
    frame[4] = header->header_size;
    frame[5] = header->flags;
    refmem_sync_put_u16(&frame[6], header->payload_size);
    frame[8] = header->source_slot;
    frame[9] = header->target_mask;
    refmem_sync_put_u32(&frame[10], header->epoch_id);
    refmem_sync_put_u32(&frame[14], header->run_id);
    refmem_sync_put_u32(&frame[18], header->seq32);
    refmem_sync_put_u32(&frame[22], header->ack_seq32);
    refmem_sync_put_u32(&frame[26], header->compact_time);
    refmem_sync_put_u16(&frame[30], header_crc16);
    refmem_sync_put_u32(&frame[32], header->payload_crc32);
}

bool refmem_sync_frame_type_is_valid(uint8_t frame_type)
{
    return frame_type >= (uint8_t)REFMEM_SYNC_FRAME_HELLO &&
           frame_type <= (uint8_t)REFMEM_SYNC_FRAME_QUALITY;
}

uint32_t refmem_sync_frame_payload_crc32(const void *payload, uint16_t payload_size)
{
    uint32_t crc = 0xFFFFFFFFu;
    crc = ota_crc32_update(crc, (const uint8_t *)&payload_size, sizeof(payload_size));
    if (payload_size != 0u && payload != NULL) {
        crc = ota_crc32_update(crc, (const uint8_t *)payload, payload_size);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint16_t refmem_sync_frame_header_crc16(const refmem_sync_frame_header_t *header)
{
    uint8_t raw[REFMEM_SYNC_FRAME_HEADER_SIZE];
    if (header == NULL) {
        return 0u;
    }
    refmem_sync_frame_encode_header_raw(header, 0u, raw);
    return refmem_sync_crc16_ccitt(raw, sizeof(raw));
}

bool refmem_sync_frame_header_init(refmem_sync_frame_header_t *header,
                                   uint8_t frame_type,
                                   uint8_t flags,
                                   uint8_t source_slot,
                                   uint8_t target_mask,
                                   uint32_t epoch_id,
                                   uint32_t run_id,
                                   uint32_t seq32,
                                   uint32_t ack_seq32,
                                   uint32_t compact_time,
                                   const void *payload,
                                   uint16_t payload_size)
{
    if (header == NULL ||
        !refmem_sync_frame_type_is_valid(frame_type) ||
        source_slot >= 8u ||
        payload_size > REFMEM_SYNC_FRAME_PAYLOAD_MAX ||
        (payload_size != 0u && payload == NULL)) {
        return false;
    }

    memset(header, 0, sizeof(*header));
    header->magic = REFMEM_SYNC_FRAME_MAGIC;
    header->protocol_version = REFMEM_SYNC_FRAME_VERSION;
    header->frame_type = frame_type;
    header->header_size = REFMEM_SYNC_FRAME_HEADER_SIZE;
    header->flags = flags;
    header->payload_size = payload_size;
    header->source_slot = source_slot;
    header->target_mask = target_mask;
    header->epoch_id = epoch_id;
    header->run_id = run_id;
    header->seq32 = seq32;
    header->ack_seq32 = ack_seq32;
    header->compact_time = compact_time;
    header->payload_crc32 = refmem_sync_frame_payload_crc32(payload, payload_size);
    header->header_crc16 = refmem_sync_frame_header_crc16(header);
    return true;
}

bool refmem_sync_frame_encode(const refmem_sync_frame_header_t *header,
                              const void *payload,
                              uint16_t payload_size,
                              uint8_t *frame,
                              size_t frame_capacity,
                              size_t *frame_size)
{
    const size_t total_size = (size_t)REFMEM_SYNC_FRAME_HEADER_SIZE + payload_size;
    if (header == NULL ||
        frame == NULL ||
        frame_size == NULL ||
        header->header_size != REFMEM_SYNC_FRAME_HEADER_SIZE ||
        header->payload_size != payload_size ||
        header->payload_crc32 != refmem_sync_frame_payload_crc32(payload, payload_size) ||
        header->header_crc16 != refmem_sync_frame_header_crc16(header) ||
        frame_capacity < total_size ||
        (payload_size != 0u && payload == NULL)) {
        return false;
    }

    refmem_sync_frame_encode_header_raw(header, header->header_crc16, frame);
    if (payload_size != 0u) {
        memcpy(&frame[REFMEM_SYNC_FRAME_HEADER_SIZE], payload, payload_size);
    }
    *frame_size = total_size;
    return true;
}

refmem_sync_frame_result_t refmem_sync_frame_decode_header(
    const uint8_t *frame,
    size_t frame_size,
    refmem_sync_frame_header_t *header)
{
    if (frame == NULL || header == NULL) {
        return REFMEM_SYNC_FRAME_BAD_ARGUMENT;
    }
    if (frame_size < REFMEM_SYNC_FRAME_HEADER_SIZE) {
        return REFMEM_SYNC_FRAME_BAD_FRAME_SIZE;
    }

    memset(header, 0, sizeof(*header));
    header->magic = refmem_sync_get_u16(&frame[0]);
    header->protocol_version = frame[2];
    header->frame_type = frame[3];
    header->header_size = frame[4];
    header->flags = frame[5];
    header->payload_size = refmem_sync_get_u16(&frame[6]);
    header->source_slot = frame[8];
    header->target_mask = frame[9];
    header->epoch_id = refmem_sync_get_u32(&frame[10]);
    header->run_id = refmem_sync_get_u32(&frame[14]);
    header->seq32 = refmem_sync_get_u32(&frame[18]);
    header->ack_seq32 = refmem_sync_get_u32(&frame[22]);
    header->compact_time = refmem_sync_get_u32(&frame[26]);
    header->header_crc16 = refmem_sync_get_u16(&frame[30]);
    header->payload_crc32 = refmem_sync_get_u32(&frame[32]);

    if (header->magic != REFMEM_SYNC_FRAME_MAGIC) {
        return REFMEM_SYNC_FRAME_BAD_MAGIC;
    }
    if (header->protocol_version != REFMEM_SYNC_FRAME_VERSION) {
        return REFMEM_SYNC_FRAME_BAD_VERSION;
    }
    if (!refmem_sync_frame_type_is_valid(header->frame_type)) {
        return REFMEM_SYNC_FRAME_BAD_TYPE;
    }
    if (header->header_size != REFMEM_SYNC_FRAME_HEADER_SIZE) {
        return REFMEM_SYNC_FRAME_BAD_HEADER_SIZE;
    }
    if (header->payload_size > REFMEM_SYNC_FRAME_PAYLOAD_MAX) {
        return REFMEM_SYNC_FRAME_BAD_PAYLOAD_SIZE;
    }
    if (frame_size < (size_t)header->header_size + header->payload_size) {
        return REFMEM_SYNC_FRAME_BAD_FRAME_SIZE;
    }
    if (header->header_crc16 != refmem_sync_frame_header_crc16(header)) {
        return REFMEM_SYNC_FRAME_BAD_HEADER_CRC;
    }

    return REFMEM_SYNC_FRAME_OK;
}

refmem_sync_frame_result_t refmem_sync_frame_validate(
    const uint8_t *frame,
    size_t frame_size,
    refmem_sync_frame_header_t *header,
    const uint8_t **payload,
    uint16_t *payload_size)
{
    refmem_sync_frame_header_t decoded;
    refmem_sync_frame_result_t result =
        refmem_sync_frame_decode_header(frame, frame_size, &decoded);
    if (result != REFMEM_SYNC_FRAME_OK) {
        return result;
    }

    const uint8_t *payload_ptr = &frame[decoded.header_size];
    if (decoded.payload_crc32 !=
        refmem_sync_frame_payload_crc32(payload_ptr, decoded.payload_size)) {
        return REFMEM_SYNC_FRAME_BAD_PAYLOAD_CRC;
    }

    if (header != NULL) {
        *header = decoded;
    }
    if (payload != NULL) {
        *payload = payload_ptr;
    }
    if (payload_size != NULL) {
        *payload_size = decoded.payload_size;
    }
    return REFMEM_SYNC_FRAME_OK;
}
