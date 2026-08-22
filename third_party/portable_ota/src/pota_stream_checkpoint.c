#include "pota_stream_checkpoint.h"

#include <stddef.h>
#include <string.h>

#include "pota_types.h"

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t sequence;
    pota_stream_checkpoint_t checkpoint;
    uint32_t record_crc32;
    uint32_t commit_marker;
    uint8_t reserved[POTA_STREAM_CHECKPOINT_RECORD_SIZE -
                     3u * sizeof(uint32_t) -
                     sizeof(pota_stream_checkpoint_t) -
                     2u * sizeof(uint32_t)];
} pota_stream_checkpoint_disk_t;

_Static_assert(sizeof(pota_stream_checkpoint_t) == 8u * sizeof(uint32_t),
               "stream checkpoint payload must be eight words");
_Static_assert(sizeof(pota_stream_checkpoint_disk_t) ==
                   POTA_STREAM_CHECKPOINT_RECORD_SIZE,
               "stream checkpoint record size mismatch");

static bool config_valid(const pota_stream_checkpoint_config_t *config)
{
    return config != NULL && config->context != NULL && config->read != NULL &&
           config->program != NULL && config->slot_count != 0u &&
           config->slot_size >= sizeof(pota_stream_checkpoint_disk_t);
}

static uint32_t slot_offset(const pota_stream_checkpoint_store_t *store,
                            uint32_t slot)
{
    return store->config.base_offset + slot * store->config.slot_size;
}

static uint32_t record_crc32(const pota_stream_checkpoint_disk_t *disk)
{
    pota_stream_checkpoint_disk_t copy;
    if (disk == NULL) {
        return 0u;
    }
    copy = *disk;
    copy.record_crc32 = 0u;
    copy.commit_marker = 0xFFFFFFFFu;
    return pota_crc32_compute(&copy, sizeof(copy));
}

