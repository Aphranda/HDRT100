#include "ota_metadata.h"

#include <stddef.h>
#include <string.h>

#include "drv_flash.h"
#include "ota_crc32.h"

#define OTA_METADATA_COPY_SIZE    DRV_FLASH_SECTOR_SIZE
#define OTA_METADATA_COPY_A_OFFSET OTA_METADATA_OFFSET
#define OTA_METADATA_COPY_B_OFFSET (OTA_METADATA_OFFSET + OTA_METADATA_COPY_SIZE)
#define OTA_METADATA_VERSION_V2   2u
#define OTA_METADATA_EXT_OFFSET   offsetof(ota_metadata_t, fault_injection_flags)
#define OTA_METADATA_EXT_SIZE     (offsetof(ota_metadata_t, metadata_ext_crc32) + \
                                   sizeof(((ota_metadata_t *)0)->metadata_ext_crc32) - \
                                   OTA_METADATA_EXT_OFFSET)
#define OTA_METADATA_AB_OFFSET    offsetof(ota_metadata_t, boot_mode)
#define OTA_METADATA_AB_SIZE      (sizeof(ota_metadata_t) - OTA_METADATA_AB_OFFSET)

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
    uint8_t slot_a_sha256[32];
    uint32_t slot_b_size;
    uint32_t slot_b_crc32;
    uint8_t slot_b_sha256[32];
    uint32_t last_boot_result;
    uint32_t last_boot_source_slot;
    uint32_t last_boot_size;
    uint32_t last_boot_crc32;
    uint32_t metadata_crc32;
} ota_metadata_v2_t;

static uint32_t ota_metadata_copy_offset(uint32_t copy_index)
{
    return (copy_index == 0u) ? OTA_METADATA_COPY_A_OFFSET : OTA_METADATA_COPY_B_OFFSET;
}

uint32_t ota_metadata_crc32(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    ota_metadata_t copy = *metadata;
    copy.metadata_crc32 = 0u;
    return ota_crc32_compute((const uint8_t *)&copy, sizeof(ota_metadata_v2_t));
}

uint32_t ota_metadata_ext_crc32(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    ota_metadata_t copy = *metadata;
    copy.metadata_ext_crc32 = 0u;
    return ota_crc32_compute(((const uint8_t *)&copy) + OTA_METADATA_EXT_OFFSET,
                             OTA_METADATA_EXT_SIZE);
}

uint32_t ota_metadata_ab_crc32(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    ota_metadata_t copy = *metadata;
    copy.metadata_ab_crc32 = 0u;
    return ota_crc32_compute(((const uint8_t *)&copy) + OTA_METADATA_AB_OFFSET,
                             OTA_METADATA_AB_SIZE);
}

static void ota_metadata_update_crc(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return;
    }

    metadata->metadata_crc32 = ota_metadata_crc32(metadata);
    metadata->metadata_ext_crc32 = ota_metadata_ext_crc32(metadata);
    metadata->metadata_ab_crc32 = ota_metadata_ab_crc32(metadata);
}

static bool ota_copy_txn_state_is_valid(uint32_t state)
{
    return state <= (uint32_t)OTA_COPY_TXN_FAILED;
}

static bool ota_boot_mode_is_valid(uint32_t mode)
{
    return mode <= (uint32_t)OTA_BOOT_MODE_DIRECT_AB;
}

static bool ota_slot_or_none_is_valid(uint32_t slot)
{
    return slot == (uint32_t)OTA_SLOT_NONE ||
           slot == (uint32_t)OTA_SLOT_A ||
           slot == (uint32_t)OTA_SLOT_B;
}

static void ota_metadata_clear_copy_transaction_fields(ota_metadata_t *metadata)
{
    metadata->copy_txn_state = (uint32_t)OTA_COPY_TXN_NONE;
    metadata->copy_source_slot = (uint32_t)OTA_SLOT_NONE;
    metadata->copy_destination_slot = (uint32_t)OTA_SLOT_NONE;
    metadata->copy_size = 0u;
    metadata->copy_crc32 = 0u;
    metadata->copy_written = 0u;
    metadata->copy_attempts = 0u;
    metadata->copy_last_error = 0u;
}

