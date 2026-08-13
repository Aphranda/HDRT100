#ifndef REFMEM_TABLE_REGISTRY_H
#define REFMEM_TABLE_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_application_model.h"

#define REFMEM_TABLE_REGISTRY_VERSION 1u
#define REFMEM_TABLE_REGISTRY_COUNT   8u

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

typedef struct {
    uint32_t table_id;
    uint32_t owner;
    uint32_t layout_version;
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

void refmem_table_registry_init(const refmem_application_model_snapshot_t *model);
void refmem_table_registry_refresh_active(const refmem_application_model_snapshot_t *model);
void refmem_table_registry_refresh_staging(const refmem_application_model_load_snapshot_t *load);
bool refmem_table_registry_get_entry(uint32_t table_id, refmem_table_registry_entry_t *entry);
void refmem_table_registry_get_snapshot(refmem_table_registry_snapshot_t *snapshot);

#endif
