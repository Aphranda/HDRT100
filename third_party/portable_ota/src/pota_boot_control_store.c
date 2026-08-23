#include "pota_boot_control_store.h"

#include <stddef.h>
#include <string.h>

#include "pota_types.h"

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t map_version;
    uint32_t sequence;
    uint32_t boot_generation;
    uint32_t security_counter;
    uint32_t payload_length;
    uint32_t payload_crc32;
    uint32_t body_crc32;
    uint8_t payload[POTA_BCB_BODY_PAYLOAD_SIZE];
} pota_bcb_body_t;

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t map_version;
    uint32_t lane_generation;
    uint32_t sequence;
    uint32_t body_crc32;
    uint32_t commit_marker;
    uint8_t reserved[POTA_BCB_PAGE_SIZE - 7u * sizeof(uint32_t)];
} pota_bcb_commit_t;

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t map_version;
    uint32_t lane_generation;
    uint32_t seal_crc32;
    uint32_t seal_marker;
    uint8_t reserved[POTA_BCB_PAGE_SIZE - 6u * sizeof(uint32_t)];
} pota_bcb_seal_t;

enum {
    BCB_TXN_STATE_ERASE = 1u,
    BCB_TXN_STATE_PROGRAM_BODY,
    BCB_TXN_STATE_VERIFY_BODY,
    BCB_TXN_STATE_PROGRAM_COMMIT,
    BCB_TXN_STATE_VERIFY_COMMIT,
    BCB_TXN_STATE_PROGRAM_SEAL,
    BCB_TXN_STATE_VERIFY_SEAL,
    BCB_TXN_STATE_DONE,
    BCB_TXN_STATE_FAILED,
};

_Static_assert(sizeof(pota_bcb_body_t) == POTA_BCB_PAGE_SIZE,
               "BCB body must occupy one program page");
_Static_assert(sizeof(pota_bcb_commit_t) == POTA_BCB_PAGE_SIZE,
               "BCB commit must occupy one program page");
_Static_assert(sizeof(pota_bcb_seal_t) == POTA_BCB_PAGE_SIZE,
               "BCB seal must occupy one program page");

static bool platform_read_valid(const pota_bcb_store_t *store)
{
    return store != NULL && store->platform.read_page != NULL;
}

static bool platform_write_valid(const pota_bcb_store_t *store)
{
    return platform_read_valid(store) &&
           store->platform.program_page != NULL &&
           (store->platform.erase_lane != NULL ||
            (store->platform.erase_lane_sector != NULL &&
             store->platform.erase_sector_count != 0u));
}

