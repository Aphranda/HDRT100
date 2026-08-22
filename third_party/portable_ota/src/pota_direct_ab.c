#include "pota_direct_ab.h"

#include <string.h>

static bool pota_direct_ab_slot_is_valid(uint32_t slot)
{
    return slot == (uint32_t)POTA_SLOT_A || slot == (uint32_t)POTA_SLOT_B;
}

bool pota_direct_ab_decide(const pota_metadata_t *metadata,
                           uint32_t max_boot_attempts,
                           pota_direct_ab_decision_t *decision)
{
    if (metadata == NULL || decision == NULL || max_boot_attempts == 0u) {
        return false;
    }

    memset(decision, 0, sizeof(*decision));
    decision->kind = POTA_DIRECT_AB_DECISION_INVALID;
    decision->active_slot = metadata->active_slot;
    decision->pending_slot = metadata->pending_slot;

    if (!pota_metadata_is_valid(metadata) ||
        metadata->boot_mode != (uint32_t)POTA_BOOT_MODE_DIRECT_AB ||
        !pota_direct_ab_slot_is_valid(metadata->active_slot)) {
        return false;
    }

    if (metadata->pending_slot == (uint32_t)POTA_SLOT_NONE) {
        decision->kind = POTA_DIRECT_AB_DECISION_NO_PENDING;
        decision->attempts_remaining = max_boot_attempts;
        return true;
    }

    if (!pota_direct_ab_slot_is_valid(metadata->pending_slot) ||
        metadata->pending_slot == metadata->active_slot) {
        return false;
    }

    decision->attempts_remaining = metadata->boot_attempts >= max_boot_attempts
                                       ? 0u
                                       : max_boot_attempts - metadata->boot_attempts;
    if (metadata->boot_attempts < max_boot_attempts) {
        decision->kind = POTA_DIRECT_AB_DECISION_BOOT_PENDING;
        return true;
    }

    decision->kind = POTA_DIRECT_AB_DECISION_ROLLBACK;
    decision->failed_slot = metadata->pending_slot;
    decision->reason = (uint32_t)POTA_BOOT_RESULT_MAX_ATTEMPTS;
    if (pota_direct_ab_slot_is_valid(metadata->confirmed_slot) &&
        metadata->confirmed_slot != decision->failed_slot) {
        decision->rollback_slot = metadata->confirmed_slot;
    } else if (pota_direct_ab_slot_is_valid(metadata->previous_slot) &&
               metadata->previous_slot != decision->failed_slot) {
        decision->rollback_slot = metadata->previous_slot;
    } else {
        decision->kind = POTA_DIRECT_AB_DECISION_INVALID;
        return false;
    }
    return true;
}
