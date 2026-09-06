#include "ota_metadata.h"

#include <stddef.h>
#include <string.h>

#include "drv_flash.h"
#include "drv_watchdog.h"
#include "ota_crc32.h"
#include "ota_metadata_flash.h"
#include "pota_boot_control_facade.h"
#include "portable_ota_port.h"


#define OTA_METADATA_COPY_SIZE    DRV_FLASH_SECTOR_SIZE
#define OTA_METADATA_COPY_A_OFFSET OTA_METADATA_OFFSET
#define OTA_METADATA_COPY_B_OFFSET (OTA_METADATA_OFFSET + OTA_METADATA_COPY_SIZE)
#define OTA_METADATA_VERSION_V2   2u
#define OTA_BCB_LANE_SIZE (OTA_METADATA_SIZE / POTA_BCB_LANE_COUNT)
#define OTA_BCB_LANE_PAGE_COUNT (OTA_BCB_LANE_SIZE / POTA_BCB_PAGE_SIZE)
#define OTA_BCB_ERASE_SECTOR_COUNT (OTA_BCB_LANE_SIZE / DRV_FLASH_SECTOR_SIZE)

_Static_assert((OTA_METADATA_SIZE % POTA_BCB_LANE_COUNT) == 0u,
               "Boot Control size must split evenly across BCB lanes");
_Static_assert((OTA_BCB_LANE_SIZE % POTA_BCB_PAGE_SIZE) == 0u,
               "BCB lane must be page aligned");
_Static_assert(sizeof(ota_metadata_t) <= POTA_BCB_BODY_PAYLOAD_SIZE,
               "OTA metadata must fit one BCB body payload");

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

static bool ota_metadata_is_valid(const ota_metadata_t *metadata);
static void ota_metadata_upgrade_if_needed(ota_metadata_t *metadata);

#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2
static uint32_t ota_metadata_copy_offset(uint32_t copy_index)
{
    return (copy_index == 0u) ? OTA_METADATA_COPY_A_OFFSET : OTA_METADATA_COPY_B_OFFSET;
}
#endif

static uint32_t ota_metadata_bcb_page_offset(uint32_t lane, uint32_t page)
{
    return OTA_METADATA_OFFSET + lane * OTA_BCB_LANE_SIZE +
           page * POTA_BCB_PAGE_SIZE;
}


static bool ota_metadata_bcb_read_page(void *context, uint32_t lane,
                                       uint32_t page, uint8_t *data,
                                       uint32_t length)
{
    (void)context;
    if (lane >= POTA_BCB_LANE_COUNT || page >= OTA_BCB_LANE_PAGE_COUNT ||
        length != POTA_BCB_PAGE_SIZE || data == NULL) {
        return false;
    }
    const bool ok = ota_metadata_flash_read(
        ota_metadata_bcb_page_offset(lane, page), data, length);
    return ok;
}

static bool ota_metadata_bcb_program_page(void *context, uint32_t lane,
                                           uint32_t page, const uint8_t *data,
                                           uint32_t length)
{
    (void)context;
    if (lane >= POTA_BCB_LANE_COUNT || page >= OTA_BCB_LANE_PAGE_COUNT ||
        length != POTA_BCB_PAGE_SIZE || data == NULL) {
        return false;
    }
    return ota_metadata_flash_program(ota_metadata_bcb_page_offset(lane, page),
                                      data, length);
}

static pota_bcb_step_result_t ota_metadata_bcb_program_page_step(
    void *context, uint32_t lane, uint32_t page, const uint8_t *data,
    uint32_t length)
{
    (void)context;
    if (lane >= POTA_BCB_LANE_COUNT || page >= OTA_BCB_LANE_PAGE_COUNT ||
        length != POTA_BCB_PAGE_SIZE || data == NULL) {
        return POTA_BCB_STEP_FAILED;
    }
    return ota_metadata_flash_program_step(
        ota_metadata_bcb_page_offset(lane, page), data, length);
}