static bool is_blank(const uint8_t *page)
{
    for (uint32_t index = 0u; index < POTA_BCB_PAGE_SIZE; index++) {
        if (page[index] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static uint32_t next_generation(uint32_t generation)
{
    generation++;
    return generation == 0u ? 1u : generation;
}

static bool sequence_newer(uint32_t candidate, uint32_t current)
{
    return candidate != current &&
           (int32_t)(candidate - current) > 0;
}

static bool read_page(const pota_bcb_store_t *store, uint32_t lane,
                      uint32_t page, uint8_t *data)
{
    return store->platform.read_page(store->platform.context, lane, page,
                                      data, POTA_BCB_PAGE_SIZE);
}

static void bcb_service(const pota_bcb_store_t *store);

static bool program_page_verified(const pota_bcb_store_t *store,
                                  uint32_t lane, uint32_t page,
                                  const uint8_t *data)
{
    uint8_t readback[POTA_BCB_PAGE_SIZE];
    ((pota_bcb_store_t *)store)->program_page_count++;
    if (store->platform.on_program_page != NULL) {
        store->platform.on_program_page(store->platform.context, lane, page);
    }
    if (!store->platform.program_page(store->platform.context, lane, page,
                                       data, POTA_BCB_PAGE_SIZE) ||
        !read_page(store, lane, page, readback)) {
        return false;
    }
    return memcmp(data, readback, sizeof(readback)) == 0;
}

static bool erase_lane(const pota_bcb_store_t *store, uint32_t lane)
{
    ((pota_bcb_store_t *)store)->erase_lane_count++;
    if (store->platform.on_erase_lane != NULL) {
        store->platform.on_erase_lane(store->platform.context, lane);
    }
    if (store->platform.erase_lane != NULL) {
        return store->platform.erase_lane(store->platform.context, lane);
    }
    if (store->platform.erase_lane_sector == NULL ||
        store->platform.erase_sector_count == 0u) {
        return false;
    }
    for (uint32_t sector = 0u;
         sector < store->platform.erase_sector_count; sector++) {
        if (!store->platform.erase_lane_sector(store->platform.context,
                                                lane, sector)) {
            return false;
        }
        bcb_service(store);
    }
    return true;
}

static uint32_t body_crc32(const pota_bcb_body_t *body)
{
    pota_bcb_body_t copy;
    if (body == NULL) {
        return 0u;
    }
    copy = *body;
    copy.body_crc32 = 0u;
    return pota_crc32_compute(&copy, sizeof(copy));
}

static uint32_t seal_crc32(const pota_bcb_seal_t *seal)
{
    pota_bcb_seal_t copy;
    if (seal == NULL) {
        return 0u;
    }
    copy = *seal;
    copy.seal_crc32 = 0u;
    return pota_crc32_compute(&copy, sizeof(copy));
}

static bool body_valid(const pota_bcb_store_t *store,
                       const pota_bcb_body_t *body)
{
    if (body == NULL || body->magic != POTA_BCB_BODY_MAGIC ||
        body->schema_version != store->schema_version ||
        body->map_version != store->map_version || body->sequence == 0u ||
        body->payload_length > POTA_BCB_BODY_PAYLOAD_SIZE ||
        body->payload_crc32 != pota_crc32_compute(body->payload,
                                                   body->payload_length) ||
        body->body_crc32 != body_crc32(body)) {
        return false;
    }
    return true;
}

static bool seal_valid(const pota_bcb_store_t *store,
                       const pota_bcb_seal_t *seal)
{
    return seal != NULL && seal->magic == POTA_BCB_SEAL_MAGIC &&
           seal->schema_version == store->schema_version &&
           seal->map_version == store->map_version &&
           seal->lane_generation != 0u &&
           seal->seal_marker == (POTA_BCB_SEAL_MARKER ^
                                 seal->lane_generation) &&
           seal->seal_crc32 == seal_crc32(seal);
}

static bool commit_valid(const pota_bcb_store_t *store,
                         const pota_bcb_commit_t *commit,
                         uint32_t lane_generation,
                         const pota_bcb_body_t *body)
{
    return commit != NULL && body != NULL &&
           commit->magic == POTA_BCB_COMMIT_MAGIC &&
           commit->schema_version == store->schema_version &&
           commit->map_version == store->map_version &&
           commit->lane_generation == lane_generation &&
           commit->sequence == body->sequence &&
           commit->body_crc32 == body->body_crc32 &&
           commit->commit_marker == (POTA_BCB_COMMIT_MARKER ^
                                     body->sequence);
}

static bool read_seal(const pota_bcb_store_t *store, uint32_t lane,
                      pota_bcb_seal_t *seal)
{
    return read_page(store, lane, store->lane_page_count - 1u,
                     (uint8_t *)seal);
}

static bool read_record(const pota_bcb_store_t *store, uint32_t lane,
                        uint32_t record_page, uint32_t lane_generation,
                        pota_bcb_body_t *body)
{
    pota_bcb_commit_t commit;
    if (!read_page(store, lane, record_page, (uint8_t *)body) ||
        !read_page(store, lane, record_page + 1u, (uint8_t *)&commit)) {
        return false;
    }
    return body_valid(store, body) &&
           commit_valid(store, &commit, lane_generation, body);
}

static bool view_from_body(uint32_t lane, uint32_t record_page,
                           uint32_t lane_generation,
                           const pota_bcb_body_t *body,
                           pota_bcb_view_t *view)
{
    if (body == NULL || view == NULL) {
        return false;
    }
    view->lane = lane;
    view->record_page = record_page;
    view->lane_generation = lane_generation;
    view->update.sequence = body->sequence;
    view->update.boot_generation = body->boot_generation;
    view->update.security_counter = body->security_counter;
    view->update.payload_length = body->payload_length;
    memcpy(view->update.payload, body->payload,
           sizeof(view->update.payload));
    return true;
}

static uint32_t records_per_lane(const pota_bcb_store_t *store)
{
    return (store->lane_page_count - 1u) / 2u;
}

static void bcb_service(const pota_bcb_store_t *store)
{
    if (store != NULL && store->platform.service != NULL) {
        store->platform.service(store->platform.context);
    }
}

pota_bcb_result_t pota_bcb_store_init(pota_bcb_store_t *store,
                                       const pota_bcb_platform_t *platform,
                                       uint32_t schema_version,
                                       uint32_t map_version,
                                       uint32_t lane_page_count)
{
    if (platform == NULL || platform->program_page == NULL ||
        (platform->erase_lane == NULL &&
         (platform->erase_lane_sector == NULL ||
          platform->erase_sector_count == 0u))) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    return pota_bcb_store_init_read_only(store, platform, schema_version,
                                         map_version, lane_page_count);
}

pota_bcb_result_t pota_bcb_store_init_read_only(
    pota_bcb_store_t *store,
    const pota_bcb_platform_t *platform,
    uint32_t schema_version,
    uint32_t map_version,
    uint32_t lane_page_count)
{
    if (store == NULL || platform == NULL || platform->read_page == NULL ||
        schema_version == 0u || map_version == 0u || lane_page_count < 3u ||
        records_per_lane(&(pota_bcb_store_t){.lane_page_count = lane_page_count}) == 0u) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    (void)memset(store, 0, sizeof(*store));
    store->platform = *platform;
    store->schema_version = schema_version;
    store->map_version = map_version;
    store->lane_page_count = lane_page_count;
    return POTA_BCB_RESULT_OK;
}

pota_bcb_result_t pota_bcb_store_select_newest(const pota_bcb_store_t *store,
                                                pota_bcb_view_t *view)
{
    if (!platform_read_valid(store) || view == NULL ||
        store->lane_page_count < 3u) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }

    bool found = false;
    pota_bcb_view_t newest;
    for (uint32_t lane = 0u; lane < POTA_BCB_LANE_COUNT; lane++) {
        pota_bcb_seal_t seal;
        if (!read_seal(store, lane, &seal) || !seal_valid(store, &seal)) {
            continue;
        }
        for (uint32_t slot = 0u; slot < records_per_lane(store); slot++) {
            pota_bcb_body_t body;
            const uint32_t page = slot * 2u;
            if (!read_record(store, lane, page, seal.lane_generation, &body)) {
                continue;
            }
            pota_bcb_view_t candidate;
            (void)view_from_body(lane, page, seal.lane_generation, &body,
                                 &candidate);
            if (!found || sequence_newer(candidate.update.sequence,
                                         newest.update.sequence)) {
                newest = candidate;
                found = true;
            }
            bcb_service(store);
        }
    }
    if (!found) {
        return POTA_BCB_RESULT_NO_VALID;
    }
    *view = newest;
    return POTA_BCB_RESULT_OK;
}

static bool page_is_blank(const pota_bcb_store_t *store, uint32_t lane,
                          uint32_t page)
{
    uint8_t data[POTA_BCB_PAGE_SIZE];
    return read_page(store, lane, page, data) && is_blank(data);
}

static bool find_free_slot(const pota_bcb_store_t *store, uint32_t lane,
                           uint32_t lane_generation, uint32_t *slot)
{
    for (uint32_t index = 0u; index < records_per_lane(store); index++) {
        const uint32_t page = index * 2u;
        if (page_is_blank(store, lane, page) &&
            page_is_blank(store, lane, page + 1u)) {
            *slot = index;
            return true;
        }
        pota_bcb_body_t body;
        if (read_record(store, lane, page, lane_generation, &body)) {
            bcb_service(store);
            continue;
        }
        bcb_service(store);
    }
    return false;
}

static bool write_seal(const pota_bcb_store_t *store, uint32_t lane,
                       uint32_t lane_generation)
{
    pota_bcb_seal_t seal;
    (void)memset(&seal, 0xFF, sizeof(seal));
    seal.magic = POTA_BCB_SEAL_MAGIC;
    seal.schema_version = store->schema_version;
    seal.map_version = store->map_version;
    seal.lane_generation = lane_generation;
    seal.seal_marker = POTA_BCB_SEAL_MARKER ^ lane_generation;
    seal.seal_crc32 = seal_crc32(&seal);
    return program_page_verified(store, lane, store->lane_page_count - 1u,
                                 (const uint8_t *)&seal);
}

static bool write_record(const pota_bcb_store_t *store, uint32_t lane,
                         uint32_t slot, uint32_t lane_generation,
                         const pota_bcb_update_t *update)
{
    pota_bcb_body_t body;
    pota_bcb_commit_t commit;
    (void)memset(&body, 0xFF, sizeof(body));
    body.magic = POTA_BCB_BODY_MAGIC;
    body.schema_version = store->schema_version;
    body.map_version = store->map_version;
    body.sequence = update->sequence;
    body.boot_generation = update->boot_generation;
    body.security_counter = update->security_counter;
    body.payload_length = update->payload_length;
    memcpy(body.payload, update->payload, sizeof(body.payload));
    body.payload_crc32 = pota_crc32_compute(body.payload, body.payload_length);
    body.body_crc32 = body_crc32(&body);

    (void)memset(&commit, 0xFF, sizeof(commit));
    commit.magic = POTA_BCB_COMMIT_MAGIC;
    commit.schema_version = store->schema_version;
    commit.map_version = store->map_version;
    commit.lane_generation = lane_generation;
    commit.sequence = body.sequence;
    commit.body_crc32 = body.body_crc32;
    commit.commit_marker = POTA_BCB_COMMIT_MARKER ^ body.sequence;

    const uint32_t page = slot * 2u;
    if (!program_page_verified(store, lane, page, (const uint8_t *)&body)) {
        return false;
    }
    return program_page_verified(store, lane, page + 1u,
                                 (const uint8_t *)&commit);
}

static uint32_t newest_lane_generation(const pota_bcb_store_t *store)
{
    uint32_t generation = 0u;
    for (uint32_t lane = 0u; lane < POTA_BCB_LANE_COUNT; lane++) {
        pota_bcb_seal_t seal;
        if (read_seal(store, lane, &seal) && seal_valid(store, &seal) &&
            (generation == 0u || seal.lane_generation > generation)) {
            generation = seal.lane_generation;
        }
    }
    return generation;
}

pota_bcb_result_t pota_bcb_store_append(pota_bcb_store_t *store,
                                         const pota_bcb_update_t *update,
                                         pota_bcb_view_t *view)
{
    if (!platform_write_valid(store) || update == NULL || view == NULL ||
        update->sequence == 0u ||
        update->payload_length > POTA_BCB_BODY_PAYLOAD_SIZE) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }

    pota_bcb_view_t newest;
    const pota_bcb_result_t selected =
        pota_bcb_store_select_newest(store, &newest);
    uint32_t lane = 0u;
    uint32_t lane_generation = 1u;
    uint32_t slot = 0u;
    bool new_lane = true;

    if (selected == POTA_BCB_RESULT_OK) {
        if (update->security_counter < newest.update.security_counter) {
            return POTA_BCB_RESULT_POLICY;
        }
        if (!sequence_newer(update->sequence, newest.update.sequence)) {
            return POTA_BCB_RESULT_REPLAY;
        }
        lane = newest.lane;
        lane_generation = newest.lane_generation;
        if (find_free_slot(store, lane, lane_generation, &slot)) {
            new_lane = false;
        } else {
            lane = (lane + 1u) % POTA_BCB_LANE_COUNT;
            lane_generation = next_generation(newest_lane_generation(store));
            if (!erase_lane(store, lane)) {
                return POTA_BCB_RESULT_IO;
            }
        }
    } else if (selected != POTA_BCB_RESULT_NO_VALID) {
        return selected;
    } else {
        lane_generation = next_generation(newest_lane_generation(store));
        if (!erase_lane(store, lane)) {
            return POTA_BCB_RESULT_IO;
        }
    }

    if (!write_record(store, lane, slot, lane_generation, update)) {
        return POTA_BCB_RESULT_VERIFY;
    }
    if (new_lane && !write_seal(store, lane, lane_generation)) {
        return POTA_BCB_RESULT_VERIFY;
    }

    view->lane = lane;
    view->record_page = slot * 2u;
    view->lane_generation = lane_generation;
    view->update = *update;
    return POTA_BCB_RESULT_OK;
}

static pota_bcb_result_t bcb_txn_fail(pota_bcb_txn_t *txn,
                                      pota_bcb_result_t result)
{
    if (txn != NULL) {
        txn->state = BCB_TXN_STATE_FAILED;
        txn->active = true;
    }
    return result;
}

static bool bcb_txn_read_page(const pota_bcb_txn_t *txn, uint32_t lane,
                              uint32_t page, uint8_t *data)
{
    /* A transaction owns a snapshot of the platform callbacks.  Readback
     * must go through that callback/context pair; txn->platform.context is
     * intentionally not a pota_bcb_store_t and must never be reconstructed
     * by casting the transaction object. */
    if (txn == NULL || txn->platform.read_page == NULL || data == NULL) {
        return false;
    }
    return txn->platform.read_page(txn->platform.context, lane, page, data,
                                   POTA_BCB_PAGE_SIZE);
}

static void bcb_txn_service(const pota_bcb_txn_t *txn)
{
    if (txn != NULL && txn->platform.service != NULL) {
        txn->platform.service(txn->platform.context);
    }
}

static bool bcb_txn_program_page(pota_bcb_txn_t *txn, uint32_t page,
                                 const uint8_t *data)
{
    if (txn == NULL || txn->platform.program_page == NULL || data == NULL) {
        return false;
    }
    if (txn->platform.on_program_page != NULL) {
        txn->platform.on_program_page(txn->platform.context, txn->lane, page);
    }
    if (txn->program_page_count != NULL) {
        (*txn->program_page_count)++;
    }
    const bool ok = txn->platform.program_page(
        txn->platform.context, txn->lane, page, data, POTA_BCB_PAGE_SIZE);
    bcb_txn_service(txn);
    return ok;
}

pota_bcb_result_t pota_bcb_txn_begin(
    pota_bcb_txn_t *txn,
    const pota_bcb_store_t *store,
    const pota_bcb_update_t *update)
{
    if (txn == NULL || !platform_write_valid(store) || update == NULL ||
        update->sequence == 0u ||
        update->payload_length > POTA_BCB_BODY_PAYLOAD_SIZE) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    (void)memset(txn, 0, sizeof(*txn));
    txn->platform = store->platform;
    txn->schema_version = store->schema_version;
    txn->map_version = store->map_version;
    txn->lane_page_count = store->lane_page_count;
    txn->program_page_count = &((pota_bcb_store_t *)store)->program_page_count;
    txn->erase_lane_count = &((pota_bcb_store_t *)store)->erase_lane_count;
    txn->update = *update;

    pota_bcb_view_t newest;
    const pota_bcb_result_t selected =
        pota_bcb_store_select_newest(store, &newest);
    txn->lane = 0u;
    txn->lane_generation = next_generation(newest_lane_generation(store));
    txn->slot = 0u;
    txn->new_lane = true;
    if (selected == POTA_BCB_RESULT_OK) {
        if (update->security_counter < newest.update.security_counter) {
            return bcb_txn_fail(txn, POTA_BCB_RESULT_POLICY);
        }
        if (!sequence_newer(update->sequence, newest.update.sequence)) {
            return bcb_txn_fail(txn, POTA_BCB_RESULT_REPLAY);
        }
        txn->lane = newest.lane;
        txn->lane_generation = newest.lane_generation;
        if (find_free_slot(store, txn->lane, txn->lane_generation,
                           &txn->slot)) {
            txn->new_lane = false;
        } else {
            txn->lane = (txn->lane + 1u) % POTA_BCB_LANE_COUNT;
            txn->lane_generation = next_generation(
                newest_lane_generation(store));
        }
    } else if (selected != POTA_BCB_RESULT_NO_VALID) {
        return bcb_txn_fail(txn, selected);
    }

    pota_bcb_body_t body;
    (void)memset(&body, 0xFF, sizeof(body));
    body.magic = POTA_BCB_BODY_MAGIC;
    body.schema_version = store->schema_version;
    body.map_version = store->map_version;
    body.sequence = update->sequence;
    body.boot_generation = update->boot_generation;
    body.security_counter = update->security_counter;
    body.payload_length = update->payload_length;
    (void)memcpy(body.payload, update->payload, sizeof(body.payload));
    body.payload_crc32 = pota_crc32_compute(body.payload, body.payload_length);
    body.body_crc32 = body_crc32(&body);
    (void)memcpy(txn->body, &body, sizeof(body));

    pota_bcb_commit_t commit;
    (void)memset(&commit, 0xFF, sizeof(commit));
    commit.magic = POTA_BCB_COMMIT_MAGIC;
    commit.schema_version = store->schema_version;
    commit.map_version = store->map_version;
    commit.lane_generation = txn->lane_generation;
    commit.sequence = body.sequence;
    commit.body_crc32 = body.body_crc32;
    commit.commit_marker = POTA_BCB_COMMIT_MARKER ^ body.sequence;
    (void)memcpy(txn->commit, &commit, sizeof(commit));

    pota_bcb_seal_t seal;
    (void)memset(&seal, 0xFF, sizeof(seal));
    seal.magic = POTA_BCB_SEAL_MAGIC;
    seal.schema_version = store->schema_version;
    seal.map_version = store->map_version;
    seal.lane_generation = txn->lane_generation;
    seal.seal_marker = POTA_BCB_SEAL_MARKER ^ txn->lane_generation;
    seal.seal_crc32 = seal_crc32(&seal);
    (void)memcpy(txn->seal, &seal, sizeof(seal));

    txn->state = txn->new_lane ? BCB_TXN_STATE_ERASE
                               : BCB_TXN_STATE_PROGRAM_BODY;
    txn->active = true;
    return POTA_BCB_RESULT_OK;
}

pota_bcb_step_result_t pota_bcb_txn_step(pota_bcb_txn_t *txn)
{
    if (txn == NULL || !txn->active) {
        return POTA_BCB_STEP_FAILED;
    }
    if (txn->state == BCB_TXN_STATE_DONE) {
        return POTA_BCB_STEP_DONE;
    }
    if (txn->state == BCB_TXN_STATE_FAILED) {
        return POTA_BCB_STEP_FAILED;
    }

    switch (txn->state) {
    case BCB_TXN_STATE_ERASE:
        if (txn->platform.erase_lane_sector != NULL &&
            txn->platform.erase_sector_count != 0u) {
            if (txn->erase_sector < txn->platform.erase_sector_count) {
                if (txn->erase_sector == 0u &&
                    txn->platform.on_erase_lane != NULL) {
                    txn->platform.on_erase_lane(txn->platform.context,
                                                txn->lane);
                }
                if (txn->erase_sector == 0u &&
                    txn->erase_lane_count != NULL) {
                    (*txn->erase_lane_count)++;
                }
                if (!txn->platform.erase_lane_sector(
                        txn->platform.context, txn->lane,
                        txn->erase_sector++)) {
                    txn->state = BCB_TXN_STATE_FAILED;
                    return POTA_BCB_STEP_FAILED;
                }
                bcb_txn_service(txn);
                return POTA_BCB_STEP_PENDING;
            }
        } else if (txn->erase_sector == 0u) {
            if (txn->platform.on_erase_lane != NULL) {
                txn->platform.on_erase_lane(txn->platform.context, txn->lane);
            }
            if (txn->erase_lane_count != NULL) {
                (*txn->erase_lane_count)++;
            }
            if (txn->platform.erase_lane == NULL ||
                !txn->platform.erase_lane(txn->platform.context, txn->lane)) {
                txn->state = BCB_TXN_STATE_FAILED;
                return POTA_BCB_STEP_FAILED;
            }
            txn->erase_sector = 1u;
            bcb_txn_service(txn);
            return POTA_BCB_STEP_PENDING;
        }
        txn->state = BCB_TXN_STATE_PROGRAM_BODY;
        return POTA_BCB_STEP_PENDING;

    case BCB_TXN_STATE_PROGRAM_BODY:
        if (!bcb_txn_program_page(txn, txn->slot * 2u, txn->body)) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        txn->state = BCB_TXN_STATE_VERIFY_BODY;
        return POTA_BCB_STEP_PENDING;

    case BCB_TXN_STATE_VERIFY_BODY:
        if (!bcb_txn_read_page(txn, txn->lane, txn->slot * 2u,
                               txn->readback) ||
            memcmp(txn->readback, txn->body, POTA_BCB_PAGE_SIZE) != 0) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        bcb_txn_service(txn);
        txn->state = BCB_TXN_STATE_PROGRAM_COMMIT;
        return POTA_BCB_STEP_PENDING;

    case BCB_TXN_STATE_PROGRAM_COMMIT:
        if (!bcb_txn_program_page(txn, txn->slot * 2u + 1u,
                                  txn->commit)) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        txn->state = BCB_TXN_STATE_VERIFY_COMMIT;
        return POTA_BCB_STEP_PENDING;

    case BCB_TXN_STATE_VERIFY_COMMIT:
        if (!bcb_txn_read_page(txn, txn->lane, txn->slot * 2u + 1u,
                               txn->readback) ||
            memcmp(txn->readback, txn->commit, POTA_BCB_PAGE_SIZE) != 0) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        bcb_txn_service(txn);
        txn->state = txn->new_lane ? BCB_TXN_STATE_PROGRAM_SEAL
                                   : BCB_TXN_STATE_DONE;
        return txn->state == BCB_TXN_STATE_DONE
                   ? POTA_BCB_STEP_DONE
                   : POTA_BCB_STEP_PENDING;

    case BCB_TXN_STATE_PROGRAM_SEAL:
        if (!bcb_txn_program_page(txn, txn->lane_page_count - 1u,
                                  txn->seal)) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        txn->state = BCB_TXN_STATE_VERIFY_SEAL;
        return POTA_BCB_STEP_PENDING;

    case BCB_TXN_STATE_VERIFY_SEAL:
        if (!bcb_txn_read_page(txn, txn->lane,
                               txn->lane_page_count - 1u, txn->readback) ||
            memcmp(txn->readback, txn->seal, POTA_BCB_PAGE_SIZE) != 0) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        bcb_txn_service(txn);
        txn->state = BCB_TXN_STATE_DONE;
        return POTA_BCB_STEP_DONE;

    default:
        txn->state = BCB_TXN_STATE_FAILED;
        return POTA_BCB_STEP_FAILED;
    }
}

bool pota_bcb_store_get_wear_snapshot(const pota_bcb_store_t *store,
                                      pota_bcb_wear_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL || !platform_read_valid(store)) {
        return false;
    }
    snapshot->program_page_count = store->program_page_count;
    snapshot->erase_lane_count = store->erase_lane_count;
    return true;
}

