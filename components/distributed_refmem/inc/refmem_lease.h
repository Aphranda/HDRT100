#ifndef REFMEM_LEASE_H
#define REFMEM_LEASE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * REFMEM N3.3 minimal lease record.
 *
 * The lease is a pure, single-writer state machine over a fixed record.  It
 * grants exclusive command authority to one owner for a bounded time; a second
 * owner gets CONFLICT, a stale epoch is rejected, and an expired lease can be
 * taken by a new epoch.  The record is intended to live in the existing node
 * control / REFMEM table; this module owns only the transition rules.
 */

#define REFMEM_LEASE_DEFAULT_TTL_MS 1000u
#define REFMEM_LEASE_MAX_TTL_MS 60000u

typedef enum {
    REFMEM_LEASE_FREE = 0u,
    REFMEM_LEASE_GRANTED = 1u,
    REFMEM_LEASE_EXPIRED = 2u,
} refmem_lease_state_t;

typedef enum {
    REFMEM_LEASE_OK = 0u,
    REFMEM_LEASE_CONFLICT = 1u,
    REFMEM_LEASE_STALE_EPOCH = 2u,
    REFMEM_LEASE_NOT_OWNER = 3u,
    REFMEM_LEASE_RESULT_EXPIRED = 4u,
    REFMEM_LEASE_BAD_ARGUMENT = 5u,
} refmem_lease_result_t;

typedef struct {
    uint32_t owner_slot;
    uint32_t epoch;
    uint32_t lease_seq;
    uint32_t expiry_ms;
    uint8_t state;
    uint8_t reserved[3];
} refmem_lease_record_t;

typedef struct {
    uint32_t owner_slot;
    uint32_t epoch;
    uint32_t lease_seq;
    uint32_t expiry_ms;
    uint8_t state;
} refmem_lease_snapshot_t;

void refmem_lease_init(refmem_lease_record_t *record);

/* Grant (or idempotently renew) the lease for owner_slot/epoch. */
refmem_lease_result_t refmem_lease_take(refmem_lease_record_t *record,
                                        uint32_t owner_slot,
                                        uint32_t epoch,
                                        uint32_t now_ms,
                                        uint32_t ttl_ms);

refmem_lease_result_t refmem_lease_release(refmem_lease_record_t *record,
                                           uint32_t owner_slot,
                                           uint32_t epoch);

/* Mark a granted lease expired when its deadline has passed. */
void refmem_lease_expire(refmem_lease_record_t *record, uint32_t now_ms);

bool refmem_lease_get(const refmem_lease_record_t *record,
                      refmem_lease_snapshot_t *snapshot);

const char *refmem_lease_result_name(refmem_lease_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_LEASE_H */
