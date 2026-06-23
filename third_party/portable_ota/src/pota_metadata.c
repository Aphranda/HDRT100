#include "pota_metadata.h"

#include <stddef.h>
#include <string.h>

#define POTA_METADATA_EXT_OFFSET offsetof(pota_metadata_t, fault_injection_flags)
#define POTA_METADATA_EXT_SIZE   (offsetof(pota_metadata_t, metadata_ext_crc32) + \
                                  sizeof(((pota_metadata_t *)0)->metadata_ext_crc32) - \
                                  POTA_METADATA_EXT_OFFSET)
#define POTA_METADATA_AB_OFFSET  offsetof(pota_metadata_t, boot_mode)
#define POTA_METADATA_AB_SIZE    (sizeof(pota_metadata_t) - POTA_METADATA_AB_OFFSET)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t confirmed_slot;
    uint32_t boot_attempts;
    uint32_t rollback_count;
    uint32_t slot_a_size;
    uint32_t slot_a_crc32;
    uint8_t slot_a_sha256[POTA_SHA256_SIZE];
    uint32_t slot_b_size;
    uint32_t slot_b_crc32;
    uint8_t slot_b_sha256[POTA_SHA256_SIZE];
    uint32_t last_boot_result;
    uint32_t last_boot_source_slot;
    uint32_t last_boot_size;
    uint32_t last_boot_crc32;
    uint32_t metadata_crc32;
} pota_metadata_v2_t;

uint32_t pota_metadata_crc32(const pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    pota_metadata_t copy = *metadata;
    copy.metadata_crc32 = 0u;
    return pota_crc32_compute(&copy, sizeof(pota_metadata_v2_t));
}

uint32_t pota_metadata_ext_crc32(const pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    pota_metadata_t copy = *metadata;
    copy.metadata_ext_crc32 = 0u;
    return pota_crc32_compute(((const uint8_t *)&copy) + POTA_METADATA_EXT_OFFSET,
                              POTA_METADATA_EXT_SIZE);
}

uint32_t pota_metadata_ab_crc32(const pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    pota_metadata_t copy = *metadata;
    copy.metadata_ab_crc32 = 0u;
    return pota_crc32_compute(((const uint8_t *)&copy) + POTA_METADATA_AB_OFFSET,
                              POTA_METADATA_AB_SIZE);
}

void pota_metadata_update_crc(pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return;
    }

    metadata->metadata_crc32 = pota_metadata_crc32(metadata);
    metadata->metadata_ext_crc32 = pota_metadata_ext_crc32(metadata);
    metadata->metadata_ab_crc32 = pota_metadata_ab_crc32(metadata);
}

bool pota_metadata_copy_txn_state_is_valid(uint32_t state)
{
    return state <= (uint32_t)POTA_COPY_TXN_FAILED;
}

bool pota_metadata_boot_mode_is_valid(uint32_t mode)
{
    return mode <= (uint32_t)POTA_BOOT_MODE_DIRECT_AB;
}

bool pota_metadata_slot_or_none_is_valid(uint32_t slot)
{
    return slot == (uint32_t)POTA_SLOT_NONE ||
           slot == (uint32_t)POTA_SLOT_A ||
           slot == (uint32_t)POTA_SLOT_B;
}

void pota_metadata_clear_copy_transaction_fields(pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return;
    }

    metadata->copy_txn_state = (uint32_t)POTA_COPY_TXN_NONE;
    metadata->copy_source_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->copy_destination_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->copy_size = 0u;
    metadata->copy_crc32 = 0u;
    metadata->copy_written = 0u;
    metadata->copy_attempts = 0u;
    metadata->copy_last_error = 0u;
}