static bool ota_metadata_bcb_erase_lane(void *context, uint32_t lane)
{
    (void)context;
    if (lane >= POTA_BCB_LANE_COUNT) {
        return false;
    }
    return ota_metadata_flash_erase(ota_metadata_bcb_page_offset(lane, 0u),
                                   OTA_BCB_LANE_SIZE);
}

static bool ota_metadata_bcb_erase_sector(void *context, uint32_t lane,
                                          uint32_t sector_index)
{
    (void)context;
    if (lane >= POTA_BCB_LANE_COUNT ||
        sector_index >= OTA_BCB_ERASE_SECTOR_COUNT) {
        return false;
    }
    return ota_metadata_flash_erase(
        ota_metadata_bcb_page_offset(lane, 0u) +
            sector_index * DRV_FLASH_SECTOR_SIZE,
                                    DRV_FLASH_SECTOR_SIZE);
}

static pota_bcb_step_result_t ota_metadata_bcb_erase_sector_step(
    void *context, uint32_t lane, uint32_t sector_index)
{
    (void)context;
    if (lane >= POTA_BCB_LANE_COUNT ||
        sector_index >= OTA_BCB_ERASE_SECTOR_COUNT) {
        return POTA_BCB_STEP_FAILED;
    }
    return ota_metadata_flash_erase_step(
        ota_metadata_bcb_page_offset(
            lane, sector_index * (DRV_FLASH_SECTOR_SIZE / POTA_BCB_PAGE_SIZE)),
        DRV_FLASH_SECTOR_SIZE);
}

/* Process-lifetime telemetry used by the diagnostics projection.  It is
 * intentionally volatile runtime state; durable endurance accounting belongs
 * to a separately budgeted store and is not inferred from this counter. */
static uint32_t s_bcb_program_page_count;
static uint32_t s_bcb_erase_lane_count;

static void ota_metadata_bcb_on_program_page(void *context, uint32_t lane,
                                             uint32_t page)
{
    (void)context;
    (void)lane;
    (void)page;
    s_bcb_program_page_count++;
}

static void ota_metadata_bcb_on_erase_lane(void *context, uint32_t lane)
{
    (void)context;
    (void)lane;
    s_bcb_erase_lane_count++;
}

static void ota_metadata_bcb_service(void *context)
{
    (void)context;
    /* This hook is deliberately non-blocking.  It is called from inside the
     * BCB selector, including from the SCPI/OTA service call boundary; a
     * scheduler delay here can strand the caller between a flash read and the
     * selector's completion breadcrumb.  The selector is bounded (two lanes,
     * finite records), and the watchdog supervisor remains the sole owner of
     * hardware feeding. */
}

static bool ota_metadata_bcb_init(pota_boot_control_facade_t *store)
{
    const pota_bcb_platform_t platform = {
        .context = NULL,
        .read_page = ota_metadata_bcb_read_page,
        .program_page = ota_metadata_bcb_program_page,
        .program_page_step = ota_metadata_bcb_program_page_step,
        .erase_lane = ota_metadata_bcb_erase_lane,
        .erase_lane_sector = ota_metadata_bcb_erase_sector,
        .erase_lane_sector_step = ota_metadata_bcb_erase_sector_step,
        .erase_sector_count = OTA_BCB_ERASE_SECTOR_COUNT,
        .on_program_page = ota_metadata_bcb_on_program_page,
        .on_erase_lane = ota_metadata_bcb_on_erase_lane,
        .service = ota_metadata_bcb_service,
    };
    return pota_boot_control_facade_init(store, &platform,
                                         FLASH_DEPLOYMENT_MAP_SCHEMA_VERSION,
                                         FLASH_DEPLOYMENT_MAP_VERSION,
                                         OTA_BCB_LANE_PAGE_COUNT) ==
           POTA_BCB_RESULT_OK;
}

