#ifndef REFMEM_COMMAND_H
#define REFMEM_COMMAND_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_sync_frame.h"

#define REFMEM_COMMAND_NODE_COUNT 8u
#define REFMEM_COMMAND_INVALID_NODE 0xFFFFFFFFu

typedef enum {
    REFMEM_COMMAND_STATE_IDLE = 0u,
    REFMEM_COMMAND_STATE_POSTED = 1u,
    REFMEM_COMMAND_STATE_TAKEN = 2u,
    REFMEM_COMMAND_STATE_BUSY = 3u,
    REFMEM_COMMAND_STATE_ACKED = 4u,
    REFMEM_COMMAND_STATE_NACKED = 5u,
    REFMEM_COMMAND_STATE_TIMED_OUT = 6u,
} refmem_command_state_t;

typedef enum {
    REFMEM_COMMAND_TYPE_NONE = 0u,
    REFMEM_COMMAND_TYPE_CONFIG_STAGE = 1u,
    REFMEM_COMMAND_TYPE_CONFIG_ACTIVATE = 2u,
    REFMEM_COMMAND_TYPE_START = 3u,
    REFMEM_COMMAND_TYPE_STOP = 4u,
    REFMEM_COMMAND_TYPE_ARM = 5u,
    REFMEM_COMMAND_TYPE_FIRE_LOAD = 6u,
    REFMEM_COMMAND_TYPE_CAL_START = 7u,
    REFMEM_COMMAND_TYPE_CAL_SAVE_LOAD_ACTIVATE = 8u,
    REFMEM_COMMAND_TYPE_SYNC_START_STOP = 9u,
    REFMEM_COMMAND_TYPE_SYNC_RELOCK_HOLDOVER = 10u,
    REFMEM_COMMAND_TYPE_FAULT_CLEAR = 11u,
    REFMEM_COMMAND_TYPE_RESOURCE_JOB = 12u,
    REFMEM_COMMAND_TYPE_MAINTENANCE = 13u,
    REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE = 14u,
    REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE = 15u,
    REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE = 16u,
    REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE = 17u,
} refmem_command_type_t;

typedef enum {
    REFMEM_COMMAND_CLASS_NONE = 0u,
    REFMEM_COMMAND_CLASS_CONFIG = 1u,
    REFMEM_COMMAND_CLASS_RUN = 2u,
    REFMEM_COMMAND_CLASS_REALTIME = 3u,
    REFMEM_COMMAND_CLASS_CALIBRATION = 4u,
    REFMEM_COMMAND_CLASS_SYNC = 5u,
    REFMEM_COMMAND_CLASS_RESOURCE = 6u,
    REFMEM_COMMAND_CLASS_MAINTENANCE = 7u,
} refmem_command_class_t;

typedef enum {
    REFMEM_COMMAND_PAYLOAD_NONE = 0u,
    REFMEM_COMMAND_PAYLOAD_INLINE_SMALL = 1u,
    REFMEM_COMMAND_PAYLOAD_SLOT_REF = 2u,
    REFMEM_COMMAND_PAYLOAD_TLV_REF = 3u,
    REFMEM_COMMAND_PAYLOAD_STAGING_REF = 4u,
} refmem_command_payload_kind_t;

typedef enum {
    REFMEM_COMMAND_REASON_NONE = 0u,
    REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH = 1u,
    REFMEM_COMMAND_REASON_HW_PROFILE_MISMATCH = 2u,
    REFMEM_COMMAND_REASON_NODE_STALE = 3u,
    REFMEM_COMMAND_REASON_NODE_FAULT = 4u,
    REFMEM_COMMAND_REASON_FLASH_LOCKOUT_UNREADY = 5u,
    REFMEM_COMMAND_REASON_RESOURCE_BUSY = 6u,
    REFMEM_COMMAND_REASON_RUN_STATE_DENIED = 7u,
    REFMEM_COMMAND_REASON_PAYLOAD_CRC_MISMATCH = 8u,
    REFMEM_COMMAND_REASON_EPOCH_MISMATCH = 9u,
    REFMEM_COMMAND_REASON_DUP_SEQ_CRC_MISMATCH = 10u,
    REFMEM_COMMAND_REASON_TIMEOUT = 11u,
    REFMEM_COMMAND_REASON_PERMISSION_DENIED = 12u,
    REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED = 13u,
} refmem_command_reason_t;

