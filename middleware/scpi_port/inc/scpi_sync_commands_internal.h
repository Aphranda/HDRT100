#ifndef SCPI_SYNC_COMMANDS_INTERNAL_H
#define SCPI_SYNC_COMMANDS_INTERNAL_H

#include "scpi_sync_commands.h"

/* N5.0.4k: shared helpers.  Definitions stay in scpi_sync_commands.c;
 * the VDC output/priority unit calls them through here. */
/* The verdict itself lives in the VDC manager
 * (vdc_dpll_manager_lock_verdict) so firmware consumers and this page share ONE
 * definition. The values below are the wire numbering of that verdict's reason
 * field and must not be renumbered. */
typedef enum {
    SCPI_SYNC_VDC_LOCK_READY = 0u,
    SCPI_SYNC_VDC_LOCK_SNAPSHOT_UNAVAILABLE = 1u,
    SCPI_SYNC_VDC_LOCK_OBSERVER_DISABLED = 2u,
    SCPI_SYNC_VDC_LOCK_DICTIONARY_EMPTY = 3u,
    SCPI_SYNC_VDC_LOCK_NO_ACCEPTED_SAMPLE = 4u,
    SCPI_SYNC_VDC_LOCK_TIMESTAMP_NOT_ELIGIBLE = 5u,
    SCPI_SYNC_VDC_LOCK_GATE_REJECTED = 6u,
    SCPI_SYNC_VDC_LOCK_NOT_LOCKED = 7u,
    SCPI_SYNC_VDC_LOCK_FOLLOW_DISABLED = 8u,
    SCPI_SYNC_VDC_LOCK_FOLLOW_STALE = 9u,
} scpi_sync_vdc_lock_readiness_reason_t;

uint32_t scpi_sync_vdc_published_lock(void);
void scpi_sync_result_u64_parts(scpi_t *context, uint64_t value);
bool scpi_sync_vdc_timestamp_is_dpll_eligible(uint32_t source,
                                            uint32_t resolution_ns,
                                            uint32_t flags);

#endif /* SCPI_SYNC_COMMANDS_INTERNAL_H */