static bool ota_metadata_load_bcb(ota_metadata_t *metadata)
{
    pota_boot_control_facade_t store;
    pota_bcb_view_t view;
    if (metadata == NULL) {
        return false;
    }

    if (!ota_metadata_bcb_init(&store)) {
        return false;
    }

    const pota_bcb_result_t selected =
        pota_boot_control_facade_select_newest(&store, &view);
    if (selected != POTA_BCB_RESULT_OK ||
        view.update.payload_length != sizeof(*metadata)) {
        return false;
    }

    memcpy(metadata, view.update.payload, sizeof(*metadata));
    if (!ota_metadata_is_valid(metadata)) {
        return false;
    }
    ota_metadata_upgrade_if_needed(metadata);
    return true;
}

uint32_t ota_metadata_crc32(const ota_metadata_t *metadata)
{
    return portable_ota_port_metadata_crc32(metadata);
}

uint32_t ota_metadata_ext_crc32(const ota_metadata_t *metadata)
{
    return portable_ota_port_metadata_ext_crc32(metadata);
}

uint32_t ota_metadata_ab_crc32(const ota_metadata_t *metadata)
{
    return portable_ota_port_metadata_ab_crc32(metadata);
}

static void ota_metadata_update_crc(ota_metadata_t *metadata)
{
    portable_ota_port_metadata_update_crc(metadata);
}

static bool ota_metadata_is_valid(const ota_metadata_t *metadata)
{
    return portable_ota_port_metadata_is_valid(metadata);
}

#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2
static void ota_metadata_init_extension_defaults(ota_metadata_t *metadata)
{
    portable_ota_port_metadata_init_extension_defaults(metadata);
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
    portable_ota_port_metadata_set_default(metadata);
}
#endif

static void ota_metadata_upgrade_if_needed(ota_metadata_t *metadata)
{
    portable_ota_port_metadata_upgrade_if_needed(metadata);
}

#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2
static bool ota_metadata_load_legacy_copies(ota_metadata_t *metadata)
{
    ota_metadata_t copies[OTA_METADATA_COPY_COUNT];
    bool valid[OTA_METADATA_COPY_COUNT] = {false, false};
    memset(copies, 0, sizeof(copies));

    for (uint32_t i = 0u; i < OTA_METADATA_COPY_COUNT; i++) {
        if (ota_metadata_flash_read(ota_metadata_copy_offset(i), &copies[i],
                                    sizeof(copies[i]))) {
            valid[i] = ota_metadata_is_valid(&copies[i]);
        }

        if (!valid[i]) {
            ota_metadata_v2_t legacy_copy;
            if (ota_metadata_flash_read(ota_metadata_copy_offset(i),
                                        &legacy_copy,
                                        sizeof(legacy_copy)) &&
                ota_metadata_v2_is_valid(&legacy_copy)) {
                ota_metadata_from_v2(&legacy_copy, &copies[i]);
                valid[i] = true;
            }
        }
    }

    (void)valid;
    const ota_metadata_t *selected =
        portable_ota_port_metadata_select_newest(
            copies, OTA_METADATA_COPY_COUNT);
    if (selected != NULL) {
        *metadata = *selected;
        ota_metadata_upgrade_if_needed(metadata);
        return true;
    }

    ota_metadata_set_default(metadata);
    return true;
}
#endif

bool ota_metadata_load(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    if (ota_metadata_load_bcb(metadata)) {
        return true;
    }

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    return false;
#else
    return ota_metadata_load_legacy_copies(metadata);
#endif
}

static bool ota_metadata_store_with_security_counter(
    const ota_metadata_t *metadata, uint32_t security_counter,
    bool allow_counter_advance)
{
    if (metadata == NULL) {
        return false;
    }

    ota_metadata_t stored_metadata = *metadata;
    ota_metadata_update_crc(&stored_metadata);

    pota_boot_control_facade_t store;
    pota_bcb_update_t update;
    pota_bcb_view_t view;
    if (!ota_metadata_bcb_init(&store)) {
        return false;
    }
    const pota_bcb_result_t selected =
        pota_boot_control_facade_select_newest(&store, &view);
    if (selected == POTA_BCB_RESULT_OK) {
        const uint32_t current_counter = view.update.security_counter;
        if (allow_counter_advance) {
            if (security_counter < current_counter) {
                return false;
            }
        } else {
            security_counter = current_counter;
        }
    } else if (selected != POTA_BCB_RESULT_NO_VALID) {
        return false;
    }
    (void)memset(&update, 0, sizeof(update));
    update.sequence = stored_metadata.sequence;
    update.boot_generation = stored_metadata.boot_generation;
    update.security_counter = security_counter;
    update.payload_length = sizeof(stored_metadata);
    memcpy(update.payload, &stored_metadata, sizeof(stored_metadata));
    return pota_boot_control_facade_append(&store, &update, &view) ==
           POTA_BCB_RESULT_OK;
}

