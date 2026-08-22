#include "flash_transaction_journal.h"

#include <stddef.h>
#include <string.h>

static bool flash_transaction_journal_config_valid(
    const flash_transaction_journal_config_t *config)
{
    return config != NULL && config->context != NULL && config->read != NULL &&
           config->program != NULL && config->crc32 != NULL &&
           config->slot_count != 0u &&
           config->slot_size >= sizeof(flash_transaction_journal_disk_record_t);
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
    for (uint32_t slot = 0u; slot < store->config.slot_count; slot++) {
        const uint32_t offset = flash_transaction_journal_slot_offset(store, slot);
        if (!store->config.read(store->config.context, offset, &disk,
                                sizeof(disk))) {
            return false;
        }
        if (!flash_transaction_journal_is_erased(&disk)) {
            continue;
        }

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
    return false;
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
    return true;
}
