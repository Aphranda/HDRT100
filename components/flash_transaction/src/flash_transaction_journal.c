#include "flash_transaction_journal.h"

#include <stddef.h>
#include <string.h>

static bool flash_transaction_journal_config_valid(
    const flash_transaction_journal_config_t *config)
{
    if (config == NULL || config->context == NULL || config->read == NULL ||
        config->program == NULL || config->crc32 == NULL ||
        config->slot_count == 0u ||
        config->slot_size < sizeof(flash_transaction_journal_disk_record_t) ||
        config->slot_count > UINT32_MAX / config->slot_size) {
        return false;
    }
    if (config->erase == NULL) {
        return config->erase_size == 0u;
    }
    return config->erase_size >= config->slot_size &&
           (config->erase_size % config->slot_size) == 0u &&
           (config->base_offset % config->erase_size) == 0u &&
           ((config->slot_count * config->slot_size) % config->erase_size) == 0u &&
           config->slot_count * config->slot_size / config->erase_size >= 2u;
}

static uint32_t flash_transaction_journal_slot_offset(
    const flash_transaction_journal_store_t *store, uint32_t slot)
{
    return store->config.base_offset + slot * store->config.slot_size;
}

static bool flash_transaction_journal_is_erased(
    const flash_transaction_journal_disk_record_t *disk)
{
    const uint8_t *bytes = (const uint8_t *)disk;
    for (uint32_t index = 0u; index < sizeof(*disk); index++) {
        if (bytes[index] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static bool flash_transaction_journal_disk_valid(
    const flash_transaction_journal_store_t *store,
    const flash_transaction_journal_disk_record_t *disk)
{
    if (disk->magic != FLASH_TRANSACTION_JOURNAL_MAGIC ||
        disk->schema_version != FLASH_TRANSACTION_JOURNAL_SCHEMA_VERSION ||
        disk->record_length != sizeof(disk->record) ||
        disk->commit_marker != FLASH_TRANSACTION_JOURNAL_COMMIT_MARKER) {
        return false;
    }
    return store->config.crc32((const uint8_t *)&disk->record,
                               sizeof(disk->record)) == disk->record_crc32;
}

static bool flash_transaction_journal_identity_equal(
    const flash_transaction_journal_record_t *left,
    const flash_transaction_journal_record_t *right)
{
    const bool durable_identity = left->request_fingerprint != 0u &&
                                  right->request_fingerprint != 0u &&
                                  left->request_fingerprint ==
                                      right->request_fingerprint;
    return (durable_identity || left->job_id == right->job_id) &&
           (durable_identity ||
            left->transaction_generation == right->transaction_generation) &&
           (durable_identity ||
            left->provider_generation == right->provider_generation) &&
           left->store_generation == right->store_generation &&
           (left->request_fingerprint == 0u ||
            right->request_fingerprint == 0u ||
            left->request_fingerprint == right->request_fingerprint) &&
           left->event == right->event;
}

static bool flash_transaction_journal_transaction_equal(
    const flash_transaction_journal_record_t *left,
    const flash_transaction_journal_record_t *right)
{
    /* transaction_generation is RAM-local and restarts after reset.  Once a
     * non-zero request fingerprint is present, it is the durable identity
     * boundary and safely bridges that reset; conflicting payloads still
     * fail closed because the fingerprint must match. */
    const bool durable_identity = left->request_fingerprint != 0u &&
                                  right->request_fingerprint != 0u &&
                                  left->request_fingerprint ==
                                      right->request_fingerprint;
    return (durable_identity || left->job_id == right->job_id) &&
           (durable_identity ||
            left->transaction_generation == right->transaction_generation) &&
           (durable_identity ||
            left->provider_generation == right->provider_generation) &&
           left->store_generation == right->store_generation &&
           (left->request_fingerprint == 0u ||
            right->request_fingerprint == 0u ||
            left->request_fingerprint == right->request_fingerprint);
}

static bool flash_transaction_journal_write_at_slot(
    flash_transaction_journal_store_t *store,
    const flash_transaction_journal_record_t *record,
    uint32_t slot)
{
    const uint32_t offset = flash_transaction_journal_slot_offset(store, slot);
    flash_transaction_journal_disk_record_t disk;
    memset(&disk, 0xFF, sizeof(disk));
    disk.magic = FLASH_TRANSACTION_JOURNAL_MAGIC;
    disk.schema_version = FLASH_TRANSACTION_JOURNAL_SCHEMA_VERSION;
    disk.sequence = store->next_sequence;
    disk.record_length = sizeof(disk.record);
    disk.record = *record;
    disk.record_crc32 = store->config.crc32(
        (const uint8_t *)&disk.record, sizeof(disk.record));
    disk.commit_marker = 0xFFFFFFFFu;
    if (!store->config.program(store->config.context, offset, &disk,
                               sizeof(disk))) {
        return false;
    }

    const uint32_t commit_offset =
        offset + (uint32_t)offsetof(flash_transaction_journal_disk_record_t,
                                   commit_marker);
    const uint32_t marker = FLASH_TRANSACTION_JOURNAL_COMMIT_MARKER;
    if (!store->config.program(store->config.context, commit_offset,
                               &marker, sizeof(marker))) {
        return false;
    }
    if (!store->config.read(store->config.context, offset, &disk,
                            sizeof(disk)) ||
        !flash_transaction_journal_disk_valid(store, &disk)) {
        return false;
    }
    store->next_sequence++;
    if (store->next_sequence == 0u) {
        store->next_sequence = 1u;
    }
    return true;
}

bool flash_transaction_journal_init(
    flash_transaction_journal_store_t *store,
    const flash_transaction_journal_config_t *config)
{
    if (store == NULL || !flash_transaction_journal_config_valid(config)) {
        return false;
    }
    memset(store, 0, sizeof(*store));
    store->config = *config;
    store->next_sequence = 1u;
    store->initialized = true;

    flash_transaction_journal_record_t latest;
    uint32_t sequence = 0u;
    if (flash_transaction_journal_recover_latest(store, &latest, &sequence)) {
        store->next_sequence = sequence + 1u;
        if (store->next_sequence == 0u) {
            store->next_sequence = 1u;
        }
    }
    return true;
}

bool flash_transaction_journal_append(
    flash_transaction_journal_store_t *store,
    const flash_transaction_journal_record_t *record)
{
    if (store == NULL || record == NULL || !store->initialized) {
        return false;
    }

    flash_transaction_journal_disk_record_t disk;
    bool newest_found = false;
    uint32_t newest_sequence = 0u;
    uint32_t newest_slot = 0u;
    /* Completion replay is idempotent only when the complete record matches.
     * A conflicting payload for the same transaction/event is ambiguous and
     * must fail closed instead of consuming another journal slot. */
    for (uint32_t slot = 0u; slot < store->config.slot_count; slot++) {
        const uint32_t offset = flash_transaction_journal_slot_offset(store, slot);
        if (!store->config.read(store->config.context, offset, &disk,
                                sizeof(disk))) {
            return false;
        }
        if (!flash_transaction_journal_disk_valid(store, &disk)) {
            continue;
        }
        if (!newest_found || (int32_t)(disk.sequence - newest_sequence) > 0) {
            newest_found = true;
            newest_sequence = disk.sequence;
            newest_slot = slot;
        }
        if (flash_transaction_journal_identity_equal(&disk.record, record)) {
            return memcmp(&disk.record, record, sizeof(*record)) == 0;
        }
    }

    for (uint32_t slot = 0u; slot < store->config.slot_count; slot++) {
        const uint32_t offset = flash_transaction_journal_slot_offset(store, slot);
        if (!store->config.read(store->config.context, offset, &disk,
                                sizeof(disk))) {
            return false;
        }
        if (flash_transaction_journal_is_erased(&disk)) {
            return flash_transaction_journal_write_at_slot(store, record, slot);
        }
    }

    if (store->config.erase == NULL || !newest_found) {
        return false;
    }
    const uint32_t slots_per_erase =
        store->config.erase_size / store->config.slot_size;
    const uint32_t erase_block_count =
        store->config.slot_count / slots_per_erase;
    const uint32_t newest_block = newest_slot / slots_per_erase;
    const uint32_t erase_block = (newest_block + 1u) % erase_block_count;
    const uint32_t erase_slot = erase_block * slots_per_erase;
    const uint32_t erase_offset =
        flash_transaction_journal_slot_offset(store, erase_slot);
    if (!store->config.erase(store->config.context, erase_offset,
                             store->config.erase_size)) {
        return false;
    }
    for (uint32_t slot = erase_slot;
         slot < erase_slot + slots_per_erase; slot++) {
        if (!store->config.read(
                store->config.context,
                flash_transaction_journal_slot_offset(store, slot), &disk,
                sizeof(disk)) ||
            !flash_transaction_journal_is_erased(&disk)) {
            return false;
        }
    }
    return flash_transaction_journal_write_at_slot(store, record, erase_slot);
}

bool flash_transaction_journal_recover_latest(
    const flash_transaction_journal_store_t *store,
    flash_transaction_journal_record_t *record,
    uint32_t *sequence)
{
    if (store == NULL || record == NULL || sequence == NULL ||
        !store->initialized) {
        return false;
    }

    bool found = false;
    uint32_t latest_sequence = 0u;
    flash_transaction_journal_disk_record_t disk;
    for (uint32_t slot = 0u; slot < store->config.slot_count; slot++) {
        const uint32_t offset = flash_transaction_journal_slot_offset(store, slot);
        if (!store->config.read(store->config.context, offset, &disk,
                                sizeof(disk))) {
            return false;
        }
        if (!flash_transaction_journal_disk_valid(store, &disk)) {
            continue;
        }
        if (!found || (int32_t)(disk.sequence - latest_sequence) > 0) {
            found = true;
            latest_sequence = disk.sequence;
            *record = disk.record;
        }
    }
    if (!found) {
        return false;
    }
    *sequence = latest_sequence;
    return true;
}

bool flash_transaction_journal_find(
    const flash_transaction_journal_store_t *store,
    const flash_transaction_journal_record_t *identity,
    flash_transaction_journal_record_t *record)
{
    if (store == NULL || identity == NULL || record == NULL ||
        !store->initialized || identity->job_id == 0u ||
        identity->transaction_generation == 0u) {
        return false;
    }

    bool found = false;
    uint32_t newest_sequence = 0u;
    flash_transaction_journal_disk_record_t disk;
    for (uint32_t slot = 0u; slot < store->config.slot_count; slot++) {
        const uint32_t offset = flash_transaction_journal_slot_offset(store, slot);
        if (!store->config.read(store->config.context, offset, &disk,
                                sizeof(disk))) {
            return false;
        }
        if (!flash_transaction_journal_disk_valid(store, &disk) ||
            !flash_transaction_journal_transaction_equal(&disk.record,
                                                          identity)) {
            continue;
        }
        if (!found || (int32_t)(disk.sequence - newest_sequence) > 0) {
            found = true;
            newest_sequence = disk.sequence;
            *record = disk.record;
        }
    }
    return found;
}

bool flash_transaction_journal_completion_retain(void *context)
{
    flash_transaction_journal_store_t *store = context;
    if (store == NULL || !store->initialized ||
        store->retained_refs == UINT32_MAX) {
        return false;
    }
    store->retained_refs++;
    return true;
}

void flash_transaction_journal_completion_release(void *context)
{
    flash_transaction_journal_store_t *store = context;
    if (store != NULL && store->retained_refs != 0u) {
        store->retained_refs--;
    }
}

bool flash_transaction_journal_completion_append(
    void *context, const flash_transaction_journal_record_t *record)
{
    return flash_transaction_journal_append(context, record);
}

static bool flash_transaction_journal_completion_find(
    void *context, const flash_transaction_journal_record_t *identity,
    flash_transaction_journal_record_t *record)
{
    return flash_transaction_journal_find(
        (const flash_transaction_journal_store_t *)context, identity, record);
}

bool flash_transaction_journal_make_completion_lease(
    flash_transaction_journal_store_t *store,
    flash_transaction_completion_lease_t *lease)
{
    if (store == NULL || lease == NULL || !store->initialized) {
        return false;
    }
    lease->context = store;
    lease->retain = flash_transaction_journal_completion_retain;
    lease->release = flash_transaction_journal_completion_release;
    lease->append = flash_transaction_journal_completion_append;
    lease->find = flash_transaction_journal_completion_find;
    return true;
}