bool ota_metadata_store(const ota_metadata_t *metadata)
{
    return ota_metadata_store_with_security_counter(metadata, 0u, false);
}

bool ota_metadata_mark_pending(ota_slot_t slot, uint32_t image_size,
                               uint32_t image_crc32,
                               uint32_t security_counter)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_mark_pending(&metadata,
                                                 slot,
                                                 image_size,
                                                 image_crc32)) {
        return false;
    }

    return ota_metadata_store_with_security_counter(&metadata,
                                                     security_counter, true);
}

bool ota_metadata_confirm_active(void)
{
    return ota_metadata_confirm_active_snapshot(NULL);
}

bool ota_metadata_confirm_active_snapshot(ota_metadata_t *committed_metadata)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_confirm_active(&metadata)) {
        return false;
    }

    if (!ota_metadata_store(&metadata)) {
        return false;
    }
    if (committed_metadata != NULL) {
        *committed_metadata = metadata;
    }
    return true;
}

bool ota_metadata_set_boot_mode(ota_boot_mode_t mode)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_set_boot_mode(&metadata, mode)) {
        return false;
    }

    return ota_metadata_store(&metadata);
}

bool ota_metadata_set_fault_injection(uint32_t flags)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_set_fault_injection(&metadata, flags)) {
        return false;
    }

    return ota_metadata_store(&metadata);
}

bool ota_metadata_begin_copy_transaction(ota_slot_t source,
                                         ota_slot_t destination,
                                         uint32_t image_size,
                                         uint32_t image_crc32)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_begin_copy_transaction(&metadata,
                                                           source,
                                                           destination,
                                                           image_size,
                                                           image_crc32)) {
        return false;
    }

    return ota_metadata_store(&metadata);
}

bool ota_metadata_update_copy_transaction(uint32_t state,
                                          uint32_t written,
                                          uint32_t last_error)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_update_copy_transaction(&metadata,
                                                            state,
                                                            written,
                                                            last_error)) {
        return false;
    }

    return ota_metadata_store(&metadata);
}

bool ota_metadata_finish_copy_transaction(void)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_finish_copy_transaction(&metadata)) {
        return false;
    }

    return ota_metadata_store(&metadata);
}

bool ota_metadata_fail_copy_transaction(uint32_t last_error)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_fail_copy_transaction(&metadata, last_error)) {
        return false;
    }

    return ota_metadata_store(&metadata);
}

bool ota_metadata_clear_copy_transaction(void)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_clear_copy_transaction(&metadata)) {
        return false;
    }

    return ota_metadata_store(&metadata);
}

bool ota_metadata_corrupt_copy(uint32_t copy_index)
{
    if (copy_index >= OTA_METADATA_COPY_COUNT) {
        return false;
    }

    return ota_metadata_bcb_erase_lane(NULL, copy_index);
}

static pota_boot_control_facade_t s_mark_pending_store;
static pota_bcb_scan_t s_mark_pending_scan;
static pota_bcb_selection_t s_mark_pending_selection;
static pota_bcb_txn_t s_mark_pending_txn;
static ota_metadata_t s_mark_pending_metadata;
static pota_bcb_update_t s_mark_pending_update;

enum {
    OTA_MARK_PENDING_STAGE_IDLE = 0u,
    OTA_MARK_PENDING_STAGE_SCAN,
    OTA_MARK_PENDING_STAGE_TRANSACTION,
};