static bool disk_blank(const pota_stream_checkpoint_disk_t *disk)
{
    const uint8_t *bytes = (const uint8_t *)disk;
    for (uint32_t index = 0u; index < sizeof(*disk); ++index) {
        if (bytes[index] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static bool disk_valid(const pota_stream_checkpoint_disk_t *disk)
{
    return disk != NULL && disk->magic == POTA_STREAM_CHECKPOINT_MAGIC &&
           disk->schema_version == POTA_STREAM_CHECKPOINT_SCHEMA_VERSION &&
           disk->record_crc32 == record_crc32(disk) &&
           disk->commit_marker == POTA_STREAM_CHECKPOINT_COMMIT_MARKER;
}

static bool identity_equal(const pota_stream_checkpoint_t *left,
                           const pota_stream_checkpoint_t *right)
{
    return left->session_id == right->session_id &&
           left->generation == right->generation &&
           left->token == right->token && left->object_id == right->object_id;
}

static bool stream_metadata_equal(const pota_stream_checkpoint_t *left,
                                  const pota_stream_checkpoint_t *right)
{
    return identity_equal(left, right) &&
           left->total_size == right->total_size &&
           left->package_crc32 == right->package_crc32;
}

pota_stream_checkpoint_result_t pota_stream_checkpoint_init(
    pota_stream_checkpoint_store_t *store,
    const pota_stream_checkpoint_config_t *config)
{
    if (store == NULL || !config_valid(config)) {
        return POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
    }
    memset(store, 0, sizeof(*store));
    store->config = *config;
    store->next_sequence = 1u;
    store->initialized = true;

    pota_stream_checkpoint_t latest;
    uint32_t sequence = 0u;
    if (pota_stream_checkpoint_recover_latest(store, &latest, &sequence) ==
        POTA_STREAM_CHECKPOINT_OK) {
        store->next_sequence = sequence + 1u;
        if (store->next_sequence == 0u) {
            store->next_sequence = 1u;
        }
    }
    return POTA_STREAM_CHECKPOINT_OK;
}

pota_stream_checkpoint_result_t pota_stream_checkpoint_recover_latest(
    const pota_stream_checkpoint_store_t *store,
    pota_stream_checkpoint_t *checkpoint,
    uint32_t *sequence)
{
    if (store == NULL || checkpoint == NULL || sequence == NULL ||
        !store->initialized) {
        return POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
    }

    bool found = false;
    uint32_t newest_sequence = 0u;
    pota_stream_checkpoint_disk_t disk;
    for (uint32_t slot = 0u; slot < store->config.slot_count; ++slot) {
        if (!store->config.read(store->config.context, slot_offset(store, slot),
                                &disk, sizeof(disk))) {
            return POTA_STREAM_CHECKPOINT_IO;
        }
        if (!disk_valid(&disk)) {
            continue;
        }
        if (!found || (int32_t)(disk.sequence - newest_sequence) > 0) {
            found = true;
            newest_sequence = disk.sequence;
            *checkpoint = disk.checkpoint;
        }
    }
    if (!found) {
        return POTA_STREAM_CHECKPOINT_NO_VALID;
    }
    *sequence = newest_sequence;
    return POTA_STREAM_CHECKPOINT_OK;
}

pota_stream_checkpoint_result_t pota_stream_checkpoint_append(
    pota_stream_checkpoint_store_t *store,
    const pota_stream_checkpoint_t *checkpoint)
{
    if (store == NULL || checkpoint == NULL || !store->initialized ||
        checkpoint->session_id == 0u || checkpoint->generation == 0u ||
        checkpoint->token == 0u || checkpoint->object_id == 0u ||
        checkpoint->total_size == 0u ||
        checkpoint->durable_offset > checkpoint->total_size) {
        return POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
    }

    pota_stream_checkpoint_disk_t disk;
    for (uint32_t slot = 0u; slot < store->config.slot_count; ++slot) {
        if (!store->config.read(store->config.context, slot_offset(store, slot),
                                &disk, sizeof(disk))) {
            return POTA_STREAM_CHECKPOINT_IO;
        }
        if (disk_valid(&disk) && identity_equal(&disk.checkpoint, checkpoint)) {
            if (!stream_metadata_equal(&disk.checkpoint, checkpoint)) {
                return POTA_STREAM_CHECKPOINT_CONFLICT;
            }
            if (disk.checkpoint.durable_offset == checkpoint->durable_offset) {
                return disk.checkpoint.chunk_crc32 == checkpoint->chunk_crc32
                           ? POTA_STREAM_CHECKPOINT_OK
                           : POTA_STREAM_CHECKPOINT_CONFLICT;
            }
            if (checkpoint->durable_offset < disk.checkpoint.durable_offset) {
                /* A replay older than the durable cursor must never roll it back. */
                return POTA_STREAM_CHECKPOINT_CONFLICT;
            }
        }
    }

    for (uint32_t slot = 0u; slot < store->config.slot_count; ++slot) {
        const uint32_t offset = slot_offset(store, slot);
        if (!store->config.read(store->config.context, offset, &disk,
                                sizeof(disk))) {
            return POTA_STREAM_CHECKPOINT_IO;
        }
        if (!disk_blank(&disk)) {
            continue;
        }
        memset(&disk, 0xFF, sizeof(disk));
        disk.magic = POTA_STREAM_CHECKPOINT_MAGIC;
        disk.schema_version = POTA_STREAM_CHECKPOINT_SCHEMA_VERSION;
        disk.sequence = store->next_sequence;
        disk.checkpoint = *checkpoint;
        disk.record_crc32 = record_crc32(&disk);
        disk.commit_marker = 0xFFFFFFFFu;
        if (!store->config.program(store->config.context, offset, &disk,
                                   sizeof(disk))) {
            return POTA_STREAM_CHECKPOINT_IO;
        }
        const uint32_t marker = POTA_STREAM_CHECKPOINT_COMMIT_MARKER;
        const uint32_t marker_offset = offset +
            (uint32_t)offsetof(pota_stream_checkpoint_disk_t, commit_marker);
        if (!store->config.program(store->config.context, marker_offset,
                                   &marker, sizeof(marker))) {
            return POTA_STREAM_CHECKPOINT_IO;
        }
        if (!store->config.read(store->config.context, offset, &disk,
                                sizeof(disk)) || !disk_valid(&disk)) {
            return POTA_STREAM_CHECKPOINT_VERIFY;
        }
        store->next_sequence++;
        if (store->next_sequence == 0u) {
            store->next_sequence = 1u;
        }
        return POTA_STREAM_CHECKPOINT_OK;
    }
    return POTA_STREAM_CHECKPOINT_FULL;
}

bool pota_stream_checkpoint_matches(const pota_stream_checkpoint_t *checkpoint,
                                    uint32_t session_id,
                                    uint32_t generation,
                                    uint32_t token,
                                    uint32_t object_id,
                                    uint32_t total_size,
                                    uint32_t package_crc32)
{
    return checkpoint != NULL && checkpoint->session_id == session_id &&
           checkpoint->generation == generation && checkpoint->token == token &&
           checkpoint->object_id == object_id &&
           checkpoint->total_size == total_size &&
           checkpoint->package_crc32 == package_crc32 &&
           checkpoint->durable_offset <= checkpoint->total_size;
}

bool pota_stream_checkpoint_policy_valid(
    const pota_stream_checkpoint_policy_t *policy)
{
    return policy != NULL && policy->interval_bytes != 0u;
}

bool pota_stream_checkpoint_should_append(
    const pota_stream_checkpoint_policy_t *policy,
    uint32_t last_checkpoint_offset,
    uint32_t durable_offset,
    uint32_t total_size)
{
    if (!pota_stream_checkpoint_policy_valid(policy) || total_size == 0u ||
        last_checkpoint_offset > durable_offset || durable_offset > total_size) {
        return false;
    }
    if (durable_offset == 0u) {
        return false;
    }
    if (policy->checkpoint_on_final && durable_offset == total_size) {
        return true;
    }
    return durable_offset - last_checkpoint_offset >= policy->interval_bytes;
}
