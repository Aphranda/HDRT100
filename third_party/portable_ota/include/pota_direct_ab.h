#ifndef POTA_DIRECT_AB_H
#define POTA_DIRECT_AB_H

#include <stdbool.h>
#include <stdint.h>

#include "pota_metadata.h"

typedef enum {
    POTA_DIRECT_AB_DECISION_INVALID = 0,
    POTA_DIRECT_AB_DECISION_NO_PENDING,
    POTA_DIRECT_AB_DECISION_BOOT_PENDING,
    POTA_DIRECT_AB_DECISION_ROLLBACK,
} pota_direct_ab_decision_kind_t;

typedef struct {
    uint32_t kind;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t failed_slot;
    uint32_t rollback_slot;
    uint32_t reason;
    uint32_t attempts_remaining;
} pota_direct_ab_decision_t;

/* Pure Direct A/B policy classification. No Flash or image IO is performed. */
bool pota_direct_ab_decide(const pota_metadata_t *metadata,
                           uint32_t max_boot_attempts,
                           pota_direct_ab_decision_t *decision);

#endif
