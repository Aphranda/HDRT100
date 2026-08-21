#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "flash_map.h"

static bool allowed(flash_map_context_t context,
                    uint32_t active_app,
                    bool scratch_lease,
                    uint32_t partition_id,
                    flash_map_operation_t operation,
                    uint32_t relative_offset,
                    size_t length,
                    uint32_t *absolute_offset)
{
    const flash_map_access_t access = {
        .context = context,
        .active_app_partition_id = active_app,
        .scratch_lease = scratch_lease,
    };
    return flash_map_operation_allowed(&access, partition_id, operation,
                                       relative_offset, length,
                                       absolute_offset);
}

static void test_partition_ranges(void)
{
    for (uint32_t id = 0u; id < FLASH_MAP_PARTITION_COUNT; id++) {
        const flash_map_partition_t *partition = flash_map_find_by_id(id);
        assert(partition != NULL);
        assert(partition->id == id);
        assert(flash_map_find(partition->offset, 1u) == partition);
        assert(flash_map_find(partition->offset + partition->size - 1u, 1u) ==
               partition);
        assert(flash_map_find(partition->offset + partition->size - 1u, 2u) ==
               NULL);
        if (id + 1u < FLASH_MAP_PARTITION_COUNT) {
            assert(flash_map_find(partition->offset + partition->size, 1u) ==
                   flash_map_find_by_id(id + 1u));
        }
    }

    assert(flash_map_find_by_id(FLASH_MAP_PARTITION_COUNT) == NULL);
    assert(flash_map_find(0u, 0u) == NULL);
    assert(flash_map_find(FLASH_GEOMETRY_TOTAL_SIZE_BYTES, 1u) == NULL);
    assert(flash_map_find(UINT32_MAX, SIZE_MAX) == NULL);
}

static void test_relative_ranges(void)
{
    const flash_map_partition_t *partition =
        flash_map_find_by_id(FLASH_MAP_APP_A_ID);
    uint32_t absolute = UINT32_MAX;
    assert(flash_map_relative_range(partition, 0u, 1u, &absolute));
    assert(absolute == FLASH_MAP_APP_A_OFFSET);
    assert(flash_map_relative_range(partition, partition->size - 1u, 1u,
                                    &absolute));
    assert(absolute == partition->offset + partition->size - 1u);
    assert(!flash_map_relative_range(partition, 0u, 0u, &absolute));
    assert(!flash_map_relative_range(partition, partition->size, 1u,
                                     &absolute));
    assert(!flash_map_relative_range(partition, partition->size - 1u, 2u,
                                     &absolute));
    assert(!flash_map_relative_range(partition, 0u, SIZE_MAX, &absolute));
    assert(!flash_map_relative_range(NULL, 0u, 1u, &absolute));
    assert(!flash_map_relative_range(partition, 0u, 1u, NULL));
}

static uint32_t context_permissions(const flash_map_partition_t *partition,
                                    flash_map_context_t context)
{
    switch (context) {
    case FLASH_MAP_CONTEXT_BOOT:
        return partition->boot_permissions;
    case FLASH_MAP_CONTEXT_APP:
        return partition->app_permissions;
    case FLASH_MAP_CONTEXT_FACTORY:
        return partition->factory_permissions;
    default:
        return 0u;
    }
}

static void test_generated_permission_views(void)
{
    const flash_map_operation_t operations[] = {
        FLASH_MAP_OPERATION_READ,
        FLASH_MAP_OPERATION_WRITE,
        FLASH_MAP_OPERATION_EXECUTE,
    };
    const flash_map_context_t contexts[] = {
        FLASH_MAP_CONTEXT_BOOT,
        FLASH_MAP_CONTEXT_FACTORY,
    };
    uint32_t absolute = 0u;

    for (size_t c = 0u; c < sizeof(contexts) / sizeof(contexts[0]); c++) {
        for (uint32_t id = 0u; id < FLASH_MAP_PARTITION_COUNT; id++) {
            const flash_map_partition_t *partition = flash_map_find_by_id(id);
            const uint32_t mask = context_permissions(partition, contexts[c]);
            for (size_t op = 0u; op < sizeof(operations) / sizeof(operations[0]);
                 op++) {
                const bool expected = (mask & (uint32_t)operations[op]) != 0u;
                assert(allowed(contexts[c], FLASH_MAP_APP_A_ID, true, id,
                               operations[op], 0u, 1u, &absolute) == expected);
                assert(absolute == partition->offset);
            }
        }
    }
}

static void test_app_dynamic_policy(void)
{
    uint32_t absolute = 0u;

    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_WRITE,
                    0u, 1u, &absolute));
    assert(allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                   FLASH_MAP_APP_B_ID, FLASH_MAP_OPERATION_WRITE,
                   0u, 1u, &absolute));
    assert(allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                   FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_EXECUTE,
                   0u, 1u, &absolute));
    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_APP_B_ID, FLASH_MAP_OPERATION_EXECUTE,
                    0u, 1u, &absolute));

    assert(!allowed(FLASH_MAP_CONTEXT_APP, UINT32_MAX, true,
                    FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_WRITE,
                    0u, 1u, &absolute));
    assert(!allowed(FLASH_MAP_CONTEXT_APP, UINT32_MAX, true,
                    FLASH_MAP_APP_B_ID, FLASH_MAP_OPERATION_EXECUTE,
                    0u, 1u, &absolute));
    assert(allowed(FLASH_MAP_CONTEXT_APP, UINT32_MAX, true,
                   FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_READ,
                   0u, 1u, &absolute));

    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, false,
                    FLASH_MAP_SCRATCH_ID, FLASH_MAP_OPERATION_WRITE,
                    0u, 1u, &absolute));
    assert(allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                   FLASH_MAP_SCRATCH_ID, FLASH_MAP_OPERATION_WRITE,
                   FLASH_MAP_SCRATCH_SIZE - 1u, 1u, &absolute));
    assert(absolute == FLASH_MAP_SCRATCH_OFFSET + FLASH_MAP_SCRATCH_SIZE - 1u);

    for (uint32_t operation = FLASH_MAP_PERMISSION_READ;
         operation <= FLASH_MAP_PERMISSION_EXECUTE; operation <<= 1u) {
        assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                        FLASH_MAP_FUTURE_POOL_ID,
                        (flash_map_operation_t)operation,
                        0u, 1u, &absolute));
    }
}

static void test_fail_closed_inputs(void)
{
    uint32_t absolute = UINT32_MAX;
    assert(!allowed((flash_map_context_t)99, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_READ,
                    0u, 1u, &absolute));
    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_PARTITION_COUNT, FLASH_MAP_OPERATION_READ,
                    0u, 1u, &absolute));
    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_APP_A_ID, (flash_map_operation_t)3,
                    0u, 1u, &absolute));
    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_READ,
                    0u, 0u, &absolute));
    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_READ,
                    FLASH_MAP_APP_A_SIZE - 1u, 2u, &absolute));
    assert(!flash_map_operation_allowed(NULL, FLASH_MAP_APP_A_ID,
                                        FLASH_MAP_OPERATION_READ,
                                        0u, 1u, &absolute));
    assert(!allowed(FLASH_MAP_CONTEXT_APP, FLASH_MAP_APP_A_ID, true,
                    FLASH_MAP_APP_A_ID, FLASH_MAP_OPERATION_READ,
                    0u, 1u, NULL));
}

int main(void)
{
    test_partition_ranges();
    test_relative_ranges();
    test_generated_permission_views();
    test_app_dynamic_policy();
    test_fail_closed_inputs();
    puts("flash_map tests passed");
    return 0;
}
