#ifndef REFMEM_TABLE_REGISTRY_H
#define REFMEM_TABLE_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_application_model.h"

#define REFMEM_TABLE_REGISTRY_VERSION 1u
#define REFMEM_TABLE_REGISTRY_COUNT   9u

#define REFMEM_TABLE_FLAG_ACTIVE_PRESENT  0x00000001u
#define REFMEM_TABLE_FLAG_STAGING_PRESENT 0x00000002u
#define REFMEM_TABLE_FLAG_CRC_OK          0x00000004u
#define REFMEM_TABLE_FLAG_OWNER_OK        0x00000008u
#define REFMEM_TABLE_FLAG_ROLLBACKABLE    0x00000010u

#define REFMEM_TABLE_PACKAGE_MAGIC        0x50544D52u
#define REFMEM_TABLE_PACKAGE_VERSION      1u
#define REFMEM_TABLE_PACKAGE_HEADER_SIZE  64u
#define REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE 16u

typedef enum {
    REFMEM_TABLE_PACKAGE_OK = 0u,
    REFMEM_TABLE_PACKAGE_ERR_TOO_SMALL = 1u,
    REFMEM_TABLE_PACKAGE_ERR_MAGIC = 2u,
    REFMEM_TABLE_PACKAGE_ERR_VERSION = 3u,
    REFMEM_TABLE_PACKAGE_ERR_SIZE = 4u,
    REFMEM_TABLE_PACKAGE_ERR_TABLE_COUNT = 5u,
    REFMEM_TABLE_PACKAGE_ERR_CRC = 6u,
    REFMEM_TABLE_PACKAGE_ERR_TABLE_DIR = 7u,
    REFMEM_TABLE_PACKAGE_ERR_OWNER_VALIDATION = 8u,
} refmem_table_package_error_t;

typedef enum {
    REFMEM_TABLE_OWNER_REFMEM_AO = 0u,
    REFMEM_TABLE_OWNER_SYSTEM_AO = 1u,
    REFMEM_TABLE_OWNER_VDC_AO = 2u,
    REFMEM_TABLE_OWNER_TRIGGER_AO = 3u,
    REFMEM_TABLE_OWNER_GATEWAY_AO = 4u,
} refmem_table_owner_t;

typedef enum {
    REFMEM_TABLE_VALIDATION_EMPTY = 0u,
    REFMEM_TABLE_VALIDATION_STAGED = 1u,
    REFMEM_TABLE_VALIDATION_CRC_OK = 2u,
    REFMEM_TABLE_VALIDATION_OWNER_OK = 3u,
    REFMEM_TABLE_VALIDATION_ACTIVE = 4u,
    REFMEM_TABLE_VALIDATION_ROLLBACKABLE = 5u,
    REFMEM_TABLE_VALIDATION_FAILED = 6u,
} refmem_table_validation_state_t;

typedef enum {
    REFMEM_TABLE_IMAGE_ACTIVE = 0u,
    REFMEM_TABLE_IMAGE_STAGING = 1u,
    REFMEM_TABLE_IMAGE_ROLLBACKABLE = 2u,
} refmem_table_image_role_t;

typedef enum {
    REFMEM_TABLE_ACTIVATE_OK = 0u,
    REFMEM_TABLE_ACTIVATE_ERR_BAD_ARGUMENT = 1u,
    REFMEM_TABLE_ACTIVATE_ERR_NO_VALID_STAGING = 2u,
    REFMEM_TABLE_ACTIVATE_ERR_GATE = 3u,
} refmem_table_activation_result_t;

typedef struct {
    uint32_t version;
    uint32_t role;
    uint32_t state;
    uint32_t table_mask;
    uint32_t package_crc32;
    uint32_t table_seq;
    uint32_t path_hash;
    uint32_t last_result;
    uint32_t evidence_index;
} refmem_table_image_descriptor_t;

typedef struct {
    uint32_t refmem_idle;
    uint32_t realtime_idle;
    uint32_t flash_safe;
    uint32_t crc_ok;
    uint32_t owner_ok;
    uint32_t slot_claim_ok;
    uint32_t deployment_gate_ok;
    uint32_t command_ack_ok;
} refmem_table_activation_gate_t;

typedef struct {
    uint32_t table_id;
    uint32_t owner;
    uint32_t layout_version;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t active_crc32;
    uint32_t staging_crc32;
    uint32_t validation_state;
    uint32_t validator_id;
    uint32_t last_result;
    uint32_t evidence_index;
    uint32_t flags;
} refmem_table_registry_entry_t;

typedef struct {
    uint32_t version;
    uint32_t table_count;
    uint32_t active_table_mask;
    uint32_t staging_table_mask;
    uint32_t registry_crc32;
    uint32_t last_error;
} refmem_table_registry_snapshot_t;

typedef struct {
    uint32_t valid;
    uint32_t error;
    uint32_t format_version;
    uint32_t total_size;
    uint32_t table_count;
    uint32_t payload_crc32;
    uint32_t package_crc32;
    uint32_t first_bad_table;
} refmem_table_package_validation_t;

void refmem_table_registry_init(const refmem_application_model_snapshot_t *model);
void refmem_table_registry_refresh_active(const refmem_application_model_snapshot_t *model);
void refmem_table_registry_refresh_staging(const refmem_application_model_load_snapshot_t *load);
bool refmem_table_registry_validate_staging(const refmem_application_model_load_snapshot_t *load);
bool refmem_table_registry_stage_table(uint32_t table_id,
                                       uint32_t staging_crc32,
                                       uint32_t validation_state,
                                       uint32_t last_result);
bool refmem_table_registry_activate_staging(const refmem_table_activation_gate_t *gate);
bool refmem_table_registry_get_image_descriptor(refmem_table_image_role_t role,
                                                refmem_table_image_descriptor_t *descriptor);
bool refmem_table_registry_validate_package(const uint8_t *data,
                                            size_t size,
                                            refmem_table_package_validation_t *validation);
bool refmem_table_registry_get_entry(uint32_t table_id, refmem_table_registry_entry_t *entry);
void refmem_table_registry_get_snapshot(refmem_table_registry_snapshot_t *snapshot);

#endif
