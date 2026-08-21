#ifndef FLASH_MAP_H
#define FLASH_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_map_gen/flash_map_v2.h"

typedef enum {
    FLASH_MAP_CONTEXT_BOOT = 0,
    FLASH_MAP_CONTEXT_APP = 1,
    FLASH_MAP_CONTEXT_FACTORY = 2,
} flash_map_context_t;

typedef enum {
    FLASH_MAP_OPERATION_READ = FLASH_MAP_PERMISSION_READ,
    FLASH_MAP_OPERATION_WRITE = FLASH_MAP_PERMISSION_WRITE,
    FLASH_MAP_OPERATION_EXECUTE = FLASH_MAP_PERMISSION_EXECUTE,
} flash_map_operation_t;

typedef struct {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t alignment;
    uint32_t boot_permissions;
    uint32_t app_permissions;
    uint32_t factory_permissions;
    bool executable;
} flash_map_partition_t;

typedef struct {
    flash_map_context_t context;
    uint32_t active_app_partition_id;
    bool scratch_lease;
} flash_map_access_t;

const flash_map_partition_t *flash_map_find_by_id(uint32_t id);
const flash_map_partition_t *flash_map_find(uint32_t absolute_offset,
                                            size_t length);
bool flash_map_relative_range(const flash_map_partition_t *partition,
                              uint32_t relative_offset,
                              size_t length,
                              uint32_t *absolute_offset);
bool flash_map_operation_allowed(const flash_map_access_t *access,
                                 uint32_t partition_id,
                                 flash_map_operation_t operation,
                                 uint32_t relative_offset,
                                 size_t length,
                                 uint32_t *absolute_offset);

#endif
