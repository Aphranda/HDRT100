#ifndef REFMEM_LEASE_TABLE_H
#define REFMEM_LEASE_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_lease.h"

#ifdef __cplusplus
extern "C" {
#endif

/* N3.3.2: one lease record per target node.  The target's local node owns the
 * record; the issuer slot is the lease owner.  This table is a thin mapping on
 * top of the pure refmem_lease state machine. */
#define REFMEM_LEASE_TABLE_MAX 8u

void refmem_lease_table_init(void);

refmem_lease_result_t refmem_lease_table_take(uint32_t node_id,
                                              uint32_t owner_slot,
                                              uint32_t epoch,
                                              uint32_t now_ms,
                                              uint32_t ttl_ms);

refmem_lease_result_t refmem_lease_table_release(uint32_t node_id,
                                                 uint32_t owner_slot,
                                                 uint32_t epoch);

bool refmem_lease_table_get(uint32_t node_id,
                            refmem_lease_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_LEASE_TABLE_H */