static bool ota_metadata_legacy_extension_tail_is_empty(const ota_metadata_t *metadata)
{
    const uint8_t *ext = ((const uint8_t *)metadata) + offsetof(ota_metadata_t, copy_txn_state);
    const size_t ext_size = OTA_METADATA_EXT_OFFSET + OTA_METADATA_EXT_SIZE -
                            offsetof(ota_metadata_t, copy_txn_state);

    for (size_t i = 0u; i < ext_size; i++) {
        if (ext[i] != 0u && ext[i] != 0xFFu) {
            return false;
        }
    }

    return true;
}

static void ota_metadata_init_extension_defaults(ota_metadata_t *metadata)
{
    metadata->fault_injection_flags = OTA_FAULT_INJECT_NONE;
    ota_metadata_clear_copy_transaction_fields(metadata);
    metadata->metadata_ext_crc32 = 0u;
    metadata->boot_mode = (uint32_t)OTA_BOOT_MODE_COPY_TO_ACTIVE;
    metadata->previous_slot = (uint32_t)OTA_SLOT_NONE;
    metadata->boot_generation = 0u;
    metadata->boot_capabilities = OTA_BOOT_CAP_COPY_TO_ACTIVE;
    metadata->metadata_ab_crc32 = 0u;
}

static uint32_t ota_metadata_v2_crc32(const ota_metadata_v2_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    ota_metadata_v2_t copy = *metadata;
    copy.metadata_crc32 = 0u;
    return ota_crc32_compute((const uint8_t *)&copy, sizeof(copy));
}

static bool ota_metadata_is_valid(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    if (metadata->magic != OTA_METADATA_MAGIC) {
        return false;
    }

    if (metadata->version != OTA_METADATA_VERSION) {
        return false;
    }

    if (ota_metadata_crc32(metadata) != metadata->metadata_crc32) {
        return false;
    }

    if (metadata->metadata_ext_crc32 == 0u ||
        metadata->metadata_ext_crc32 == 0xFFFFFFFFu) {
        return ota_metadata_legacy_extension_tail_is_empty(metadata);
    }

    if (!ota_copy_txn_state_is_valid(metadata->copy_txn_state)) {
        return false;
    }

    if (ota_metadata_ext_crc32(metadata) != metadata->metadata_ext_crc32) {
        return false;
    }

    if (metadata->metadata_ab_crc32 == 0u ||
        metadata->metadata_ab_crc32 == 0xFFFFFFFFu) {
        return true;
    }

    if (!ota_boot_mode_is_valid(metadata->boot_mode) ||
        !ota_slot_or_none_is_valid(metadata->previous_slot)) {
        return false;
    }

    return ota_metadata_ab_crc32(metadata) == metadata->metadata_ab_crc32;
}

static bool ota_metadata_v2_is_valid(const ota_metadata_v2_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    if ((metadata->magic != OTA_METADATA_MAGIC) ||
        (metadata->version != OTA_METADATA_VERSION_V2)) {
        return false;
    }

    return ota_metadata_v2_crc32(metadata) == metadata->metadata_crc32;
}