static bool pota_metadata_legacy_extension_tail_is_empty(const pota_metadata_t *metadata)
{
    const uint8_t *ext = ((const uint8_t *)metadata) + offsetof(pota_metadata_t, copy_txn_state);
    const size_t ext_size = POTA_METADATA_EXT_OFFSET + POTA_METADATA_EXT_SIZE -
                            offsetof(pota_metadata_t, copy_txn_state);

    for (size_t i = 0u; i < ext_size; i++) {
        if (ext[i] != 0u && ext[i] != 0xFFu) {
            return false;
        }
    }

    return true;
}

void pota_metadata_init_extension_defaults(pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return;
    }

    metadata->fault_injection_flags = POTA_FAULT_INJECT_NONE;
    pota_metadata_clear_copy_transaction_fields(metadata);
    metadata->metadata_ext_crc32 = 0u;
    metadata->boot_mode = (uint32_t)POTA_BOOT_MODE_COPY_TO_ACTIVE;
    metadata->previous_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->boot_generation = 0u;
    metadata->boot_capabilities = POTA_BOOT_CAP_COPY_TO_ACTIVE;
    metadata->metadata_ab_crc32 = 0u;
}

bool pota_metadata_is_valid(const pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    if (metadata->magic != POTA_METADATA_MAGIC) {
        return false;
    }

    if (metadata->version != POTA_METADATA_VERSION) {
        return false;
    }

    if (pota_metadata_crc32(metadata) != metadata->metadata_crc32) {
        return false;
    }

    if (metadata->metadata_ext_crc32 == 0u ||
        metadata->metadata_ext_crc32 == 0xFFFFFFFFu) {
        return pota_metadata_legacy_extension_tail_is_empty(metadata);
    }

    if (!pota_metadata_copy_txn_state_is_valid(metadata->copy_txn_state)) {
        return false;
    }

    if (pota_metadata_ext_crc32(metadata) != metadata->metadata_ext_crc32) {
        return false;
    }

    if (metadata->metadata_ab_crc32 == 0u ||
        metadata->metadata_ab_crc32 == 0xFFFFFFFFu) {
        return true;
    }

    if (!pota_metadata_boot_mode_is_valid(metadata->boot_mode) ||
        !pota_metadata_slot_or_none_is_valid(metadata->previous_slot)) {
        return false;
    }

    return pota_metadata_ab_crc32(metadata) == metadata->metadata_ab_crc32;
}

void pota_metadata_set_default(pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return;
    }

    (void)memset(metadata, 0, sizeof(*metadata));
    metadata->magic = POTA_METADATA_MAGIC;
    metadata->version = POTA_METADATA_VERSION;
    metadata->sequence = 0u;
    metadata->active_slot = (uint32_t)POTA_SLOT_A;
    metadata->confirmed_slot = (uint32_t)POTA_SLOT_A;
    metadata->pending_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->last_boot_result = (uint32_t)POTA_BOOT_RESULT_NONE;
    pota_metadata_init_extension_defaults(metadata);
    pota_metadata_update_crc(metadata);
}

void pota_metadata_upgrade_if_needed(pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return;
    }

    if (metadata->version == POTA_METADATA_VERSION &&
        metadata->metadata_ext_crc32 != 0u &&
        metadata->metadata_ext_crc32 != 0xFFFFFFFFu &&
        metadata->metadata_ab_crc32 != 0u &&
        metadata->metadata_ab_crc32 != 0xFFFFFFFFu) {
        return;
    }

    metadata->version = POTA_METADATA_VERSION;
    if (metadata->last_boot_source_slot > (uint32_t)POTA_SLOT_B) {
        metadata->last_boot_source_slot = (uint32_t)POTA_SLOT_NONE;
    }

    if (metadata->metadata_ext_crc32 == 0u ||
        metadata->metadata_ext_crc32 == 0xFFFFFFFFu) {
        pota_metadata_init_extension_defaults(metadata);
    } else {
        metadata->boot_mode = (uint32_t)POTA_BOOT_MODE_COPY_TO_ACTIVE;
        metadata->previous_slot = (uint32_t)POTA_SLOT_NONE;
        metadata->boot_generation = 0u;
        metadata->boot_capabilities = POTA_BOOT_CAP_COPY_TO_ACTIVE;
        metadata->metadata_ab_crc32 = 0u;
    }

    metadata->sequence++;
    pota_metadata_update_crc(metadata);
}

