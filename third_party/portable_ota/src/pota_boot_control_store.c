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

_Static_assert(sizeof(pota_bcb_body_t) == POTA_BCB_PAGE_SIZE,
               "BCB body must occupy one program page");
_Static_assert(sizeof(pota_bcb_commit_t) == POTA_BCB_PAGE_SIZE,
               "BCB commit must occupy one program page");
_Static_assert(sizeof(pota_bcb_seal_t) == POTA_BCB_PAGE_SIZE,
               "BCB seal must occupy one program page");

static bool platform_valid(const pota_bcb_store_t *store)
{
    return store != NULL && store->platform.read_page != NULL &&
           store->platform.program_page != NULL &&
           store->platform.erase_lane != NULL;
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

static bool program_page_verified(const pota_bcb_store_t *store,
                                  uint32_t lane, uint32_t page,
                                  const uint8_t *data)
{
    uint8_t readback[POTA_BCB_PAGE_SIZE];
    if (!store->platform.program_page(store->platform.context, lane, page,
                                       data, POTA_BCB_PAGE_SIZE) ||
        !read_page(store, lane, page, readback)) {
        return false;
    }
    return memcmp(data, readback, sizeof(readback)) == 0;
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

pota_bcb_result_t pota_bcb_store_init(pota_bcb_store_t *store,
                                       const pota_bcb_platform_t *platform,
                                       uint32_t schema_version,
                                       uint32_t map_version,
                                       uint32_t lane_page_count)
{
    if (store == NULL || platform == NULL || platform->read_page == NULL ||
        platform->program_page == NULL || platform->erase_lane == NULL ||
        schema_version == 0u || map_version == 0u || lane_page_count < 3u ||
        records_per_lane(&(pota_bcb_store_t){.lane_page_count = lane_page_count}) == 0u) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    store->platform = *platform;
    store->schema_version = schema_version;
    store->map_version = map_version;
    store->lane_page_count = lane_page_count;
    return POTA_BCB_RESULT_OK;
}

pota_bcb_result_t pota_bcb_store_select_newest(const pota_bcb_store_t *store,
                                                pota_bcb_view_t *view)
{
    if (!platform_valid(store) || view == NULL || store->lane_page_count < 3u) {
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
            continue;
        }
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
    if (!platform_valid(store) || update == NULL || view == NULL ||
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
            if (!store->platform.erase_lane(store->platform.context, lane)) {
                return POTA_BCB_RESULT_IO;
            }
        }
    } else if (selected != POTA_BCB_RESULT_NO_VALID) {
        return selected;
    } else {
        lane_generation = next_generation(newest_lane_generation(store));
        if (!store->platform.erase_lane(store->platform.context, lane)) {
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
