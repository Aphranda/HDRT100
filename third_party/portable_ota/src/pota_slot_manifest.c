#include "pota_slot_manifest.h"

#include <stddef.h>
#include <string.h>

#include "pota_types.h"

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t map_version;
    uint32_t sequence;
    uint32_t slot;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t body_crc32;
    uint8_t header[POTA_PACKAGE_HEADER_SIZE];
    uint8_t reserved[POTA_SLOT_MANIFEST_BODY_SIZE - 32u -
                     POTA_PACKAGE_HEADER_SIZE];
} pota_slot_manifest_body_t;

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t map_version;
    uint32_t sequence;
    uint32_t slot;
    uint32_t body_crc32;
    uint32_t marker;
    uint8_t reserved[POTA_SLOT_MANIFEST_COMMIT_SIZE - 28u];
} pota_slot_manifest_commit_t;

_Static_assert(sizeof(pota_slot_manifest_body_t) ==
                   POTA_SLOT_MANIFEST_BODY_SIZE,
               "slot manifest body layout drift");
_Static_assert(sizeof(pota_slot_manifest_commit_t) ==
                   POTA_SLOT_MANIFEST_COMMIT_SIZE,
               "slot manifest commit layout drift");

static bool power_of_two(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool sequence_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

static uint32_t lane_offset(const pota_slot_manifest_store_t *store,
                            uint32_t lane)
{
    return store->config.base_offset + lane * store->config.lane_size;
}

static uint32_t body_crc(const pota_slot_manifest_body_t *body)
{
    pota_slot_manifest_body_t copy = *body;
    copy.body_crc32 = 0u;
    return pota_crc32_compute(&copy, sizeof(copy));
}

static pota_slot_manifest_result_t read_lane(
    const pota_slot_manifest_store_t *store,
    uint32_t lane,
    pota_slot_manifest_t *manifest,
    pota_slot_manifest_body_t *body_out)
{
    pota_slot_manifest_body_t body;
    pota_slot_manifest_commit_t commit;
    const uint32_t offset = lane_offset(store, lane);
    if (!store->config.read(store->config.context, offset, &body,
                            sizeof(body)) ||
        !store->config.read(store->config.context,
                            offset + POTA_SLOT_MANIFEST_BODY_SIZE,
                            &commit, sizeof(commit))) {
        return POTA_SLOT_MANIFEST_IO;
    }

    if (body.magic == 0xFFFFFFFFu && commit.magic == 0xFFFFFFFFu) {
        return POTA_SLOT_MANIFEST_NO_VALID;
    }
    const uint32_t computed_body_crc = body_crc(&body);
    if (body.magic != POTA_SLOT_MANIFEST_BODY_MAGIC ||
        body.schema_version != POTA_SLOT_MANIFEST_SCHEMA_VERSION ||
        body.map_version != store->config.map_version ||
        body.slot != (uint32_t)store->config.slot ||
        body.header_size != POTA_PACKAGE_HEADER_SIZE ||
        body.header_crc32 !=
            pota_crc32_compute(body.header, sizeof(body.header)) ||
        body.body_crc32 != computed_body_crc ||
        commit.magic != POTA_SLOT_MANIFEST_COMMIT_MAGIC ||
        commit.schema_version != POTA_SLOT_MANIFEST_SCHEMA_VERSION ||
        commit.map_version != body.map_version ||
        commit.sequence != body.sequence || commit.slot != body.slot ||
        commit.body_crc32 != body.body_crc32 ||
        commit.marker !=
            (POTA_SLOT_MANIFEST_COMMIT_MARKER ^ body.sequence)) {
        return POTA_SLOT_MANIFEST_CORRUPT;
    }

    if (manifest != NULL) {
        manifest->sequence = body.sequence;
        manifest->lane = lane;
        manifest->slot = store->config.slot;
        memcpy(manifest->header, body.header, sizeof(manifest->header));
    }
    if (body_out != NULL) {
        *body_out = body;
    }
    return POTA_SLOT_MANIFEST_OK;
}

pota_slot_manifest_result_t pota_slot_manifest_init(
    pota_slot_manifest_store_t *store,
    const pota_slot_manifest_config_t *config)
{
    if (store == NULL || config == NULL || config->read == NULL ||
        ((config->program == NULL) != (config->erase == NULL)) ||
        config->map_version == 0u ||
        (config->slot != POTA_SLOT_A && config->slot != POTA_SLOT_B) ||
        !power_of_two(config->page_size) ||
        !power_of_two(config->erase_size) ||
        config->page_size > config->erase_size ||
        config->lane_size < POTA_SLOT_MANIFEST_RECORD_SIZE ||
        (config->lane_size % config->erase_size) != 0u ||
        (config->base_offset % config->erase_size) != 0u ||
        (POTA_SLOT_MANIFEST_BODY_SIZE % config->page_size) != 0u ||
        POTA_SLOT_MANIFEST_COMMIT_SIZE != config->page_size) {
        return POTA_SLOT_MANIFEST_BAD_ARGUMENT;
    }
    store->config = *config;
    store->initialized = true;
    return POTA_SLOT_MANIFEST_OK;
}

pota_slot_manifest_result_t pota_slot_manifest_load(
    const pota_slot_manifest_store_t *store,
    pota_slot_manifest_t *manifest)
{
    if (store == NULL || !store->initialized || manifest == NULL) {
        return POTA_SLOT_MANIFEST_BAD_ARGUMENT;
    }

    bool found = false;
    bool io_failed = false;
    pota_slot_manifest_t newest;
    for (uint32_t lane = 0u; lane < POTA_SLOT_MANIFEST_LANE_COUNT; lane++) {
        pota_slot_manifest_t candidate;
        const pota_slot_manifest_result_t result =
            read_lane(store, lane, &candidate, NULL);
        if (result == POTA_SLOT_MANIFEST_IO) {
            io_failed = true;
        } else if (result == POTA_SLOT_MANIFEST_OK &&
                   (!found || sequence_newer(candidate.sequence,
                                             newest.sequence))) {
            newest = candidate;
            found = true;
        }
    }
    if (!found) {
        return io_failed ? POTA_SLOT_MANIFEST_IO
                         : POTA_SLOT_MANIFEST_NO_VALID;
    }
    *manifest = newest;
    return POTA_SLOT_MANIFEST_OK;
}

static bool program_pages(const pota_slot_manifest_store_t *store,
                          uint32_t offset,
                          const uint8_t *data,
                          uint32_t length)
{
    for (uint32_t cursor = 0u; cursor < length;
         cursor += store->config.page_size) {
        if (!store->config.program(store->config.context, offset + cursor,
                                   &data[cursor],
                                   store->config.page_size)) {
            return false;
        }
    }
    return true;
}

pota_slot_manifest_result_t pota_slot_manifest_append(
    pota_slot_manifest_store_t *store,
    const uint8_t header[POTA_PACKAGE_HEADER_SIZE],
    pota_slot_manifest_t *committed)
{
    if (store == NULL || !store->initialized || header == NULL) {
        return POTA_SLOT_MANIFEST_BAD_ARGUMENT;
    }

    pota_slot_manifest_t current;
    const pota_slot_manifest_result_t load_result =
        pota_slot_manifest_load(store, &current);
    if (load_result != POTA_SLOT_MANIFEST_OK &&
        load_result != POTA_SLOT_MANIFEST_NO_VALID) {
        return load_result;
    }
    if (load_result == POTA_SLOT_MANIFEST_OK &&
        memcmp(current.header, header, POTA_PACKAGE_HEADER_SIZE) == 0) {
        if (committed != NULL) {
            *committed = current;
        }
        return POTA_SLOT_MANIFEST_OK;
    }

    const uint32_t sequence =
        load_result == POTA_SLOT_MANIFEST_OK ? current.sequence + 1u : 1u;
    if (sequence == 0u) {
        return POTA_SLOT_MANIFEST_ROLLBACK;
    }
    const uint32_t target_lane =
        load_result == POTA_SLOT_MANIFEST_OK ? current.lane ^ 1u : 0u;
    const uint32_t target_offset = lane_offset(store, target_lane);

    pota_slot_manifest_body_t body;
    memset(&body, 0xFF, sizeof(body));
    body.magic = POTA_SLOT_MANIFEST_BODY_MAGIC;
    body.schema_version = POTA_SLOT_MANIFEST_SCHEMA_VERSION;
    body.map_version = store->config.map_version;
    body.sequence = sequence;
    body.slot = (uint32_t)store->config.slot;
    body.header_size = POTA_PACKAGE_HEADER_SIZE;
    memcpy(body.header, header, sizeof(body.header));
    body.header_crc32 = pota_crc32_compute(body.header, sizeof(body.header));
    body.body_crc32 = body_crc(&body);

    pota_slot_manifest_commit_t commit;
    memset(&commit, 0xFF, sizeof(commit));
    commit.magic = POTA_SLOT_MANIFEST_COMMIT_MAGIC;
    commit.schema_version = POTA_SLOT_MANIFEST_SCHEMA_VERSION;
    commit.map_version = store->config.map_version;
    commit.sequence = sequence;
    commit.slot = (uint32_t)store->config.slot;
    commit.body_crc32 = body.body_crc32;
    commit.marker = POTA_SLOT_MANIFEST_COMMIT_MARKER ^ sequence;

    if (!store->config.erase(store->config.context, target_offset,
                             store->config.lane_size) ||
        !program_pages(store, target_offset, (const uint8_t *)&body,
                       sizeof(body))) {
        return POTA_SLOT_MANIFEST_IO;
    }
    pota_slot_manifest_body_t readback;
    if (!store->config.read(store->config.context, target_offset, &readback,
                            sizeof(readback)) ||
        memcmp(&readback, &body, sizeof(body)) != 0 ||
        !store->config.program(store->config.context,
                               target_offset +
                                   POTA_SLOT_MANIFEST_BODY_SIZE,
                               &commit, sizeof(commit))) {
        return POTA_SLOT_MANIFEST_IO;
    }

    pota_slot_manifest_t verified;
    if (read_lane(store, target_lane, &verified, NULL) !=
        POTA_SLOT_MANIFEST_OK) {
        return POTA_SLOT_MANIFEST_CORRUPT;
    }
    if (committed != NULL) {
        *committed = verified;
    }
    return POTA_SLOT_MANIFEST_OK;
}