bool pota_metadata_mark_pending(pota_metadata_t *metadata,
                                pota_slot_t slot,
                                uint32_t image_size,
                                uint32_t image_crc32)
{
    if (metadata == NULL || image_size == 0u) {
        return false;
    }

    if (slot != POTA_SLOT_A && slot != POTA_SLOT_B) {
        return false;
    }

    metadata->sequence++;
    metadata->pending_slot = (uint32_t)slot;
    metadata->boot_attempts = 0u;
    metadata->last_boot_result = (uint32_t)POTA_BOOT_RESULT_NONE;
    metadata->last_boot_source_slot = (uint32_t)slot;
    metadata->last_boot_size = image_size;
    metadata->last_boot_crc32 = image_crc32;

    if (slot == POTA_SLOT_A) {
        metadata->slot_a_size = image_size;
        metadata->slot_a_crc32 = image_crc32;
    } else {
        metadata->slot_b_size = image_size;
        metadata->slot_b_crc32 = image_crc32;
    }

    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_can_confirm_active(const pota_metadata_t *metadata)
{
    if (metadata == NULL ||
        (metadata->active_slot != (uint32_t)POTA_SLOT_A &&
         metadata->active_slot != (uint32_t)POTA_SLOT_B)) {
        return false;
    }

    return metadata->pending_slot == (uint32_t)POTA_SLOT_NONE &&
           metadata->last_boot_result == (uint32_t)POTA_BOOT_RESULT_APPLIED;
}

bool pota_metadata_confirm_active(pota_metadata_t *metadata)
{
    if (!pota_metadata_can_confirm_active(metadata)) {
        return false;
    }

    metadata->sequence++;
    metadata->confirmed_slot = metadata->active_slot;
    metadata->pending_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->boot_attempts = 0u;
    metadata->last_boot_result = (uint32_t)POTA_BOOT_RESULT_APPLIED;
    metadata->last_boot_source_slot = metadata->active_slot;
    metadata->last_boot_size = (metadata->active_slot == (uint32_t)POTA_SLOT_A) ?
                                   metadata->slot_a_size :
                                   metadata->slot_b_size;
    metadata->last_boot_crc32 = (metadata->active_slot == (uint32_t)POTA_SLOT_A) ?
                                    metadata->slot_a_crc32 :
                                    metadata->slot_b_crc32;
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_set_boot_mode(pota_metadata_t *metadata, pota_boot_mode_t mode)
{
    if (metadata == NULL || !pota_metadata_boot_mode_is_valid((uint32_t)mode)) {
        return false;
    }

    metadata->sequence++;
    metadata->boot_mode = (uint32_t)mode;
    metadata->previous_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->boot_generation = 0u;
    metadata->pending_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->boot_attempts = 0u;
    metadata->boot_capabilities = POTA_BOOT_CAP_COPY_TO_ACTIVE;
    if (mode == POTA_BOOT_MODE_DIRECT_AB) {
        metadata->boot_capabilities |= POTA_BOOT_CAP_DIRECT_AB;
    }
    pota_metadata_clear_copy_transaction_fields(metadata);
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_set_fault_injection(pota_metadata_t *metadata, uint32_t flags)
{
    if (metadata == NULL) {
        return false;
    }

    metadata->sequence++;
    metadata->fault_injection_flags = flags;
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_begin_copy_transaction(pota_metadata_t *metadata,
                                          pota_slot_t source,
                                          pota_slot_t destination,
                                          uint32_t image_size,
                                          uint32_t image_crc32)
{
    if (metadata == NULL ||
        source == POTA_SLOT_NONE ||
        destination == POTA_SLOT_NONE ||
        source == destination ||
        image_size == 0u) {
        return false;
    }

    metadata->sequence++;
    metadata->copy_txn_state = (uint32_t)POTA_COPY_TXN_STARTED;
    metadata->copy_source_slot = (uint32_t)source;
    metadata->copy_destination_slot = (uint32_t)destination;
    metadata->copy_size = image_size;
    metadata->copy_crc32 = image_crc32;
    metadata->copy_written = 0u;
    metadata->copy_attempts++;
    metadata->copy_last_error = 0u;
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_update_copy_transaction(pota_metadata_t *metadata,
                                           uint32_t state,
                                           uint32_t written,
                                           uint32_t last_error)
{
    if (metadata == NULL ||
        !pota_metadata_copy_txn_state_is_valid(state) ||
        state == (uint32_t)POTA_COPY_TXN_NONE ||
        metadata->copy_txn_state == (uint32_t)POTA_COPY_TXN_NONE ||
        written > metadata->copy_size) {
        return false;
    }

    metadata->sequence++;
    metadata->copy_txn_state = state;
    metadata->copy_written = written;
    metadata->copy_last_error = last_error;
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_finish_copy_transaction(pota_metadata_t *metadata)
{
    if (metadata == NULL ||
        metadata->copy_txn_state == (uint32_t)POTA_COPY_TXN_NONE) {
        return false;
    }

    metadata->sequence++;
    metadata->copy_txn_state = (uint32_t)POTA_COPY_TXN_DONE;
    metadata->copy_written = metadata->copy_size;
    metadata->copy_last_error = 0u;
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_fail_copy_transaction(pota_metadata_t *metadata, uint32_t last_error)
{
    if (metadata == NULL ||
        metadata->copy_txn_state == (uint32_t)POTA_COPY_TXN_NONE) {
        return false;
    }

    metadata->sequence++;
    metadata->copy_txn_state = (uint32_t)POTA_COPY_TXN_FAILED;
    metadata->copy_last_error = last_error;
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_clear_copy_transaction(pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    metadata->sequence++;
    pota_metadata_clear_copy_transaction_fields(metadata);
    pota_metadata_update_crc(metadata);
    return true;
}

static uint32_t pota_metadata_slot_size(const pota_metadata_t *metadata, pota_slot_t slot)
{
    if (metadata == NULL) {
        return 0u;
    }

    return (slot == POTA_SLOT_A) ? metadata->slot_a_size :
           (slot == POTA_SLOT_B) ? metadata->slot_b_size :
                                   0u;
}

static uint32_t pota_metadata_slot_crc32(const pota_metadata_t *metadata, pota_slot_t slot)
{
    if (metadata == NULL) {
        return 0u;
    }

    return (slot == POTA_SLOT_A) ? metadata->slot_a_crc32 :
           (slot == POTA_SLOT_B) ? metadata->slot_b_crc32 :
                                   0u;
}

static bool pota_metadata_slot_is_valid(pota_slot_t slot)
{
    return slot == POTA_SLOT_A || slot == POTA_SLOT_B;
}

bool pota_metadata_record_boot_result(pota_metadata_t *metadata,
                                      pota_boot_result_t result,
                                      pota_slot_t source_slot,
                                      bool clear_pending)
{
    if (metadata == NULL || !pota_metadata_slot_or_none_is_valid((uint32_t)source_slot)) {
        return false;
    }

    metadata->sequence++;
    metadata->last_boot_result = (uint32_t)result;
    metadata->last_boot_source_slot = (uint32_t)source_slot;
    metadata->last_boot_size = pota_metadata_slot_size(metadata, source_slot);
    metadata->last_boot_crc32 = pota_metadata_slot_crc32(metadata, source_slot);

    if (clear_pending) {
        metadata->pending_slot = (uint32_t)POTA_SLOT_NONE;
        metadata->boot_attempts = 0u;
        if (result != POTA_BOOT_RESULT_APPLIED &&
            result != POTA_BOOT_RESULT_NO_PENDING) {
            metadata->rollback_count++;
        }
    }

    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_apply_copy_to_active_done(pota_metadata_t *metadata,
                                             pota_slot_t staging_slot,
                                             pota_slot_t active_slot)
{
    if (metadata == NULL ||
        !pota_metadata_slot_is_valid(staging_slot) ||
        !pota_metadata_slot_is_valid(active_slot)) {
        return false;
    }

    const uint32_t staging_size = pota_metadata_slot_size(metadata, staging_slot);
    const uint32_t staging_crc32 = pota_metadata_slot_crc32(metadata, staging_slot);
    if (staging_size == 0u) {
        return false;
    }

    metadata->active_slot = (uint32_t)active_slot;
    metadata->confirmed_slot = (uint32_t)active_slot;
    metadata->pending_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->boot_attempts = 0u;

    if (active_slot == POTA_SLOT_A) {
        metadata->slot_a_size = staging_size;
        metadata->slot_a_crc32 = staging_crc32;
    } else {
        metadata->slot_b_size = staging_size;
        metadata->slot_b_crc32 = staging_crc32;
    }

    pota_metadata_clear_copy_transaction_fields(metadata);
    return pota_metadata_record_boot_result(metadata,
                                            POTA_BOOT_RESULT_APPLIED,
                                            staging_slot,
                                            false);
}

bool pota_metadata_apply_direct_ab_pending(pota_metadata_t *metadata,
                                           pota_slot_t pending_slot)
{
    if (metadata == NULL ||
        !pota_metadata_slot_is_valid(pending_slot) ||
        !pota_metadata_slot_is_valid((pota_slot_t)metadata->active_slot) ||
        metadata->pending_slot != (uint32_t)pending_slot) {
        return false;
    }

    metadata->previous_slot = metadata->active_slot;
    metadata->active_slot = metadata->pending_slot;
    metadata->pending_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->boot_attempts++;
    metadata->boot_generation++;

    return pota_metadata_record_boot_result(metadata,
                                            POTA_BOOT_RESULT_APPLIED,
                                            pending_slot,
                                            false);
}

bool pota_metadata_rollback_direct_ab(pota_metadata_t *metadata,
                                      pota_boot_result_t reason,
                                      pota_slot_t failed_slot,
                                      pota_slot_t rollback_slot)
{
    if (metadata == NULL ||
        !pota_metadata_slot_is_valid(failed_slot) ||
        !pota_metadata_slot_is_valid(rollback_slot)) {
        return false;
    }

    metadata->sequence++;
    metadata->active_slot = (uint32_t)rollback_slot;
    metadata->previous_slot = (uint32_t)failed_slot;
    metadata->pending_slot = (uint32_t)POTA_SLOT_NONE;
    metadata->boot_attempts = 0u;
    metadata->rollback_count++;
    metadata->last_boot_result = (uint32_t)reason;
    metadata->last_boot_source_slot = (uint32_t)failed_slot;
    metadata->last_boot_size = pota_metadata_slot_size(metadata, failed_slot);
    metadata->last_boot_crc32 = pota_metadata_slot_crc32(metadata, failed_slot);
    pota_metadata_update_crc(metadata);
    return true;
}

bool pota_metadata_increment_boot_attempts(pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    metadata->sequence++;
    metadata->boot_attempts++;
    pota_metadata_update_crc(metadata);
    return true;
}

const pota_metadata_t *pota_metadata_select_newest(const pota_metadata_t *copies,
                                                   size_t copy_count)
{
    if (copies == NULL || copy_count == 0u) {
        return NULL;
    }

    const pota_metadata_t *selected = NULL;
    for (size_t i = 0u; i < copy_count; i++) {
        const pota_metadata_t *candidate = &copies[i];
        if (!pota_metadata_is_valid(candidate)) {
            continue;
        }
        if (selected == NULL || candidate->sequence > selected->sequence) {
            selected = candidate;
        }
    }

    return selected;
}
