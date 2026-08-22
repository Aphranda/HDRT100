#include "ota_metadata.h"

#include <stddef.h>
#include <string.h>

#include "drv_flash.h"
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
    return ota_metadata_flash_read(ota_metadata_bcb_page_offset(lane, page),
                                   data, length);
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

static bool ota_metadata_bcb_erase_lane(void *context, uint32_t lane)
{
    (void)context;
    if (lane >= POTA_BCB_LANE_COUNT) {
        return false;
    }
    return ota_metadata_flash_erase(ota_metadata_bcb_page_offset(lane, 0u),
                                   OTA_BCB_LANE_SIZE);
}

static bool ota_metadata_bcb_init(pota_boot_control_facade_t *store)
{
    const pota_bcb_platform_t platform = {
        .context = NULL,
        .read_page = ota_metadata_bcb_read_page,
        .program_page = ota_metadata_bcb_program_page,
        .erase_lane = ota_metadata_bcb_erase_lane,
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
    if (metadata == NULL || !ota_metadata_bcb_init(&store) ||
        pota_boot_control_facade_select_newest(&store, &view) !=
            POTA_BCB_RESULT_OK ||
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

    ota_metadata_t copies[OTA_METADATA_COPY_COUNT];
    bool valid[OTA_METADATA_COPY_COUNT] = {false, false};
    memset(copies, 0, sizeof(copies));

    for (uint32_t i = 0u; i < OTA_METADATA_COPY_COUNT; i++) {
        if (ota_metadata_flash_read(ota_metadata_copy_offset(i), &copies[i], sizeof(copies[i]))) {
            valid[i] = ota_metadata_is_valid(&copies[i]);
        }

        if (!valid[i]) {
            ota_metadata_v2_t legacy_copy;
            if (ota_metadata_flash_read(ota_metadata_copy_offset(i), &legacy_copy, sizeof(legacy_copy)) &&
                ota_metadata_v2_is_valid(&legacy_copy)) {
                ota_metadata_from_v2(&legacy_copy, &copies[i]);
                valid[i] = true;
            }
        }
    }

    (void)valid;
    const ota_metadata_t *selected =
        portable_ota_port_metadata_select_newest(copies, OTA_METADATA_COPY_COUNT);
    if (selected != NULL) {
        *metadata = *selected;
        ota_metadata_upgrade_if_needed(metadata);
        return true;
    }

    ota_metadata_set_default(metadata);
    return true;
#endif
}

bool ota_metadata_store(const ota_metadata_t *metadata)
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
    (void)memset(&update, 0, sizeof(update));
    update.sequence = stored_metadata.sequence;
    update.boot_generation = stored_metadata.boot_generation;
    update.security_counter = 0u;
    update.payload_length = sizeof(stored_metadata);
    memcpy(update.payload, &stored_metadata, sizeof(stored_metadata));
    return pota_boot_control_facade_append(&store, &update, &view) ==
           POTA_BCB_RESULT_OK;
}

bool ota_metadata_mark_pending(ota_slot_t slot, uint32_t image_size, uint32_t image_crc32)
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

    return ota_metadata_store(&metadata);
}

bool ota_metadata_confirm_active(void)
{
    ota_metadata_t metadata;
    if (!ota_metadata_load(&metadata)) {
        return false;
    }

    if (!portable_ota_port_metadata_confirm_active(&metadata)) {
        return false;
    }

    return ota_metadata_store(&metadata);
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

const char *ota_metadata_boot_result_to_string(uint32_t result)
{
    return portable_ota_port_boot_result_to_string(result);
}