bool pota_bcb_store_get_health_snapshot(
    const pota_bcb_store_t *store,
    pota_bcb_health_snapshot_t *snapshot)
{
    if (!platform_read_valid(store) || snapshot == NULL ||
        store->lane_page_count < 3u) {
        return false;
    }

    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->newest_lane = UINT32_MAX;
    snapshot->newest_record_page = UINT32_MAX;

    bool found = false;
    for (uint32_t lane = 0u; lane < POTA_BCB_LANE_COUNT; lane++) {
        pota_bcb_seal_t seal;
        if (!read_seal(store, lane, &seal) || !seal_valid(store, &seal)) {
            continue;
        }
        snapshot->valid_lane_count++;
        if (seal.lane_generation > snapshot->newest_lane_generation) {
            snapshot->newest_lane_generation = seal.lane_generation;
        }

        for (uint32_t slot = 0u; slot < records_per_lane(store); slot++) {
            const uint32_t page = slot * 2u;
            pota_bcb_body_t body;
            if (!read_record(store, lane, page, seal.lane_generation, &body)) {
                continue;
            }
            snapshot->valid_record_count++;
            if (!found || sequence_newer(body.sequence,
                                         snapshot->newest_sequence)) {
                snapshot->newest_sequence = body.sequence;
                snapshot->newest_security_counter = body.security_counter;
                snapshot->newest_lane = lane;
                snapshot->newest_record_page = page;
                found = true;
            }
        }
    }
    return true;
}
