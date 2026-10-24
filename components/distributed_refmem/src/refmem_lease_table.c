#include "refmem_lease_table.h"

#include <string.h>

static refmem_lease_record_t s_leases[REFMEM_LEASE_TABLE_MAX];

void refmem_lease_table_init(void)
{
    for (uint32_t i = 0u; i < REFMEM_LEASE_TABLE_MAX; i++) {
        refmem_lease_init(&s_leases[i]);
    }
}

refmem_lease_result_t refmem_lease_table_take(uint32_t node_id,
                                              uint32_t owner_slot,
                                              uint32_t epoch,
                                              uint32_t now_ms,
                                              uint32_t ttl_ms)
{
    if (node_id >= REFMEM_LEASE_TABLE_MAX) {
        return REFMEM_LEASE_BAD_ARGUMENT;
    }
    return refmem_lease_take(&s_leases[node_id], owner_slot, epoch, now_ms,
                             ttl_ms);
}

refmem_lease_result_t refmem_lease_table_release(uint32_t node_id,
                                                 uint32_t owner_slot,
                                                 uint32_t epoch)
{
    if (node_id >= REFMEM_LEASE_TABLE_MAX) {
        return REFMEM_LEASE_BAD_ARGUMENT;
    }
    return refmem_lease_release(&s_leases[node_id], owner_slot, epoch);
}

bool refmem_lease_table_get(uint32_t node_id,
                            refmem_lease_snapshot_t *snapshot)
{
    if (node_id >= REFMEM_LEASE_TABLE_MAX) {
        return false;
    }
    return refmem_lease_get(&s_leases[node_id], snapshot);
}