static void ota_metadata_from_v2(const ota_metadata_v2_t *old_metadata,
                                 ota_metadata_t *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = old_metadata->magic;
    metadata->version = OTA_METADATA_VERSION;
    metadata->sequence = old_metadata->sequence;
    metadata->active_slot = old_metadata->active_slot;
    metadata->pending_slot = old_metadata->pending_slot;
    metadata->confirmed_slot = old_metadata->confirmed_slot;
    metadata->boot_attempts = old_metadata->boot_attempts;
    metadata->rollback_count = old_metadata->rollback_count;
    metadata->slot_a_size = old_metadata->slot_a_size;
    metadata->slot_a_crc32 = old_metadata->slot_a_crc32;
    memcpy(metadata->slot_a_sha256, old_metadata->slot_a_sha256, sizeof(metadata->slot_a_sha256));
    metadata->slot_b_size = old_metadata->slot_b_size;
    metadata->slot_b_crc32 = old_metadata->slot_b_crc32;
    memcpy(metadata->slot_b_sha256, old_metadata->slot_b_sha256, sizeof(metadata->slot_b_sha256));
    metadata->last_boot_result = old_metadata->last_boot_result;
    metadata->last_boot_source_slot = old_metadata->last_boot_source_slot;
    metadata->last_boot_size = old_metadata->last_boot_size;
    metadata->last_boot_crc32 = old_metadata->last_boot_crc32;
    ota_metadata_init_extension_defaults(metadata);
    ota_metadata_update_crc(metadata);
}

static void ota_metadata_set_default(ota_metadata_t *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = OTA_METADATA_MAGIC;
    metadata->version = OTA_METADATA_VERSION;
    metadata->sequence = 0u;
    metadata->active_slot = (uint32_t)OTA_SLOT_A;
    metadata->confirmed_slot = (uint32_t)OTA_SLOT_A;
    metadata->pending_slot = (uint32_t)OTA_SLOT_NONE;
    metadata->last_boot_result = (uint32_t)OTA_BOOT_RESULT_NONE;
    ota_metadata_init_extension_defaults(metadata);
    ota_metadata_update_crc(metadata);
}

static void ota_metadata_upgrade_if_needed(ota_metadata_t *metadata)
{
    if (metadata->version == OTA_METADATA_VERSION &&
        metadata->metadata_ext_crc32 != 0u &&
        metadata->metadata_ext_crc32 != 0xFFFFFFFFu &&
        metadata->metadata_ab_crc32 != 0u &&
        metadata->metadata_ab_crc32 != 0xFFFFFFFFu) {
        return;
    }

    metadata->version = OTA_METADATA_VERSION;
    if (metadata->last_boot_source_slot > (uint32_t)OTA_SLOT_B) {
        metadata->last_boot_source_slot = (uint32_t)OTA_SLOT_NONE;
    }
    if (metadata->metadata_ext_crc32 == 0u ||
        metadata->metadata_ext_crc32 == 0xFFFFFFFFu) {
        ota_metadata_init_extension_defaults(metadata);
    } else {
        metadata->boot_mode = (uint32_t)OTA_BOOT_MODE_COPY_TO_ACTIVE;
        metadata->previous_slot = (uint32_t)OTA_SLOT_NONE;
        metadata->boot_generation = 0u;
        metadata->boot_capabilities = OTA_BOOT_CAP_COPY_TO_ACTIVE;
        metadata->metadata_ab_crc32 = 0u;
    }
    metadata->sequence++;
    ota_metadata_update_crc(metadata);
}

bool ota_metadata_load(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    ota_metadata_t copies[OTA_METADATA_COPY_COUNT];
    bool valid[OTA_METADATA_COPY_COUNT] = {false, false};

    for (uint32_t i = 0u; i < OTA_METADATA_COPY_COUNT; i++) {
        if (drv_flash_read(ota_metadata_copy_offset(i), &copies[i], sizeof(copies[i]))) {
            valid[i] = ota_metadata_is_valid(&copies[i]);
        }

        if (!valid[i]) {
            ota_metadata_v2_t legacy_copy;
            if (drv_flash_read(ota_metadata_copy_offset(i), &legacy_copy, sizeof(legacy_copy)) &&
                ota_metadata_v2_is_valid(&legacy_copy)) {
                ota_metadata_from_v2(&legacy_copy, &copies[i]);
                valid[i] = true;
            }
        }
    }

    if (valid[0] && valid[1]) {
        *metadata = (copies[0].sequence >= copies[1].sequence) ? copies[0] : copies[1];
        ota_metadata_upgrade_if_needed(metadata);
        return true;
    }

    if (valid[0]) {
        *metadata = copies[0];
        ota_metadata_upgrade_if_needed(metadata);
        return true;
    }

    if (valid[1]) {
        *metadata = copies[1];
        ota_metadata_upgrade_if_needed(metadata);
        return true;
    }

    ota_metadata_set_default(metadata);
    return true;
}

