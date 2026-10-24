#include "refmem_lease_table.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    refmem_lease_table_init();
    refmem_lease_snapshot_t snap;
    assert(refmem_lease_table_get(0u, &snap));
    assert(snap.state == REFMEM_LEASE_FREE);

    assert(refmem_lease_table_take(0u, 0u, 1u, 100u, 100u) ==
           REFMEM_LEASE_OK);
    assert(refmem_lease_table_take(0u, 1u, 2u, 100u, 100u) ==
           REFMEM_LEASE_CONFLICT);
    assert(refmem_lease_table_get(0u, &snap));
    assert(snap.state == REFMEM_LEASE_GRANTED && snap.owner_slot == 0u);

    assert(refmem_lease_table_release(0u, 0u, 1u) == REFMEM_LEASE_OK);
    assert(refmem_lease_table_take(0u, 1u, 2u, 110u, 100u) ==
           REFMEM_LEASE_OK);
    assert(refmem_lease_table_get(0u, &snap));
    assert(snap.owner_slot == 1u && snap.epoch == 2u);

    /* Nodes are independent; out-of-range nodes fail closed. */
    assert(refmem_lease_table_get(1u, &snap));
    assert(snap.state == REFMEM_LEASE_FREE);
    assert(refmem_lease_table_take(REFMEM_LEASE_TABLE_MAX, 0u, 1u, 0u, 10u) ==
           REFMEM_LEASE_BAD_ARGUMENT);
    assert(!refmem_lease_table_get(REFMEM_LEASE_TABLE_MAX, &snap));

    printf("refmem_lease_table: per-node take/conflict/release/take OK\n");
    return 0;
}