static uint32_t s_mark_pending_stage;
static ota_slot_t s_mark_pending_slot;
static uint32_t s_mark_pending_image_size;
static uint32_t s_mark_pending_image_crc32;
static uint32_t s_mark_pending_security_counter;

static void ota_metadata_mark_pending_reset(void)
{
    s_mark_pending_stage = OTA_MARK_PENDING_STAGE_IDLE;
}

static bool ota_metadata_mark_pending_request_matches(
    ota_slot_t slot, uint32_t image_size, uint32_t image_crc32,
    uint32_t security_counter)
{
    return slot == s_mark_pending_slot &&
           image_size == s_mark_pending_image_size &&
           image_crc32 == s_mark_pending_image_crc32 &&
           security_counter == s_mark_pending_security_counter;
}

static bool ota_metadata_from_selection(
    const pota_bcb_selection_t *selection,
    ota_metadata_t *metadata)
{
    if (selection == NULL || metadata == NULL) {
        return false;
    }
    if (selection->result == POTA_BCB_RESULT_OK) {
        if (selection->newest.update.payload_length != sizeof(*metadata)) {
            return false;
        }
        (void)memcpy(metadata, selection->newest.update.payload,
                     sizeof(*metadata));
        if (!ota_metadata_is_valid(metadata)) {
            return false;
        }
        ota_metadata_upgrade_if_needed(metadata);
        return true;
    }
    if (selection->result != POTA_BCB_RESULT_NO_VALID) {
        return false;
    }
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    return false;
#else
    return ota_metadata_load_legacy_copies(metadata);
#endif
}