bool ota_metadata_store(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    ota_metadata_t stored_metadata = *metadata;
    ota_metadata_update_crc(&stored_metadata);

    const uint32_t copy_index = stored_metadata.sequence % OTA_METADATA_COPY_COUNT;
    const uint32_t offset = ota_metadata_copy_offset(copy_index);
    uint8_t page[DRV_FLASH_PAGE_SIZE];

    if (!drv_flash_erase(offset, OTA_METADATA_COPY_SIZE)) {
        return false;
    }

    const uint8_t *src = (const uint8_t *)&stored_metadata;
    uint32_t written = 0u;
    while (written < sizeof(stored_metadata)) {
        memset(page, 0xFF, sizeof(page));
        const uint32_t chunk = ((sizeof(stored_metadata) - written) > sizeof(page)) ?
                                   sizeof(page) :
                                   (uint32_t)(sizeof(stored_metadata) - written);
        memcpy(page, &src[written], chunk);
        if (!drv_flash_program(offset + written, page, sizeof(page))) {
            return false;
        }
        written += sizeof(page);
    }

    ota_metadata_t readback;
    if (!drv_flash_read(offset, &readback, sizeof(readback))) {
        return false;
    }

    return memcmp(&readback, &stored_metadata, sizeof(stored_metadata)) == 0;
}

