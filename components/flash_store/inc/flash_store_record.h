#ifndef FLASH_STORE_RECORD_H
#define FLASH_STORE_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The envelope is a wire/storage format.  Keep it explicitly little-endian
 * and do not expose a packed C struct as the on-flash representation. */
#define FLASH_STORE_RECORD_MAGIC 0x46535231u
#define FLASH_STORE_RECORD_COMMIT_MARKER 0x434F4D4Du
#define FLASH_STORE_RECORD_HEADER_SIZE 40u

typedef enum {
    FLASH_STORE_RECORD_OK = 0,
    FLASH_STORE_RECORD_BAD_ARGUMENT,
    FLASH_STORE_RECORD_TRUNCATED,
    FLASH_STORE_RECORD_MAGIC_MISMATCH,
    FLASH_STORE_RECORD_SCHEMA_MISMATCH,
    FLASH_STORE_RECORD_OBJECT_MISMATCH,
    FLASH_STORE_RECORD_GENERATION_INVALID,
    FLASH_STORE_RECORD_RANGE,
    FLASH_STORE_RECORD_FLAGS,
    FLASH_STORE_RECORD_PAYLOAD_CRC,
    FLASH_STORE_RECORD_HEADER_CRC,
    FLASH_STORE_RECORD_COMMIT,
} flash_store_record_result_t;

typedef struct {
    uint32_t schema_version;
    uint32_t object_type;
    uint32_t generation;
    uint32_t sequence;
    uint32_t flags;
    uint32_t payload_length;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    uint32_t commit_marker;
} flash_store_record_header_t;

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
    size_t *record_size);

flash_store_record_result_t flash_store_record_decode(
    const uint8_t *record,
    size_t record_size,
    uint32_t expected_schema_version,
    uint32_t expected_object_type,
    uint32_t known_flags_mask,
    uint8_t *payload,
    size_t payload_capacity,
    flash_store_record_header_t *header);

/* Generation and sequence use serial-number arithmetic; zero is reserved. */
bool flash_store_record_is_newer(const flash_store_record_header_t *candidate,
                                 const flash_store_record_header_t *current);

#endif
