#include "refmem_lease.h"

#include <string.h>

#define REFMEM_LEASE_OWNER_MAX 32u

static bool lease_expired_at(uint32_t expiry_ms, uint32_t now_ms)
{
    return (int32_t)(now_ms - expiry_ms) >= 0;
}

void refmem_lease_init(refmem_lease_record_t *record)
{
    if (record == NULL) {
        return;
    }
    memset(record, 0, sizeof(*record));
    record->state = REFMEM_LEASE_FREE;
}

void refmem_lease_expire(refmem_lease_record_t *record, uint32_t now_ms)
{
    if (record == NULL || record->state != REFMEM_LEASE_GRANTED) {
        return;
    }
    if (lease_expired_at(record->expiry_ms, now_ms)) {
        record->state = REFMEM_LEASE_EXPIRED;
        record->lease_seq++;
    }
}

refmem_lease_result_t refmem_lease_take(refmem_lease_record_t *record,
                                        uint32_t owner_slot,
                                        uint32_t epoch,
                                        uint32_t now_ms,
                                        uint32_t ttl_ms)
{
    if (record == NULL || owner_slot >= REFMEM_LEASE_OWNER_MAX) {
        return REFMEM_LEASE_BAD_ARGUMENT;
    }
    if (ttl_ms == 0u) {
        ttl_ms = REFMEM_LEASE_DEFAULT_TTL_MS;
    }
    if (ttl_ms > REFMEM_LEASE_MAX_TTL_MS) {
        return REFMEM_LEASE_BAD_ARGUMENT;
    }
    refmem_lease_expire(record, now_ms);
    if (epoch < record->epoch) {
        return REFMEM_LEASE_STALE_EPOCH;
    }
    if (record->state == REFMEM_LEASE_GRANTED) {
        if (record->owner_slot == owner_slot && record->epoch == epoch) {
            /* Idempotent take refreshes only the deadline. */
            record->expiry_ms = now_ms + ttl_ms;
            return REFMEM_LEASE_OK;
        }
        return REFMEM_LEASE_CONFLICT;
    }
    record->owner_slot = owner_slot;
    record->epoch = epoch;
    record->expiry_ms = now_ms + ttl_ms;
    record->state = REFMEM_LEASE_GRANTED;
    record->lease_seq++;
    return REFMEM_LEASE_OK;
}

refmem_lease_result_t refmem_lease_release(refmem_lease_record_t *record,
                                           uint32_t owner_slot,
                                           uint32_t epoch)
{
    if (record == NULL || owner_slot >= REFMEM_LEASE_OWNER_MAX) {
        return REFMEM_LEASE_BAD_ARGUMENT;
    }
    if (record->state == REFMEM_LEASE_EXPIRED) {
        return REFMEM_LEASE_RESULT_EXPIRED;
    }
    if (record->state != REFMEM_LEASE_GRANTED) {
        return REFMEM_LEASE_NOT_OWNER;
    }
    if (epoch < record->epoch) {
        return REFMEM_LEASE_STALE_EPOCH;
    }
    if (record->owner_slot != owner_slot || record->epoch != epoch) {
        return REFMEM_LEASE_NOT_OWNER;
    }
    record->state = REFMEM_LEASE_FREE;
    record->owner_slot = 0u;
    record->expiry_ms = 0u;
    record->lease_seq++;
    return REFMEM_LEASE_OK;
}

bool refmem_lease_get(const refmem_lease_record_t *record,
                      refmem_lease_snapshot_t *snapshot)
{
    if (record == NULL || snapshot == NULL) {
        return false;
    }
    snapshot->owner_slot = record->owner_slot;
    snapshot->epoch = record->epoch;
    snapshot->lease_seq = record->lease_seq;
    snapshot->expiry_ms = record->expiry_ms;
    snapshot->state = record->state;
    return true;
}

const char *refmem_lease_result_name(refmem_lease_result_t result)
{
    switch (result) {
    case REFMEM_LEASE_OK:
        return "OK";
    case REFMEM_LEASE_CONFLICT:
        return "CONFLICT";
    case REFMEM_LEASE_STALE_EPOCH:
        return "STALE_EPOCH";
    case REFMEM_LEASE_NOT_OWNER:
        return "NOT_OWNER";
    case REFMEM_LEASE_RESULT_EXPIRED:
        return "EXPIRED";
    case REFMEM_LEASE_BAD_ARGUMENT:
        return "BAD_ARGUMENT";
    default:
        return "UNKNOWN";
    }
}
