#include "refmem_lease.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    refmem_lease_record_t record;
    refmem_lease_init(&record);
    refmem_lease_snapshot_t snap;
    assert(refmem_lease_get(&record, &snap));
    assert(snap.state == REFMEM_LEASE_FREE);

    /* A takes, renews, B conflicts. */
    assert(refmem_lease_take(&record, 0u, 1u, 100u, 50u) ==
           REFMEM_LEASE_OK);
    assert(refmem_lease_get(&record, &snap));
    assert(snap.state == REFMEM_LEASE_GRANTED);
    assert(snap.owner_slot == 0u && snap.epoch == 1u);
    assert(snap.expiry_ms == 150u);
    const uint32_t seq_after_take = snap.lease_seq;
    assert(refmem_lease_take(&record, 0u, 1u, 120u, 50u) ==
           REFMEM_LEASE_OK);
    assert(refmem_lease_get(&record, &snap));
    assert(snap.expiry_ms == 170u);
    assert(snap.lease_seq == seq_after_take);

    assert(refmem_lease_take(&record, 1u, 2u, 120u, 50u) ==
           REFMEM_LEASE_CONFLICT);
    assert(refmem_lease_take(&record, 1u, 1u, 120u, 50u) ==
           REFMEM_LEASE_CONFLICT);
    assert(refmem_lease_get(&record, &snap));
    assert(snap.owner_slot == 0u);

    /* A releases, B takes with the next epoch; A's old epoch is stale. */
    assert(refmem_lease_release(&record, 0u, 1u) == REFMEM_LEASE_OK);
    assert(refmem_lease_get(&record, &snap));
    assert(snap.state == REFMEM_LEASE_FREE);
    assert(refmem_lease_take(&record, 1u, 2u, 130u, 50u) ==
           REFMEM_LEASE_OK);
    assert(refmem_lease_take(&record, 0u, 1u, 130u, 50u) ==
           REFMEM_LEASE_STALE_EPOCH);
    assert(refmem_lease_release(&record, 1u, 2u) == REFMEM_LEASE_OK);
    assert(refmem_lease_release(&record, 1u, 2u) ==
           REFMEM_LEASE_NOT_OWNER);

    /* Expiry is fail-closed: the old owner cannot release an expired lease,
     * a new epoch can take it. */
    assert(refmem_lease_take(&record, 0u, 3u, 1000u, 10u) ==
           REFMEM_LEASE_OK);
    refmem_lease_expire(&record, 1010u);
    assert(refmem_lease_get(&record, &snap));
    assert(snap.state == REFMEM_LEASE_EXPIRED);
    assert(refmem_lease_release(&record, 0u, 3u) == REFMEM_LEASE_RESULT_EXPIRED);
    assert(refmem_lease_take(&record, 1u, 4u, 1010u, 10u) ==
           REFMEM_LEASE_OK);
    assert(refmem_lease_get(&record, &snap));
    assert(snap.state == REFMEM_LEASE_GRANTED);
    assert(snap.owner_slot == 1u && snap.epoch == 4u);

    /* Argument guards. */
    assert(refmem_lease_take(&record, 1u, 4u, 1010u, 60001u) ==
           REFMEM_LEASE_BAD_ARGUMENT);
    assert(refmem_lease_take(&record, 32u, 4u, 1010u, 10u) ==
           REFMEM_LEASE_BAD_ARGUMENT);
    assert(strcmp(refmem_lease_result_name(REFMEM_LEASE_CONFLICT),
                  "CONFLICT") == 0);

    printf("refmem_lease: take/conflict/release/take/expiry OK\n");
    return 0;
}