bool ota_metadata_mark_pending(ota_slot_t slot, uint32_t image_size, uint32_t image_crc32)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    metadata.pending_slot = (uint32_t)slot;
    metadata.boot_attempts = 0u;
    metadata.last_boot_result = (uint32_t)OTA_BOOT_RESULT_NONE;
    metadata.last_boot_source_slot = (uint32_t)slot;
    metadata.last_boot_size = image_size;
    metadata.last_boot_crc32 = image_crc32;

    if (slot == OTA_SLOT_A) {
        metadata.slot_a_size = image_size;
        metadata.slot_a_crc32 = image_crc32;
    } else if (slot == OTA_SLOT_B) {
        metadata.slot_b_size = image_size;
        metadata.slot_b_crc32 = image_crc32;
    } else {
        return false;
    }

    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_confirm_active(void)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    metadata.confirmed_slot = metadata.active_slot;
    metadata.pending_slot = (uint32_t)OTA_SLOT_NONE;
    metadata.boot_attempts = 0u;
    metadata.last_boot_result = (uint32_t)OTA_BOOT_RESULT_APPLIED;
    metadata.last_boot_source_slot = metadata.active_slot;
    metadata.last_boot_size = (metadata.active_slot == (uint32_t)OTA_SLOT_A) ?
                                  metadata.slot_a_size :
                                  metadata.slot_b_size;
    metadata.last_boot_crc32 = (metadata.active_slot == (uint32_t)OTA_SLOT_A) ?
                                   metadata.slot_a_crc32 :
                                   metadata.slot_b_crc32;
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_set_boot_mode(ota_boot_mode_t mode)
{
    if (!ota_boot_mode_is_valid((uint32_t)mode)) {
        return false;
    }

    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    metadata.boot_mode = (uint32_t)mode;
    metadata.previous_slot = (uint32_t)OTA_SLOT_NONE;
    metadata.boot_generation = 0u;
    metadata.pending_slot = (uint32_t)OTA_SLOT_NONE;
    metadata.boot_attempts = 0u;
    metadata.boot_capabilities = OTA_BOOT_CAP_COPY_TO_ACTIVE;
    if (mode == OTA_BOOT_MODE_DIRECT_AB) {
        metadata.boot_capabilities |= OTA_BOOT_CAP_DIRECT_AB;
    }
    ota_metadata_clear_copy_transaction_fields(&metadata);
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_set_fault_injection(uint32_t flags)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    metadata.fault_injection_flags = flags;
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_begin_copy_transaction(ota_slot_t source,
                                         ota_slot_t destination,
                                         uint32_t image_size,
                                         uint32_t image_crc32)
{
    if (source == OTA_SLOT_NONE ||
        destination == OTA_SLOT_NONE ||
        source == destination ||
        image_size == 0u) {
        return false;
    }

    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    metadata.copy_txn_state = (uint32_t)OTA_COPY_TXN_STARTED;
    metadata.copy_source_slot = (uint32_t)source;
    metadata.copy_destination_slot = (uint32_t)destination;
    metadata.copy_size = image_size;
    metadata.copy_crc32 = image_crc32;
    metadata.copy_written = 0u;
    metadata.copy_attempts++;
    metadata.copy_last_error = 0u;
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_update_copy_transaction(uint32_t state,
                                          uint32_t written,
                                          uint32_t last_error)
{
    if (!ota_copy_txn_state_is_valid(state) ||
        state == (uint32_t)OTA_COPY_TXN_NONE) {
        return false;
    }

    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (metadata.copy_txn_state == (uint32_t)OTA_COPY_TXN_NONE ||
        written > metadata.copy_size) {
        return false;
    }

    metadata.sequence++;
    metadata.copy_txn_state = state;
    metadata.copy_written = written;
    metadata.copy_last_error = last_error;
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_finish_copy_transaction(void)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (metadata.copy_txn_state == (uint32_t)OTA_COPY_TXN_NONE) {
        return false;
    }

    metadata.sequence++;
    metadata.copy_txn_state = (uint32_t)OTA_COPY_TXN_DONE;
    metadata.copy_written = metadata.copy_size;
    metadata.copy_last_error = 0u;
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_fail_copy_transaction(uint32_t last_error)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (metadata.copy_txn_state == (uint32_t)OTA_COPY_TXN_NONE) {
        return false;
    }

    metadata.sequence++;
    metadata.copy_txn_state = (uint32_t)OTA_COPY_TXN_FAILED;
    metadata.copy_last_error = last_error;
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_clear_copy_transaction(void)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    ota_metadata_clear_copy_transaction_fields(&metadata);
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

bool ota_metadata_corrupt_copy(uint32_t copy_index)
{
    if (copy_index >= OTA_METADATA_COPY_COUNT) {
        return false;
    }

    return drv_flash_erase(ota_metadata_copy_offset(copy_index), OTA_METADATA_COPY_SIZE);
}

bool ota_metadata_repair_copies(void)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    metadata.sequence++;
    ota_metadata_update_crc(&metadata);
    if (!ota_metadata_store(&metadata)) {
        return false;
    }

    metadata.sequence++;
    ota_metadata_update_crc(&metadata);
    return ota_metadata_store(&metadata);
}

const char *ota_metadata_boot_result_to_string(uint32_t result)
{
    switch ((ota_boot_result_t)result) {
    case OTA_BOOT_RESULT_NONE:
        return "NONE";
    case OTA_BOOT_RESULT_APPLIED:
        return "APPLIED";
    case OTA_BOOT_RESULT_NO_PENDING:
        return "NO_PENDING";
    case OTA_BOOT_RESULT_MAX_ATTEMPTS:
        return "MAX_ATTEMPTS";
    case OTA_BOOT_RESULT_STAGE_VALIDATE_FAILED:
        return "STAGE_VALIDATE_FAILED";
    case OTA_BOOT_RESULT_COPY_FAILED:
        return "COPY_FAILED";
    case OTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED:
        return "ACTIVE_VALIDATE_FAILED";
    default:
        return "UNKNOWN";
    }
}
