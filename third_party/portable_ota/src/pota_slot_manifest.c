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

enum {
    MANIFEST_TXN_STATE_ERASE = 1u,
    MANIFEST_TXN_STATE_PROGRAM_BODY,
    MANIFEST_TXN_STATE_READBACK,
    MANIFEST_TXN_STATE_PROGRAM_COMMIT,
    MANIFEST_TXN_STATE_VERIFY,
    MANIFEST_TXN_STATE_DONE,
    MANIFEST_TXN_STATE_FAILED,
};

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

pota_slot_manifest_result_t pota_slot_manifest_txn_begin(
    pota_slot_manifest_txn_t *txn,
    const pota_slot_manifest_store_t *store,
    const uint8_t header[POTA_PACKAGE_HEADER_SIZE])
{
    if (txn == NULL || store == NULL || !store->initialized ||
        header == NULL) {
        return POTA_SLOT_MANIFEST_BAD_ARGUMENT;
    }

    (void)memset(txn, 0, sizeof(*txn));
    txn->config = store->config;
    (void)memcpy(txn->header, header, POTA_PACKAGE_HEADER_SIZE);

    pota_slot_manifest_t current;
    const pota_slot_manifest_result_t loaded =
        pota_slot_manifest_load(store, &current);
    if (loaded != POTA_SLOT_MANIFEST_OK &&
        loaded != POTA_SLOT_MANIFEST_NO_VALID) {
        txn->state = MANIFEST_TXN_STATE_FAILED;
        txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
        return loaded;
    }
    if (loaded == POTA_SLOT_MANIFEST_OK &&
        memcmp(current.header, header, POTA_PACKAGE_HEADER_SIZE) == 0) {
        txn->state = MANIFEST_TXN_STATE_DONE;
        txn->terminal = POTA_SLOT_MANIFEST_STEP_DONE;
        txn->active = true;
        return POTA_SLOT_MANIFEST_OK;
    }

    txn->sequence = loaded == POTA_SLOT_MANIFEST_OK
                        ? current.sequence + 1u
                        : 1u;
    if (txn->sequence == 0u) {
        txn->state = MANIFEST_TXN_STATE_FAILED;
        txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
        return POTA_SLOT_MANIFEST_ROLLBACK;
    }
    txn->target_lane = loaded == POTA_SLOT_MANIFEST_OK ? current.lane ^ 1u : 0u;
    txn->target_offset = lane_offset(store, txn->target_lane);

    pota_slot_manifest_body_t body;
    (void)memset(&body, 0xFF, sizeof(body));
    body.magic = POTA_SLOT_MANIFEST_BODY_MAGIC;
    body.schema_version = POTA_SLOT_MANIFEST_SCHEMA_VERSION;
    body.map_version = store->config.map_version;
    body.sequence = txn->sequence;
    body.slot = (uint32_t)store->config.slot;
    body.header_size = POTA_PACKAGE_HEADER_SIZE;
    (void)memcpy(body.header, header, sizeof(body.header));
    body.header_crc32 = pota_crc32_compute(body.header, sizeof(body.header));
    body.body_crc32 = body_crc(&body);
    (void)memcpy(txn->body, &body, sizeof(body));

    pota_slot_manifest_commit_t commit;
    (void)memset(&commit, 0xFF, sizeof(commit));
    commit.magic = POTA_SLOT_MANIFEST_COMMIT_MAGIC;
    commit.schema_version = POTA_SLOT_MANIFEST_SCHEMA_VERSION;
    commit.map_version = store->config.map_version;
    commit.sequence = txn->sequence;
    commit.slot = (uint32_t)store->config.slot;
    commit.body_crc32 = body.body_crc32;
    commit.marker = POTA_SLOT_MANIFEST_COMMIT_MARKER ^ txn->sequence;
    (void)memcpy(txn->commit, &commit, sizeof(commit));

    txn->state = MANIFEST_TXN_STATE_ERASE;
    txn->terminal = POTA_SLOT_MANIFEST_STEP_PENDING;
    txn->active = true;
    return POTA_SLOT_MANIFEST_OK;
}

