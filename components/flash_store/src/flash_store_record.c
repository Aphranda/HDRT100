#include "flash_store_record.h"

#include <string.h>

#include "pota_types.h"

enum {
    RECORD_OFFSET_MAGIC = 0u,
    RECORD_OFFSET_SCHEMA = 4u,
    RECORD_OFFSET_OBJECT = 8u,
    RECORD_OFFSET_GENERATION = 12u,
    RECORD_OFFSET_SEQUENCE = 16u,
    RECORD_OFFSET_FLAGS = 20u,
    RECORD_OFFSET_PAYLOAD_LENGTH = 24u,
    RECORD_OFFSET_PAYLOAD_CRC = 28u,
    RECORD_OFFSET_HEADER_CRC = 32u,
    RECORD_OFFSET_COMMIT = 36u,
};

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8u) & 0xFFu);
    data[2] = (uint8_t)((value >> 16u) & 0xFFu);
    data[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint32_t read_le32(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static bool all_erased(const uint8_t *data, size_t length)
{
    for (size_t index = 0u; index < length; index++) {
        if (data[index] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static uint32_t commit_marker(uint32_t generation, uint32_t sequence)
{
    return FLASH_STORE_RECORD_COMMIT_MARKER ^ generation ^ sequence;
}

static bool serial_newer(uint32_t candidate, uint32_t current)
{
    return candidate != current && (int32_t)(candidate - current) > 0;
}

static void decode_header(const uint8_t *record,
                          flash_store_record_header_t *header)
{
    header->schema_version = read_le32(&record[RECORD_OFFSET_SCHEMA]);
    header->object_type = read_le32(&record[RECORD_OFFSET_OBJECT]);
    header->generation = read_le32(&record[RECORD_OFFSET_GENERATION]);
    header->sequence = read_le32(&record[RECORD_OFFSET_SEQUENCE]);
    header->flags = read_le32(&record[RECORD_OFFSET_FLAGS]);
    header->payload_length = read_le32(&record[RECORD_OFFSET_PAYLOAD_LENGTH]);
    header->payload_crc32 = read_le32(&record[RECORD_OFFSET_PAYLOAD_CRC]);
    header->header_crc32 = read_le32(&record[RECORD_OFFSET_HEADER_CRC]);
    header->commit_marker = read_le32(&record[RECORD_OFFSET_COMMIT]);
}

flash_store_record_result_t flash_store_record_encode(
    uint32_t schema_version,
    uint32_t object_type,
    uint32_t generation,
    uint32_t sequence,
    uint32_t flags,
    const uint8_t *payload,
    uint32_t payload_length,
    uint8_t *record,
    size_t record_capacity,
    size_t *record_size)
{
    if (record_size == NULL || record == NULL || schema_version == 0u ||
        object_type == 0u || generation == 0u || sequence == 0u ||
        (payload_length != 0u && payload == NULL)) {
        return FLASH_STORE_RECORD_BAD_ARGUMENT;
    }
    if (record_capacity < FLASH_STORE_RECORD_HEADER_SIZE ||
        (size_t)payload_length >
            record_capacity - FLASH_STORE_RECORD_HEADER_SIZE) {
        return FLASH_STORE_RECORD_RANGE;
    }

    memset(record, 0xFF, FLASH_STORE_RECORD_HEADER_SIZE + payload_length);
    write_le32(&record[RECORD_OFFSET_MAGIC], FLASH_STORE_RECORD_MAGIC);
    write_le32(&record[RECORD_OFFSET_SCHEMA], schema_version);
    write_le32(&record[RECORD_OFFSET_OBJECT], object_type);
    write_le32(&record[RECORD_OFFSET_GENERATION], generation);
    write_le32(&record[RECORD_OFFSET_SEQUENCE], sequence);
    write_le32(&record[RECORD_OFFSET_FLAGS], flags);
    write_le32(&record[RECORD_OFFSET_PAYLOAD_LENGTH], payload_length);
    const uint32_t payload_crc = pota_crc32_compute(payload, payload_length);
    write_le32(&record[RECORD_OFFSET_PAYLOAD_CRC], payload_crc);
    const uint32_t header_crc =
        pota_crc32_compute(record, RECORD_OFFSET_HEADER_CRC);
    write_le32(&record[RECORD_OFFSET_HEADER_CRC], header_crc);
    write_le32(&record[RECORD_OFFSET_COMMIT],
               commit_marker(generation, sequence));
    if (payload_length != 0u) {
        memcpy(&record[FLASH_STORE_RECORD_HEADER_SIZE], payload,
               payload_length);
    }
    *record_size = FLASH_STORE_RECORD_HEADER_SIZE + payload_length;
    return FLASH_STORE_RECORD_OK;
}

flash_store_record_result_t flash_store_record_decode(
    const uint8_t *record,
    size_t record_size,
    uint32_t expected_schema_version,
    uint32_t expected_object_type,
    uint32_t known_flags_mask,
    uint8_t *payload,
    size_t payload_capacity,
    flash_store_record_header_t *header)
{
    if (record == NULL || header == NULL || expected_schema_version == 0u ||
        expected_object_type == 0u) {
        return FLASH_STORE_RECORD_BAD_ARGUMENT;
    }
    if (record_size < FLASH_STORE_RECORD_HEADER_SIZE) {
        return FLASH_STORE_RECORD_TRUNCATED;
    }
    if (read_le32(&record[RECORD_OFFSET_MAGIC]) != FLASH_STORE_RECORD_MAGIC) {
        return FLASH_STORE_RECORD_MAGIC_MISMATCH;
    }

    decode_header(record, header);
    if (header->schema_version != expected_schema_version) {
        return FLASH_STORE_RECORD_SCHEMA_MISMATCH;
    }
    if (header->object_type != expected_object_type) {
        return FLASH_STORE_RECORD_OBJECT_MISMATCH;
    }
    if (header->generation == 0u || header->sequence == 0u) {
        return FLASH_STORE_RECORD_GENERATION_INVALID;
    }
    if ((header->flags & ~known_flags_mask) != 0u) {
        return FLASH_STORE_RECORD_FLAGS;
    }
    const size_t encoded_size = FLASH_STORE_RECORD_HEADER_SIZE +
                                 (size_t)header->payload_length;
    if (header->payload_length >
            record_size - FLASH_STORE_RECORD_HEADER_SIZE ||
        (record_size > encoded_size &&
         !all_erased(&record[encoded_size], record_size - encoded_size)) ||
        header->payload_length > payload_capacity ||
        (header->payload_length != 0u && payload == NULL)) {
        return FLASH_STORE_RECORD_RANGE;
    }
    if (header->header_crc32 !=
        pota_crc32_compute(record, RECORD_OFFSET_HEADER_CRC)) {
        return FLASH_STORE_RECORD_HEADER_CRC;
    }
    if (header->commit_marker !=
        commit_marker(header->generation, header->sequence)) {
        return FLASH_STORE_RECORD_COMMIT;
    }
    if (pota_crc32_compute(&record[FLASH_STORE_RECORD_HEADER_SIZE],
                           header->payload_length) != header->payload_crc32) {
        return FLASH_STORE_RECORD_PAYLOAD_CRC;
    }
    if (header->payload_length != 0u) {
        memcpy(payload, &record[FLASH_STORE_RECORD_HEADER_SIZE],
               header->payload_length);
    }
    return FLASH_STORE_RECORD_OK;
}

bool flash_store_record_is_newer(const flash_store_record_header_t *candidate,
                                 const flash_store_record_header_t *current)
{
    if (candidate == NULL || current == NULL || candidate->generation == 0u ||
        candidate->sequence == 0u || current->generation == 0u ||
        current->sequence == 0u) {
        return false;
    }
    if (candidate->generation != current->generation) {
        return serial_newer(candidate->generation, current->generation);
    }
    return serial_newer(candidate->sequence, current->sequence);
}
