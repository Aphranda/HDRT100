#ifndef REFMEM_SYNC_FRAME_H
#define REFMEM_SYNC_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REFMEM_SYNC_FRAME_MAGIC 0x524Du
#define REFMEM_SYNC_FRAME_VERSION 1u
#define REFMEM_SYNC_FRAME_HEADER_SIZE 36u
#define REFMEM_SYNC_FRAME_PAYLOAD_MAX 256u

typedef enum {
    REFMEM_SYNC_FRAME_HELLO = 1u,
    REFMEM_SYNC_FRAME_EPOCH = 2u,
    REFMEM_SYNC_FRAME_DELTA = 3u,
    REFMEM_SYNC_FRAME_COMMAND = 4u,
    REFMEM_SYNC_FRAME_ACK_NACK = 5u,
    REFMEM_SYNC_FRAME_FENCE = 6u,
    REFMEM_SYNC_FRAME_QUALITY = 7u,
} refmem_sync_frame_type_t;

typedef enum {
    REFMEM_SYNC_FRAME_OK = 0u,
    REFMEM_SYNC_FRAME_BAD_ARGUMENT = 1u,
    REFMEM_SYNC_FRAME_BAD_MAGIC = 2u,
    REFMEM_SYNC_FRAME_BAD_VERSION = 3u,
    REFMEM_SYNC_FRAME_BAD_TYPE = 4u,
    REFMEM_SYNC_FRAME_BAD_HEADER_SIZE = 5u,
    REFMEM_SYNC_FRAME_BAD_PAYLOAD_SIZE = 6u,
    REFMEM_SYNC_FRAME_BAD_FRAME_SIZE = 7u,
    REFMEM_SYNC_FRAME_BAD_HEADER_CRC = 8u,
    REFMEM_SYNC_FRAME_BAD_PAYLOAD_CRC = 9u,
} refmem_sync_frame_result_t;

typedef enum {
    REFMEM_SYNC_FRAME_FLAG_ACK_REQUEST = 0x01u,
    REFMEM_SYNC_FRAME_FLAG_TIMESTAMP_VALID = 0x02u,
    REFMEM_SYNC_FRAME_FLAG_FRAGMENT = 0x04u,
} refmem_sync_frame_flags_t;

typedef struct {
    uint16_t magic;
    uint8_t protocol_version;
    uint8_t frame_type;
    uint8_t header_size;
    uint8_t flags;
    uint16_t payload_size;
    uint8_t source_slot;
    uint8_t target_mask;
    uint32_t epoch_id;
    uint32_t run_id;
    uint32_t seq32;
    uint32_t ack_seq32;
    uint32_t compact_time;
    uint16_t header_crc16;
    uint32_t payload_crc32;
} refmem_sync_frame_header_t;

typedef struct {
    uint32_t build_id_crc32;
    uint32_t layout_version;
    uint32_t application_crc32;
    uint32_t config_crc32;
    uint32_t capability_mask;
    uint32_t io_constraint_mask;
    uint32_t ip_core_mask;
    uint32_t adapter_id;
    uint32_t adapter_caps;
    uint16_t max_payload_size;
    uint16_t preferred_mtu;
} refmem_sync_hello_payload_t;

typedef struct {
    uint32_t table_seq;
    uint32_t layout_crc32;
    uint32_t application_crc32;
    uint32_t config_crc32;
    uint32_t calibration_crc32;
    uint32_t sync_profile_crc32;
    uint32_t quality_epoch;
} refmem_sync_epoch_payload_t;

typedef struct {
    uint16_t delta_id;
    uint8_t slot_id;
    uint8_t payload_kind;
    uint32_t slot_seq;
    uint16_t field_id;
    uint16_t field_offset;
    uint16_t field_width;
    uint32_t dirty_mask;
} refmem_sync_delta_header_t;

typedef struct {
    uint32_t command_seq;
    uint32_t command_type;
    uint32_t command_class;
    uint32_t source_instance;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t payload_kind;
    uint32_t payload_ref;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t timeout_us;
} refmem_sync_command_payload_t;

typedef struct {
    uint32_t command_seq;
    uint32_t delta_seq32;
    uint32_t taken_flags;
    uint32_t ack_flags;
    uint32_t nack_flags;
    uint32_t busy_flags;
    uint32_t timeout_flags;
    uint32_t last_reason;
    uint32_t last_reason_slot;
    uint32_t evidence_index;
} refmem_sync_ack_nack_payload_t;

typedef struct {
    uint32_t fence_seq;
    uint32_t fence_scope;
    uint32_t required_mask;
    uint32_t min_table_seq;
    uint32_t layout_crc32;
    uint32_t application_crc32;
    uint32_t config_crc32;
    uint32_t calibration_crc32;
    uint32_t sync_profile_crc32;
    uint32_t deadline_us;
} refmem_sync_fence_payload_t;

typedef struct {
    uint32_t quality_id;
    uint32_t scope;
    uint32_t source_slot;
    uint32_t target_slot;
    uint32_t seq_expected;
    uint32_t seq_last;
    uint32_t crc_error_count;
    uint32_t stale_count;
    uint32_t drop_count;
    uint32_t late_count;
    uint32_t timeout_count;
    uint32_t last_error;
    uint32_t p99_us;
    uint32_t p999_us;
    uint32_t evidence_index;
} refmem_sync_quality_payload_t;

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
                                   uint16_t payload_size);
bool refmem_sync_frame_encode(const refmem_sync_frame_header_t *header,
                              const void *payload,
                              uint16_t payload_size,
                              uint8_t *frame,
                              size_t frame_capacity,
                              size_t *frame_size);
refmem_sync_frame_result_t refmem_sync_frame_decode_header(
    const uint8_t *frame,
    size_t frame_size,
    refmem_sync_frame_header_t *header);
refmem_sync_frame_result_t refmem_sync_frame_validate(
    const uint8_t *frame,
    size_t frame_size,
    refmem_sync_frame_header_t *header,
    const uint8_t **payload,
    uint16_t *payload_size);
uint32_t refmem_sync_frame_payload_crc32(const void *payload, uint16_t payload_size);
uint16_t refmem_sync_frame_header_crc16(const refmem_sync_frame_header_t *header);
bool refmem_sync_frame_type_is_valid(uint8_t frame_type);

#endif
