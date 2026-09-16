#include "vdc_priority_codec.h"

#include <string.h>

_Static_assert(VDC_PRIORITY_CODEC_BODY_SIZE == 22u,
    "typed synchronization body size is part of the wire contract");

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}
static void put32(uint8_t *p, uint32_t value)
{
    put16(p, (uint16_t)value);
    put16(p + 2u, (uint16_t)(value >> 16u));
}
static void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)value);
    put32(p + 4u, (uint32_t)(value >> 32u));
}
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2u) << 16u);
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4u) << 32u);
}

uint16_t vdc_priority_codec_crc16(const uint8_t *data, size_t size)
{
    if (data == NULL) return 0u;
    uint16_t crc = VDC_PRIORITY_CODEC_CRC_INIT;
    for (size_t i = 0u; i < size; ++i) {
        crc ^= (uint16_t)data[i] << 8u;
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc & 0x8000u) != 0u
                ? (uint16_t)((crc << 1u) ^ VDC_PRIORITY_CODEC_CRC_POLY)
                : (uint16_t)(crc << 1u);
    }
    return crc;
}

static bool valid_record(const vdc_priority_codec_record_t *record)
{
    return record != NULL && record->binding_generation != 0u &&
        record->uncertainty_width != 0u &&
        (record->flags & (uint16_t)~VDC_PRIORITY_CODEC_KNOWN_FLAGS) == 0u;
}

bool vdc_priority_codec_encode(const vdc_priority_codec_record_t *record,
    uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE])
{
    if (!valid_record(record) || body == NULL) return false;
    memset(body, 0, VDC_PRIORITY_CODEC_BODY_SIZE);
    put32(body + (TDMA_PROCESS_IMAGE_PRIORITY_SYNC_GENERATION_OFFSET -
                  TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET), record->binding_generation);
    put32(body + (TDMA_PROCESS_IMAGE_PRIORITY_SYNC_EVENT_SEQUENCE_OFFSET -
                  TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET), record->event_sequence);
    put64(body + (TDMA_PROCESS_IMAGE_PRIORITY_SYNC_TIME_LOWER_OFFSET -
                  TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET), record->event_time_lower);
    put32(body + (TDMA_PROCESS_IMAGE_PRIORITY_SYNC_UNCERTAINTY_WIDTH_OFFSET -
                  TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET), record->uncertainty_width);
    put16(body + (TDMA_PROCESS_IMAGE_PRIORITY_SYNC_FLAGS_OFFSET -
                  TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET), record->flags);
    return true;
}

bool vdc_priority_codec_decode(const uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE],
    vdc_priority_codec_record_t *record)
{
    if (body == NULL || record == NULL) return false;
    vdc_priority_codec_record_t value = {
        .binding_generation = get32(body + 0u),
        .event_sequence = get32(body + 4u),
        .event_time_lower = get64(body + 8u),
        .uncertainty_width = get32(body + 16u),
        .flags = get16(body + 20u),
    };
    if (!valid_record(&value)) return false;
    *record = value;
    return true;
}

bool vdc_priority_codec_crc_valid(const uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE],
    uint16_t expected_crc)
{
    return body != NULL && vdc_priority_codec_crc16(body, VDC_PRIORITY_CODEC_BODY_SIZE) == expected_crc;
}

bool vdc_priority_codec_layout_admit(uint8_t message_class, size_t body_size)
{
    return tdma_process_image_typed_sync_class_valid(message_class) &&
        body_size == VDC_PRIORITY_CODEC_BODY_SIZE;
}
