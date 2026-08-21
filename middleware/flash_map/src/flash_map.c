#include "flash_map.h"

#define FLASH_MAP_PARTITION_ENTRY(name, id, offset, size, alignment, boot, app, factory, executable) \
    {id, offset, size, alignment, boot, app, factory, executable != 0u},

static const flash_map_partition_t s_partitions[] = {
    FLASH_MAP_PARTITION_TABLE(FLASH_MAP_PARTITION_ENTRY)
};

#undef FLASH_MAP_PARTITION_ENTRY

_Static_assert((sizeof(s_partitions) / sizeof(s_partitions[0])) ==
                   FLASH_MAP_PARTITION_COUNT,
               "generated FlashMap partition count mismatch");

static bool flash_map_operation_valid(flash_map_operation_t operation)
{
    return operation == FLASH_MAP_OPERATION_READ ||
           operation == FLASH_MAP_OPERATION_WRITE ||
           operation == FLASH_MAP_OPERATION_EXECUTE;
}

const flash_map_partition_t *flash_map_find_by_id(uint32_t id)
{
    if (id >= FLASH_MAP_PARTITION_COUNT) {
        return NULL;
    }
    return &s_partitions[id];
}

const flash_map_partition_t *flash_map_find(uint32_t absolute_offset,
                                            size_t length)
{
    if (length == 0u || absolute_offset >= FLASH_GEOMETRY_TOTAL_SIZE_BYTES ||
        length > (size_t)(FLASH_GEOMETRY_TOTAL_SIZE_BYTES - absolute_offset)) {
        return NULL;
    }

    for (size_t index = 0u; index < FLASH_MAP_PARTITION_COUNT; index++) {
        const flash_map_partition_t *partition = &s_partitions[index];
        if (absolute_offset < partition->offset ||
            absolute_offset >= partition->offset + partition->size) {
            continue;
        }
        const uint32_t relative_offset = absolute_offset - partition->offset;
        return length <= (size_t)(partition->size - relative_offset)
                   ? partition
                   : NULL;
    }
    return NULL;
}

bool flash_map_relative_range(const flash_map_partition_t *partition,
                              uint32_t relative_offset,
                              size_t length,
                              uint32_t *absolute_offset)
{
    if (partition == NULL || absolute_offset == NULL || length == 0u ||
        relative_offset >= partition->size ||
        length > (size_t)(partition->size - relative_offset)) {
        return false;
    }
    *absolute_offset = partition->offset + relative_offset;
    return true;
}

static bool flash_map_permissions_for_context(
    const flash_map_partition_t *partition,
    flash_map_context_t context,
    uint32_t *permissions)
{
    switch (context) {
    case FLASH_MAP_CONTEXT_BOOT:
        *permissions = partition->boot_permissions;
        return true;
    case FLASH_MAP_CONTEXT_APP:
        *permissions = partition->app_permissions;
        return true;
    case FLASH_MAP_CONTEXT_FACTORY:
        *permissions = partition->factory_permissions;
        return true;
    default:
        return false;
    }
}

bool flash_map_operation_allowed(const flash_map_access_t *access,
                                 uint32_t partition_id,
                                 flash_map_operation_t operation,
                                 uint32_t relative_offset,
                                 size_t length,
                                 uint32_t *absolute_offset)
{
    const flash_map_partition_t *partition = flash_map_find_by_id(partition_id);
    uint32_t permissions = 0u;
    if (access == NULL || partition == NULL ||
        !flash_map_operation_valid(operation) ||
        !flash_map_relative_range(partition, relative_offset, length,
                                  absolute_offset) ||
        !flash_map_permissions_for_context(partition, access->context,
                                           &permissions) ||
        (permissions & (uint32_t)operation) == 0u) {
        return false;
    }

    if (access->context != FLASH_MAP_CONTEXT_APP) {
        return true;
    }

    const bool is_app_partition = partition_id == FLASH_MAP_APP_A_ID ||
                                  partition_id == FLASH_MAP_APP_B_ID;
    if (is_app_partition &&
        access->active_app_partition_id != FLASH_MAP_APP_A_ID &&
        access->active_app_partition_id != FLASH_MAP_APP_B_ID) {
        return operation != FLASH_MAP_OPERATION_WRITE &&
               operation != FLASH_MAP_OPERATION_EXECUTE;
    }
    if (is_app_partition && operation == FLASH_MAP_OPERATION_WRITE) {
        return partition_id != access->active_app_partition_id;
    }
    if (is_app_partition && operation == FLASH_MAP_OPERATION_EXECUTE) {
        return partition_id == access->active_app_partition_id;
    }
    if (partition_id == FLASH_MAP_SCRATCH_ID &&
        operation == FLASH_MAP_OPERATION_WRITE) {
        return access->scratch_lease;
    }
    return true;
}
