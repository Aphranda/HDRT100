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

enum {
    BCB_SCAN_STATE_READ_SEAL = 1u,
    BCB_SCAN_STATE_READ_BODY,
    BCB_SCAN_STATE_READ_COMMIT,
    BCB_SCAN_STATE_DONE,
    BCB_SCAN_STATE_FAILED,
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
           (store->platform.program_page != NULL ||
            store->platform.program_page_step != NULL) &&
           (store->platform.erase_lane != NULL ||
            (store->platform.erase_lane_sector != NULL &&
             store->platform.erase_sector_count != 0u) ||
            (store->platform.erase_lane_sector_step != NULL &&
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

static uint32_t crc32_with_zeroed_word(const void *object, size_t object_size,
                                       size_t word_offset)
{
    if (object == NULL || word_offset > object_size ||
        sizeof(uint32_t) > object_size - word_offset) {
        return 0u;
    }

    const uint8_t *bytes = object;
    const uint32_t zero = 0u;
    uint32_t crc = pota_crc32_update(0u, bytes, word_offset);
    crc = pota_crc32_update(crc, &zero, sizeof(zero));
    const size_t suffix_offset = word_offset + sizeof(zero);
    return pota_crc32_update(crc, bytes + suffix_offset,
                             object_size - suffix_offset);
}

static uint32_t body_crc32(const pota_bcb_body_t *body)
{
    return crc32_with_zeroed_word(body, sizeof(*body),
                                  offsetof(pota_bcb_body_t, body_crc32));
}

static uint32_t seal_crc32(const pota_bcb_seal_t *seal)
{
    return crc32_with_zeroed_word(seal, sizeof(*seal),
                                  offsetof(pota_bcb_seal_t, seal_crc32));
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
    const bool body_ok = body_valid(store, body);
    if (!body_ok) {
        return false;
    }
    return commit_valid(store, &commit, lane_generation, body);
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

static bool page_is_blank(const pota_bcb_store_t *store, uint32_t lane,
                          uint32_t page);

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
    if (platform == NULL ||
        (platform->program_page == NULL &&
         platform->program_page_step == NULL) ||
        (platform->erase_lane == NULL &&
         (platform->erase_lane_sector == NULL ||
          platform->erase_sector_count == 0u) &&
         (platform->erase_lane_sector_step == NULL ||
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
    store->mutation_generation = 1u;
    return POTA_BCB_RESULT_OK;
}

static pota_bcb_result_t finalize_selection(
    const pota_bcb_store_t *store,
    bool found,
    const pota_bcb_view_t *newest,
    uint32_t newest_lane_generation,
    const uint32_t free_slot[POTA_BCB_LANE_COUNT],
    pota_bcb_selection_t *selection)
{
    if (store == NULL || selection == NULL ||
        (found && newest == NULL)) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    selection->result = found ? POTA_BCB_RESULT_OK
                              : POTA_BCB_RESULT_NO_VALID;
    selection->store_generation = store->mutation_generation;
    selection->schema_version = store->schema_version;
    selection->map_version = store->map_version;
    selection->lane_page_count = store->lane_page_count;
    selection->append_lane = 0u;
    selection->append_slot = 0u;
    selection->append_lane_generation =
        next_generation(newest_lane_generation);
    selection->append_new_lane = true;
    if (found) {
        selection->newest = *newest;
        selection->append_lane = newest->lane;
        selection->append_lane_generation = newest->lane_generation;
        if (free_slot[newest->lane] != UINT32_MAX) {
            selection->append_slot = free_slot[newest->lane];
            selection->append_new_lane = false;
        } else {
            selection->append_lane =
                (newest->lane + 1u) % POTA_BCB_LANE_COUNT;
            selection->append_slot = 0u;
            selection->append_lane_generation =
                next_generation(newest_lane_generation);
            selection->append_new_lane = true;
        }
    }
    return selection->result;
}

static pota_bcb_result_t bcb_select(
    const pota_bcb_store_t *store,
    pota_bcb_view_t *view,
    pota_bcb_selection_t *selection)
{
    if (!platform_read_valid(store) || view == NULL ||
        store->lane_page_count < 3u) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }

    bool found = false;
    pota_bcb_view_t newest;
    uint32_t newest_lane_generation = 0u;
    uint32_t free_slot[POTA_BCB_LANE_COUNT] = {UINT32_MAX, UINT32_MAX};
    for (uint32_t lane = 0u; lane < POTA_BCB_LANE_COUNT; lane++) {
        pota_bcb_seal_t seal;
        const bool seal_read = read_seal(store, lane, &seal);
        bcb_service(store);
        if (!seal_read || !seal_valid(store, &seal)) {
            continue;
        }
        if (seal.lane_generation > newest_lane_generation) {
            newest_lane_generation = seal.lane_generation;
        }
        for (uint32_t slot = 0u; slot < records_per_lane(store); slot++) {
            pota_bcb_body_t body;
            const uint32_t page = slot * 2u;
            const bool body_blank = page_is_blank(store, lane, page);
            const bool commit_blank = page_is_blank(store, lane, page + 1u);
            bcb_service(store);
            if (body_blank && commit_blank) {
                free_slot[lane] = slot;
                break;
            }
            const bool record_valid =
                read_record(store, lane, page, seal.lane_generation, &body);
            bcb_service(store);
            if (!record_valid) {
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

    if (selection != NULL) {
        (void)finalize_selection(store, found, found ? &newest : NULL,
                                 newest_lane_generation, free_slot,
                                 selection);
    }

    if (!found) {
        return POTA_BCB_RESULT_NO_VALID;
    }
    *view = newest;
    return POTA_BCB_RESULT_OK;
}

pota_bcb_result_t pota_bcb_store_select_newest(const pota_bcb_store_t *store,
                                                pota_bcb_view_t *view)
{
    return bcb_select(store, view, NULL);
}

pota_bcb_result_t pota_bcb_store_select(
    const pota_bcb_store_t *store,
    pota_bcb_selection_t *selection)
{
    if (store == NULL || selection == NULL) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    (void)memset(selection, 0, sizeof(*selection));
    const pota_bcb_result_t result =
        bcb_select(store, &selection->newest, selection);
    selection->result = result;
    return result;
}

static pota_bcb_step_result_t bcb_scan_finish(pota_bcb_scan_t *scan)
{
    scan->terminal_result = finalize_selection(
        scan->store, scan->found,
        scan->found ? &scan->selection.newest : NULL,
        scan->newest_lane_generation, scan->free_slot,
        &scan->selection);
    scan->state = BCB_SCAN_STATE_DONE;
    scan->active = false;
    return POTA_BCB_STEP_DONE;
}

static pota_bcb_step_result_t bcb_scan_fail(
    pota_bcb_scan_t *scan,
    pota_bcb_result_t result)
{
    scan->terminal_result = result;
    scan->state = BCB_SCAN_STATE_FAILED;
    scan->active = false;
    return POTA_BCB_STEP_FAILED;
}

static pota_bcb_step_result_t bcb_scan_next_lane(pota_bcb_scan_t *scan)
{
    scan->lane++;
    scan->slot = 0u;
    if (scan->lane >= POTA_BCB_LANE_COUNT) {
        return bcb_scan_finish(scan);
    }
    scan->state = BCB_SCAN_STATE_READ_SEAL;
    return POTA_BCB_STEP_PENDING;
}

pota_bcb_result_t pota_bcb_scan_begin(
    pota_bcb_scan_t *scan,
    const pota_bcb_store_t *store)
{
    if (scan == NULL || !platform_read_valid(store) ||
        store->lane_page_count < 3u) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    (void)memset(scan, 0, sizeof(*scan));
    scan->store = store;
    scan->selection.store_generation = store->mutation_generation;
    for (uint32_t lane = 0u; lane < POTA_BCB_LANE_COUNT; lane++) {
        scan->free_slot[lane] = UINT32_MAX;
    }
    scan->terminal_result = POTA_BCB_RESULT_BUSY;
    scan->state = BCB_SCAN_STATE_READ_SEAL;
    scan->active = true;
    return POTA_BCB_RESULT_OK;
}

pota_bcb_step_result_t pota_bcb_scan_step(pota_bcb_scan_t *scan)
{
    if (scan == NULL || scan->store == NULL) {
        return POTA_BCB_STEP_FAILED;
    }
    if (scan->state == BCB_SCAN_STATE_DONE) {
        return POTA_BCB_STEP_DONE;
    }
    if (scan->state == BCB_SCAN_STATE_FAILED || !scan->active) {
        return POTA_BCB_STEP_FAILED;
    }
    if (scan->selection.store_generation !=
        scan->store->mutation_generation) {
        return bcb_scan_fail(scan, POTA_BCB_RESULT_BUSY);
    }

    switch (scan->state) {
    case BCB_SCAN_STATE_READ_SEAL: {
        const bool read_ok = read_page(
            scan->store, scan->lane, scan->store->lane_page_count - 1u,
            scan->seal);
        bcb_service(scan->store);
        const pota_bcb_seal_t *seal =
            (const pota_bcb_seal_t *)scan->seal;
        if (!read_ok || !seal_valid(scan->store, seal)) {
            return bcb_scan_next_lane(scan);
        }
        if (seal->lane_generation > scan->newest_lane_generation) {
            scan->newest_lane_generation = seal->lane_generation;
        }
        scan->slot = 0u;
        scan->state = BCB_SCAN_STATE_READ_BODY;
        return POTA_BCB_STEP_PENDING;
    }

    case BCB_SCAN_STATE_READ_BODY: {
        const uint32_t page = scan->slot * 2u;
        scan->body_read_ok = read_page(scan->store, scan->lane, page,
                                       scan->body);
        scan->body_blank = scan->body_read_ok && is_blank(scan->body);
        scan->state = BCB_SCAN_STATE_READ_COMMIT;
        return POTA_BCB_STEP_PENDING;
    }

    case BCB_SCAN_STATE_READ_COMMIT: {
        const uint32_t page = scan->slot * 2u;
        const bool commit_read_ok = read_page(
            scan->store, scan->lane, page + 1u, scan->commit);
        bcb_service(scan->store);
        const bool commit_blank =
            commit_read_ok && is_blank(scan->commit);
        if (scan->body_blank && commit_blank) {
            scan->free_slot[scan->lane] = scan->slot;
            return bcb_scan_next_lane(scan);
        }

        const pota_bcb_body_t *body =
            (const pota_bcb_body_t *)scan->body;
        const pota_bcb_commit_t *commit =
            (const pota_bcb_commit_t *)scan->commit;
        const pota_bcb_seal_t *seal =
            (const pota_bcb_seal_t *)scan->seal;
        const bool body_ok = scan->body_read_ok && body_valid(scan->store, body);
        const bool record_ok = body_ok && commit_read_ok &&
            commit_valid(scan->store, commit, seal->lane_generation, body);
        if (record_ok) {
            (void)view_from_body(scan->lane, page, seal->lane_generation,
                                 body, &scan->candidate);
            if (!scan->found ||
                sequence_newer(scan->candidate.update.sequence,
                               scan->selection.newest.update.sequence)) {
                scan->selection.newest = scan->candidate;
                scan->found = true;
            }
        }

        scan->slot++;
        if (scan->slot >= records_per_lane(scan->store)) {
            return bcb_scan_next_lane(scan);
        }
        scan->state = BCB_SCAN_STATE_READ_BODY;
        return POTA_BCB_STEP_PENDING;
    }

    default:
        return bcb_scan_fail(scan, POTA_BCB_RESULT_BAD_ARGUMENT);
    }
}

pota_bcb_result_t pota_bcb_scan_result(
    const pota_bcb_scan_t *scan,
    pota_bcb_selection_t *selection)
{
    if (scan == NULL || selection == NULL) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    if (scan->state != BCB_SCAN_STATE_DONE &&
        scan->state != BCB_SCAN_STATE_FAILED) {
        return POTA_BCB_RESULT_BUSY;
    }
    if (scan->state == BCB_SCAN_STATE_DONE) {
        *selection = scan->selection;
    }
    return scan->terminal_result;
}

static bool page_is_blank(const pota_bcb_store_t *store, uint32_t lane,
                          uint32_t page)
{
    uint8_t data[POTA_BCB_PAGE_SIZE];
    return read_page(store, lane, page, data) && is_blank(data);
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

pota_bcb_result_t pota_bcb_store_append(pota_bcb_store_t *store,
                                         const pota_bcb_update_t *update,
                                         pota_bcb_view_t *view)
{
    if (!platform_write_valid(store) || update == NULL || view == NULL ||
        update->sequence == 0u ||
        update->payload_length > POTA_BCB_BODY_PAYLOAD_SIZE) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }

    pota_bcb_selection_t selection;
    const pota_bcb_result_t selected =
        pota_bcb_store_select(store, &selection);
    uint32_t lane = selection.append_lane;
    uint32_t lane_generation = selection.append_lane_generation;
    uint32_t slot = selection.append_slot;
    const bool new_lane = selection.append_new_lane;

    if (selected == POTA_BCB_RESULT_OK) {
        if (update->security_counter <
            selection.newest.update.security_counter) {
            return POTA_BCB_RESULT_POLICY;
        }
        if (!sequence_newer(update->sequence,
                            selection.newest.update.sequence)) {
            return POTA_BCB_RESULT_REPLAY;
        }
    } else if (selected != POTA_BCB_RESULT_NO_VALID) {
        return selected;
    }

    store->mutation_generation = next_generation(store->mutation_generation);
    if (new_lane) {
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

static pota_bcb_result_t validate_selection(
    const pota_bcb_store_t *store,
    const pota_bcb_selection_t *selection)
{
    if (store == NULL || selection == NULL ||
        (selection->result != POTA_BCB_RESULT_OK &&
         selection->result != POTA_BCB_RESULT_NO_VALID) ||
        selection->schema_version != store->schema_version ||
        selection->map_version != store->map_version ||
        selection->lane_page_count != store->lane_page_count) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    return selection->store_generation == store->mutation_generation
               ? POTA_BCB_RESULT_OK
               : POTA_BCB_RESULT_BUSY;
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

static pota_bcb_step_result_t bcb_txn_program_page_step(
    pota_bcb_txn_t *txn, uint32_t page, const uint8_t *data)
{
    if (txn == NULL || data == NULL ||
        (txn->platform.program_page == NULL &&
         txn->platform.program_page_step == NULL)) {
        return POTA_BCB_STEP_FAILED;
    }
    if (!txn->io_active) {
        if (txn->platform.on_program_page != NULL) {
            txn->platform.on_program_page(txn->platform.context, txn->lane,
                                          page);
        }
        if (txn->program_page_count != NULL) {
            (*txn->program_page_count)++;
        }
    }
    pota_bcb_step_result_t result;
    if (txn->platform.program_page_step != NULL) {
        result = txn->platform.program_page_step(
            txn->platform.context, txn->lane, page, data,
            POTA_BCB_PAGE_SIZE);
    } else {
        result = txn->platform.program_page(
                     txn->platform.context, txn->lane, page, data,
                     POTA_BCB_PAGE_SIZE)
                     ? POTA_BCB_STEP_DONE
                     : POTA_BCB_STEP_FAILED;
    }
    txn->io_active = result == POTA_BCB_STEP_PENDING;
    bcb_txn_service(txn);
    return result;
}

pota_bcb_result_t pota_bcb_txn_begin(
    pota_bcb_txn_t *txn,
    pota_bcb_store_t *store,
    const pota_bcb_update_t *update)
{
    if (txn == NULL || !platform_write_valid(store) || update == NULL) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    pota_bcb_selection_t selection;
    const pota_bcb_result_t selected =
        pota_bcb_store_select(store, &selection);
    if (selected != POTA_BCB_RESULT_OK &&
        selected != POTA_BCB_RESULT_NO_VALID) {
        return selected;
    }
    return pota_bcb_txn_begin_from_selection(txn, store, update, &selection);
}

pota_bcb_result_t pota_bcb_txn_begin_from_selection(
    pota_bcb_txn_t *txn,
    pota_bcb_store_t *store,
    const pota_bcb_update_t *update,
    const pota_bcb_selection_t *selection)
{
    if (txn == NULL || !platform_write_valid(store) || update == NULL ||
        update->sequence == 0u ||
        update->payload_length > POTA_BCB_BODY_PAYLOAD_SIZE) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    const pota_bcb_result_t selection_valid =
        validate_selection(store, selection);
    if (selection_valid != POTA_BCB_RESULT_OK) {
        return selection_valid;
    }
    (void)memset(txn, 0, sizeof(*txn));
    txn->platform = store->platform;
    txn->schema_version = store->schema_version;
    txn->map_version = store->map_version;
    txn->lane_page_count = store->lane_page_count;
    txn->program_page_count = &store->program_page_count;
    txn->erase_lane_count = &store->erase_lane_count;
    txn->update = *update;

    txn->lane = selection->append_lane;
    txn->lane_generation = selection->append_lane_generation;
    txn->slot = selection->append_slot;
    txn->new_lane = selection->append_new_lane;
    if (selection->result == POTA_BCB_RESULT_OK) {
        if (update->security_counter <
            selection->newest.update.security_counter) {
            return bcb_txn_fail(txn, POTA_BCB_RESULT_POLICY);
        }
        if (!sequence_newer(update->sequence,
                            selection->newest.update.sequence)) {
            return bcb_txn_fail(txn, POTA_BCB_RESULT_REPLAY);
        }
    }

    store->mutation_generation = next_generation(store->mutation_generation);

    pota_bcb_body_t *body = (pota_bcb_body_t *)txn->body;
    (void)memset(body, 0xFF, sizeof(*body));
    body->magic = POTA_BCB_BODY_MAGIC;
    body->schema_version = store->schema_version;
    body->map_version = store->map_version;
    body->sequence = update->sequence;
    body->boot_generation = update->boot_generation;
    body->security_counter = update->security_counter;
    body->payload_length = update->payload_length;
    (void)memcpy(body->payload, update->payload, sizeof(body->payload));
    body->payload_crc32 = pota_crc32_compute(body->payload,
                                              body->payload_length);
    body->body_crc32 = body_crc32(body);

    pota_bcb_commit_t *commit = (pota_bcb_commit_t *)txn->commit;
    (void)memset(commit, 0xFF, sizeof(*commit));
    commit->magic = POTA_BCB_COMMIT_MAGIC;
    commit->schema_version = store->schema_version;
    commit->map_version = store->map_version;
    commit->lane_generation = txn->lane_generation;
    commit->sequence = body->sequence;
    commit->body_crc32 = body->body_crc32;
    commit->commit_marker = POTA_BCB_COMMIT_MARKER ^ body->sequence;

    pota_bcb_seal_t *seal = (pota_bcb_seal_t *)txn->seal;
    (void)memset(seal, 0xFF, sizeof(*seal));
    seal->magic = POTA_BCB_SEAL_MAGIC;
    seal->schema_version = store->schema_version;
    seal->map_version = store->map_version;
    seal->lane_generation = txn->lane_generation;
    seal->seal_marker = POTA_BCB_SEAL_MARKER ^ txn->lane_generation;
    seal->seal_crc32 = seal_crc32(seal);

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
        if (txn->platform.erase_lane_sector_step != NULL &&
            txn->platform.erase_sector_count != 0u) {
            if (txn->erase_sector < txn->platform.erase_sector_count) {
                if (!txn->io_active && txn->erase_sector == 0u &&
                    txn->platform.on_erase_lane != NULL) {
                    txn->platform.on_erase_lane(txn->platform.context,
                                                txn->lane);
                }
                if (!txn->io_active && txn->erase_sector == 0u &&
                    txn->erase_lane_count != NULL) {
                    (*txn->erase_lane_count)++;
                }
                const pota_bcb_step_result_t erase_step =
                    txn->platform.erase_lane_sector_step(
                        txn->platform.context, txn->lane,
                        txn->erase_sector);
                txn->io_active = erase_step == POTA_BCB_STEP_PENDING;
                bcb_txn_service(txn);
                if (erase_step == POTA_BCB_STEP_PENDING) {
                    return POTA_BCB_STEP_PENDING;
                }
                if (erase_step != POTA_BCB_STEP_DONE) {
                    txn->state = BCB_TXN_STATE_FAILED;
                    return POTA_BCB_STEP_FAILED;
                }
                txn->erase_sector++;
                return POTA_BCB_STEP_PENDING;
            }
        } else
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
        {
        const pota_bcb_step_result_t program_step =
            bcb_txn_program_page_step(txn, txn->slot * 2u, txn->body);
        if (program_step == POTA_BCB_STEP_PENDING) {
            return POTA_BCB_STEP_PENDING;
        }
        if (program_step != POTA_BCB_STEP_DONE) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        txn->state = BCB_TXN_STATE_VERIFY_BODY;
        return POTA_BCB_STEP_PENDING;
        }

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
        {
        const pota_bcb_step_result_t program_step =
            bcb_txn_program_page_step(txn, txn->slot * 2u + 1u,
                                      txn->commit);
        if (program_step == POTA_BCB_STEP_PENDING) {
            return POTA_BCB_STEP_PENDING;
        }
        if (program_step != POTA_BCB_STEP_DONE) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        txn->state = BCB_TXN_STATE_VERIFY_COMMIT;
        return POTA_BCB_STEP_PENDING;
        }

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
        {
        const pota_bcb_step_result_t program_step =
            bcb_txn_program_page_step(txn, txn->lane_page_count - 1u,
                                      txn->seal);
        if (program_step == POTA_BCB_STEP_PENDING) {
            return POTA_BCB_STEP_PENDING;
        }
        if (program_step != POTA_BCB_STEP_DONE) {
            txn->state = BCB_TXN_STATE_FAILED;
            return POTA_BCB_STEP_FAILED;
        }
        txn->state = BCB_TXN_STATE_VERIFY_SEAL;
        return POTA_BCB_STEP_PENDING;
        }

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