pota_platform_step_result_t ota_metadata_mark_pending_step(
    ota_slot_t slot, uint32_t image_size, uint32_t image_crc32,
    uint32_t security_counter, ota_metadata_t *committed_metadata)
{
    if (s_mark_pending_stage == OTA_MARK_PENDING_STAGE_IDLE) {
        drv_watchdog_mark_ota_phase(OTA_TRACE_PHASE_MARK_PENDING_LOAD);
        if (!ota_metadata_bcb_init(&s_mark_pending_store)) {
            return POTA_PLATFORM_STEP_FAILED;
        }
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_STORE_INIT_DONE);
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_INIT);
        if (pota_bcb_scan_begin(&s_mark_pending_scan,
                                &s_mark_pending_store.store) !=
            POTA_BCB_RESULT_OK) {
            ota_metadata_mark_pending_reset();
            return POTA_PLATFORM_STEP_FAILED;
        }
        s_mark_pending_slot = slot;
        s_mark_pending_image_size = image_size;
        s_mark_pending_image_crc32 = image_crc32;
        s_mark_pending_security_counter = security_counter;
        s_mark_pending_stage = OTA_MARK_PENDING_STAGE_SCAN;
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_SELECT);
        return POTA_PLATFORM_STEP_PENDING;
    }

    if (!ota_metadata_mark_pending_request_matches(
            slot, image_size, image_crc32, security_counter)) {
        ota_metadata_mark_pending_reset();
        return POTA_PLATFORM_STEP_FAILED;
    }

    if (s_mark_pending_stage == OTA_MARK_PENDING_STAGE_SCAN) {
        const pota_bcb_step_result_t scan_step =
            pota_bcb_scan_step(&s_mark_pending_scan);
        if (scan_step == POTA_BCB_STEP_PENDING) {
            return POTA_PLATFORM_STEP_PENDING;
        }
        if (scan_step != POTA_BCB_STEP_DONE) {
            ota_metadata_mark_pending_reset();
            return POTA_PLATFORM_STEP_FAILED;
        }
        const pota_bcb_result_t selected =
            pota_bcb_scan_result(&s_mark_pending_scan,
                                 &s_mark_pending_selection);
        if (selected != POTA_BCB_RESULT_OK &&
            selected != POTA_BCB_RESULT_NO_VALID) {
            ota_metadata_mark_pending_reset();
            return POTA_PLATFORM_STEP_FAILED;
        }
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_SELECT_DONE);

        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_LOAD_BCB_COPY);
        if (!ota_metadata_from_selection(&s_mark_pending_selection,
                                         &s_mark_pending_metadata) ||
            !portable_ota_port_metadata_mark_pending(
                &s_mark_pending_metadata, slot, image_size, image_crc32)) {
            ota_metadata_mark_pending_reset();
            return POTA_PLATFORM_STEP_FAILED;
        }
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_MUTATE_DONE);

        ota_metadata_update_crc(&s_mark_pending_metadata);
        (void)memset(&s_mark_pending_update, 0, sizeof(s_mark_pending_update));
        s_mark_pending_update.sequence = s_mark_pending_metadata.sequence;
        s_mark_pending_update.boot_generation =
            s_mark_pending_metadata.boot_generation;
        s_mark_pending_update.security_counter = security_counter;
        s_mark_pending_update.payload_length = sizeof(s_mark_pending_metadata);
        (void)memcpy(s_mark_pending_update.payload, &s_mark_pending_metadata,
                     sizeof(s_mark_pending_metadata));
        const pota_bcb_result_t begin =
            pota_bcb_txn_begin_from_selection(
                &s_mark_pending_txn, &s_mark_pending_store.store,
                &s_mark_pending_update,
                &s_mark_pending_selection);
        if (begin != POTA_BCB_RESULT_OK) {
            ota_metadata_mark_pending_reset();
            return POTA_PLATFORM_STEP_FAILED;
        }
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_TXN_BEGIN);
        s_mark_pending_stage = OTA_MARK_PENDING_STAGE_TRANSACTION;
        return POTA_PLATFORM_STEP_PENDING;
    }

    if (s_mark_pending_stage != OTA_MARK_PENDING_STAGE_TRANSACTION) {
        ota_metadata_mark_pending_reset();
        return POTA_PLATFORM_STEP_FAILED;
    }
    drv_watchdog_mark_ota_phase(OTA_TRACE_PHASE_MARK_PENDING_TXN_STEP);
    const pota_bcb_step_result_t step =
        pota_bcb_txn_step(&s_mark_pending_txn);
    if (step == POTA_BCB_STEP_DONE) {
        if (committed_metadata != NULL) {
            *committed_metadata = s_mark_pending_metadata;
        }
        ota_metadata_mark_pending_reset();
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_TXN_DONE);
        return POTA_PLATFORM_STEP_DONE;
    }
    if (step == POTA_BCB_STEP_FAILED) {
        ota_metadata_mark_pending_reset();
        drv_watchdog_mark_ota_phase(
            OTA_TRACE_PHASE_MARK_PENDING_TXN_FAILED);
        return POTA_PLATFORM_STEP_FAILED;
    }
    return POTA_PLATFORM_STEP_PENDING;
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

bool ota_metadata_get_bcb_health(ota_metadata_bcb_health_t *health)
{
    pota_boot_control_facade_t store;
    pota_bcb_health_snapshot_t snapshot;
    if (health == NULL || !ota_metadata_bcb_init(&store) ||
        !pota_boot_control_facade_get_health_snapshot(&store, &snapshot)) {
        return false;
    }

    health->valid_lane_count = snapshot.valid_lane_count;
    health->valid_record_count = snapshot.valid_record_count;
    health->newest_lane_generation = snapshot.newest_lane_generation;
    health->newest_sequence = snapshot.newest_sequence;
    health->newest_security_counter = snapshot.newest_security_counter;
    health->newest_lane = snapshot.newest_lane;
    health->newest_record_page = snapshot.newest_record_page;
    return true;
}

bool ota_metadata_get_bcb_wear(ota_metadata_bcb_wear_t *wear)
{
    if (wear == NULL) {
        return false;
    }

    wear->program_page_count = s_bcb_program_page_count;
    wear->erase_lane_count = s_bcb_erase_lane_count;
    return true;
}

const char *ota_metadata_boot_result_to_string(uint32_t result)
{
    return portable_ota_port_boot_result_to_string(result);
}