typedef enum {
    REFMEM_COMMAND_TAKE_TAKEN = 0u,
    REFMEM_COMMAND_TAKE_NO_COMMAND = 1u,
    REFMEM_COMMAND_TAKE_NOT_TARGET = 2u,
    REFMEM_COMMAND_TAKE_ALREADY_COMPLETE = 3u,
    REFMEM_COMMAND_TAKE_EPOCH_MISMATCH = 4u,
    REFMEM_COMMAND_TAKE_PAYLOAD_CRC_MISMATCH = 5u,
} refmem_command_take_result_t;

typedef struct {
    uint32_t command_seq;
    uint32_t source_node;
    uint32_t source_instance;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t command_type;
    uint32_t command_class;
    uint32_t payload_kind;
    uint32_t payload_ref;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t issue_epoch;
    uint32_t run_id;
    uint32_t timeout_1e3ns;
} refmem_command_request_t;

typedef struct {
    uint32_t state;
    uint32_t command_seq;
    uint32_t source_node;
    uint32_t source_instance;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t command_type;
    uint32_t command_class;
    uint32_t payload_kind;
    uint32_t payload_ref;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t issue_epoch;
    uint32_t run_id;
    uint32_t issue_tick32;
    uint32_t timeout_1e3ns;
    uint32_t taken_flags;
    uint32_t ack_flags;
    uint32_t nack_flags;
    uint32_t busy_flags;
    uint32_t timeout_flags;
    uint32_t last_reason;
    uint32_t last_reason_slot;
    uint32_t reason_table_crc32;
    uint32_t evidence_index;
    uint32_t clear_seq;
    uint32_t last_completed_seq;
} refmem_command_snapshot_t;

typedef struct {
    volatile uint32_t guard;
    volatile uint32_t command_seq;
    volatile uint32_t source_node;
    volatile uint32_t source_instance;
    volatile uint32_t target_mask;
    volatile uint32_t required_mask;
    volatile uint32_t command_type;
    volatile uint32_t command_class;
    volatile uint32_t payload_kind;
    volatile uint32_t payload_ref;
    volatile uint32_t payload_size;
    volatile uint32_t payload_crc32;
    volatile uint32_t issue_epoch;
    volatile uint32_t run_id;
    volatile uint32_t issue_tick32;
    volatile uint32_t timeout_1e3ns;
    volatile uint32_t taken_flags;
    volatile uint32_t ack_flags;
    volatile uint32_t nack_flags;
    volatile uint32_t busy_flags;
    volatile uint32_t timeout_flags;
    volatile uint32_t last_reason;
    volatile uint32_t last_reason_slot;
    volatile uint32_t reason_table_crc32;
    volatile uint32_t evidence_index;
    volatile uint32_t clear_seq;
    volatile uint32_t last_completed_seq;
} refmem_command_slot_t;

bool refmem_command_init(refmem_command_slot_t *slot, uint32_t reason_table_crc32);
bool refmem_command_set_reason_table_crc32(refmem_command_slot_t *slot,
                                           uint32_t reason_table_crc32);
bool refmem_command_try_post(refmem_command_slot_t *slot,
                             const refmem_command_request_t *request,
                             uint32_t issue_tick32);
refmem_command_take_result_t refmem_command_try_take(refmem_command_slot_t *slot,
                                                     uint32_t target_node,
                                                     uint32_t active_epoch,
                                                     uint32_t active_run_id,
                                                     uint32_t observed_payload_crc32,
                                                     uint32_t evidence_index);
bool refmem_command_ack(refmem_command_slot_t *slot,
                        uint32_t target_node,
                        uint32_t evidence_index);
bool refmem_command_nack(refmem_command_slot_t *slot,
                         uint32_t target_node,
                         refmem_command_reason_t reason,
                         uint32_t evidence_index);
bool refmem_command_mark_timeout(refmem_command_slot_t *slot,
                                 uint32_t now_tick32,
                                 uint32_t evidence_index);
bool refmem_command_clear(refmem_command_slot_t *slot, uint32_t clear_seq);
bool refmem_command_get_snapshot(const refmem_command_slot_t *slot,
                                 refmem_command_snapshot_t *snapshot);
bool refmem_command_to_sync_command_payload(const refmem_command_snapshot_t *snapshot,
                                            refmem_sync_command_payload_t *payload);
bool refmem_command_to_sync_ack_payload(const refmem_command_snapshot_t *snapshot,
                                        refmem_sync_ack_nack_payload_t *payload);

#endif