pota_slot_manifest_step_result_t pota_slot_manifest_txn_step(
    pota_slot_manifest_txn_t *txn)
{
    if (txn == NULL || !txn->active) {
        return POTA_SLOT_MANIFEST_STEP_FAILED;
    }
    if (txn->state == MANIFEST_TXN_STATE_DONE ||
        txn->state == MANIFEST_TXN_STATE_FAILED) {
        return txn->terminal;
    }

    switch (txn->state) {
    case MANIFEST_TXN_STATE_ERASE:
        if (txn->erase_cursor < txn->config.lane_size) {
            if (txn->config.erase == NULL ||
                !txn->config.erase(txn->config.context,
                                   txn->target_offset + txn->erase_cursor,
                                   txn->config.erase_size)) {
                txn->state = MANIFEST_TXN_STATE_FAILED;
                txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
                return txn->terminal;
            }
            txn->erase_cursor += txn->config.erase_size;
            return POTA_SLOT_MANIFEST_STEP_PENDING;
        }
        txn->state = MANIFEST_TXN_STATE_PROGRAM_BODY;
        return POTA_SLOT_MANIFEST_STEP_PENDING;

    case MANIFEST_TXN_STATE_PROGRAM_BODY:
        if (txn->program_cursor < POTA_SLOT_MANIFEST_BODY_SIZE) {
            if (txn->config.program == NULL ||
                !txn->config.program(txn->config.context,
                                     txn->target_offset + txn->program_cursor,
                                     &txn->body[txn->program_cursor],
                                     txn->config.page_size)) {
                txn->state = MANIFEST_TXN_STATE_FAILED;
                txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
                return txn->terminal;
            }
            txn->program_cursor += txn->config.page_size;
            return POTA_SLOT_MANIFEST_STEP_PENDING;
        }
        txn->state = MANIFEST_TXN_STATE_READBACK;
        return POTA_SLOT_MANIFEST_STEP_PENDING;

    case MANIFEST_TXN_STATE_READBACK:
        if (txn->config.read == NULL ||
            !txn->config.read(txn->config.context, txn->target_offset,
                              txn->readback, POTA_SLOT_MANIFEST_BODY_SIZE) ||
            memcmp(txn->readback, txn->body, POTA_SLOT_MANIFEST_BODY_SIZE) != 0) {
            txn->state = MANIFEST_TXN_STATE_FAILED;
            txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
            return txn->terminal;
        }
        txn->state = MANIFEST_TXN_STATE_PROGRAM_COMMIT;
        return POTA_SLOT_MANIFEST_STEP_PENDING;

    case MANIFEST_TXN_STATE_PROGRAM_COMMIT:
        if (txn->config.program == NULL ||
            !txn->config.program(
                txn->config.context,
                txn->target_offset + POTA_SLOT_MANIFEST_BODY_SIZE,
                txn->commit, POTA_SLOT_MANIFEST_COMMIT_SIZE)) {
            txn->state = MANIFEST_TXN_STATE_FAILED;
            txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
            return txn->terminal;
        }
        txn->state = MANIFEST_TXN_STATE_VERIFY;
        return POTA_SLOT_MANIFEST_STEP_PENDING;

    case MANIFEST_TXN_STATE_VERIFY: {
        pota_slot_manifest_store_t store = {
            .config = txn->config,
            .initialized = true,
        };
        pota_slot_manifest_t verified;
        if (read_lane(&store, txn->target_lane, &verified, NULL) !=
            POTA_SLOT_MANIFEST_OK) {
            txn->state = MANIFEST_TXN_STATE_FAILED;
            txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
            return txn->terminal;
        }
        txn->state = MANIFEST_TXN_STATE_DONE;
        txn->terminal = POTA_SLOT_MANIFEST_STEP_DONE;
        return txn->terminal;
    }

    default:
        txn->state = MANIFEST_TXN_STATE_FAILED;
        txn->terminal = POTA_SLOT_MANIFEST_STEP_FAILED;
        return txn->terminal;
    }
}
