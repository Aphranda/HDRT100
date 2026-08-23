#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bootloader_config.h"
#include "drv_flash_write.h"
#include "boot_flash_service.h"
#include "hardware/structs/scb.h"
#include "hardware/sync.h"
#include "ota_crc32.h"
#include "ota_image.h"
#include "ota_metadata.h"
#include "ota_partition.h"
#include "pico/stdlib.h"
#include "portable_ota_port.h"
#include "portable_ota_crypto.h"
#include "pota_slot_manifest.h"
#include "project_config.h"

#define RP2350_SRAM_BASE 0x20000000u
#define RP2350_SRAM_END  0x20082000u
#define BOOTLOADER_COPY_PROGRESS_STORE_BYTES (64u * 1024u)

typedef void (*app_entry_t)(void);

static bool bootloader_store_result(ota_metadata_t *metadata,
                                    ota_boot_result_t result,
                                    ota_slot_t source_slot,
                                    bool clear_pending);
static bool bootloader_app_vector_is_valid(uint32_t vector_offset);
static bool bootloader_slot_is_valid(ota_slot_t slot);
static uint32_t bootloader_metadata_slot_size(const ota_metadata_t *metadata,
                                              ota_slot_t slot);
static uint32_t bootloader_metadata_slot_crc32(const ota_metadata_t *metadata,
                                              ota_slot_t slot);

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
static ota_boot_result_t bootloader_manifest_error_to_result(pota_error_t error)
{
    return error == POTA_ERR_SIGNATURE_INVALID
               ? OTA_BOOT_RESULT_SIGNATURE_INVALID
               : OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
}

typedef struct {
    uint32_t base;
} bootloader_manifest_flash_context_t;

static bool bootloader_manifest_read(void *context, uint32_t offset,
                                     void *data, uint32_t length)
{
    const bootloader_manifest_flash_context_t *flash = context;
    return flash != NULL && data != NULL &&
           drv_flash_read(flash->base + offset, data, length);
}

static bool bootloader_hash_read(void *context, uint32_t offset,
                                 void *data, uint32_t length)
{
    (void)context;
    return drv_flash_read(offset, data, length);
}

static bool bootloader_slot_manifest_is_valid(const ota_metadata_t *metadata,
                                              ota_slot_t slot,
                                              ota_boot_result_t *failure)
{
    if (failure != NULL) {
        *failure = OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    }
    if (metadata == NULL || !bootloader_slot_is_valid(slot)) {
        return false;
    }
    bootloader_manifest_flash_context_t flash = {
        .base = OTA_SLOT_MANIFEST_BASE_OFFSET(slot),
    };
    pota_slot_manifest_store_t store;
    const pota_slot_manifest_config_t config = {
        .context = &flash,
        .read = bootloader_manifest_read,
        .program = NULL,
        .erase = NULL,
        .base_offset = 0u,
        /* lane_size describes one manifest lane.  The two-lane footprint is
         * OTA_SLOT_MANIFEST_LANE_BYTES; passing that combined size makes the
         * read-only boot validator address the second lane past its record. */
        .lane_size = OTA_SLOT_MANIFEST_LANE_SIZE,
        .page_size = FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
        .erase_size = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
        .map_version = FLASH_DEPLOYMENT_MAP_VERSION,
        .slot = (pota_slot_t)slot,
    };
    /* The portable store validates the durable body and commit marker.  Boot
     * never provides mutating callbacks, so the same lane cannot be altered. */
    if (pota_slot_manifest_init(&store, &config) !=
        POTA_SLOT_MANIFEST_OK) {
        return false;
    }
    pota_slot_manifest_t durable;
    if (pota_slot_manifest_load(&store, &durable) != POTA_SLOT_MANIFEST_OK) {
        return false;
    }
    ota_metadata_bcb_health_t health;
    if (!ota_metadata_get_bcb_health(&health)) {
        return false;
    }
    const pota_package_constraints_t constraints = {
        .product_id = PROJECT_PRODUCT_ID,
        .hardware_id = PROJECT_HARDWARE_ID,
        .bootloader_version = POTA_PACK_VERSION(
            PROJECT_BOOTLOADER_VERSION_MAJOR,
            PROJECT_BOOTLOADER_VERSION_MINOR,
            PROJECT_BOOTLOADER_VERSION_PATCH),
        .minimum_security_counter = health.newest_security_counter,
        .require_signature = true,
        .require_image_hashes = true,
        .verify_signature = portable_ota_crypto_verify_manifest,
    };
    pota_package_manifest_t manifest;
    const pota_error_t parse_result = pota_package_parse_header(
        durable.header, sizeof(durable.header), &constraints, &manifest);
    if (parse_result != POTA_ERR_NONE) {
        if (failure != NULL) {
            *failure = bootloader_manifest_error_to_result(parse_result);
        }
        return false;
    }
    const pota_package_image_t *image =
        pota_package_find_image(&manifest, (pota_slot_t)slot);
    const uint32_t image_size = bootloader_metadata_slot_size(metadata, slot);
    const uint32_t image_crc32 = bootloader_metadata_slot_crc32(metadata, slot);
    if (image == NULL || image->size != image_size ||
        image->run_offset != ota_partition_slot_offset(slot) ||
        image->crc32 != image_crc32) {
        if (failure != NULL) {
            *failure = OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
        }
        return false;
    }
    uint8_t digest[POTA_SHA256_SIZE];
    if (!portable_ota_crypto_sha256_flash(
            bootloader_hash_read, NULL,
            ota_partition_slot_offset(slot), image_size, digest) ||
        memcmp(digest, image->sha256, sizeof(digest)) != 0) {
        if (failure != NULL) {
            *failure = OTA_BOOT_RESULT_IMAGE_HASH_INVALID;
        }
        return false;
    }
    return true;
}

#if defined(PROJECT_DEBUG_ALLOW_UNSIGNED_FACTORY) && \
    PROJECT_DEBUG_ALLOW_UNSIGNED_FACTORY
static bool bootloader_slot_manifest_region_is_erased(ota_slot_t slot)
{
    uint32_t magic = 0u;
    return bootloader_slot_is_valid(slot) &&
           drv_flash_read(OTA_SLOT_MANIFEST_BASE_OFFSET(slot), &magic,
                          sizeof(magic)) && magic == 0xFFFFFFFFu;
}
#endif
#endif

static bool bootloader_slot_is_valid(ota_slot_t slot)
{
    return slot == OTA_SLOT_A || slot == OTA_SLOT_B;
}

static uint32_t bootloader_metadata_slot_size(const ota_metadata_t *metadata, ota_slot_t slot)
{
    if (metadata == NULL) {
        return 0u;
    }

    return (slot == OTA_SLOT_A) ? metadata->slot_a_size :
           (slot == OTA_SLOT_B) ? metadata->slot_b_size :
                                  0u;
}

static uint32_t bootloader_metadata_slot_crc32(const ota_metadata_t *metadata, ota_slot_t slot)
{
    if (metadata == NULL) {
        return 0u;
    }

    return (slot == OTA_SLOT_A) ? metadata->slot_a_crc32 :
           (slot == OTA_SLOT_B) ? metadata->slot_b_crc32 :
                                  0u;
}

static bool bootloader_validate_slot_at_run_offset(ota_slot_t slot,
                                                   uint32_t image_size,
                                                   uint32_t image_crc32,
                                                   uint32_t run_flash_offset,
                                                   ota_boot_result_t *failure)
{
    const uint32_t slot_offset = ota_partition_slot_offset(slot);
    const uint32_t slot_size = ota_partition_slot_size(slot);

    if (slot_offset == 0u || image_size == 0u || image_size > slot_size) {
        if (failure != NULL) {
            *failure = image_size == 0u ? OTA_BOOT_RESULT_SLOT_EMPTY
                                        : OTA_BOOT_RESULT_SLOT_RANGE_INVALID;
        }
        return false;
    }

    if (!ota_image_validate_app_vector(slot_offset, image_size, run_flash_offset)) {
        if (failure != NULL) {
            *failure = OTA_BOOT_RESULT_VECTOR_INVALID;
        }
        return false;
    }

    const uint8_t *image = drv_flash_xip_ptr(slot_offset);
    if (image == NULL) {
        if (failure != NULL) {
            *failure = OTA_BOOT_RESULT_SLOT_RANGE_INVALID;
        }
        return false;
    }

    if (ota_crc32_compute(image, image_size) != image_crc32) {
        if (failure != NULL) {
            *failure = OTA_BOOT_RESULT_IMAGE_CRC_INVALID;
        }
        return false;
    }
    if (failure != NULL) {
        *failure = OTA_BOOT_RESULT_APPLIED;
    }
    return true;
}

#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2
static bool bootloader_validate_slot_as_slot_a(ota_slot_t slot,
                                               uint32_t image_size,
                                               uint32_t image_crc32,
                                               ota_boot_result_t *failure)
{
    return bootloader_validate_slot_at_run_offset(slot,
                                                  image_size,
                                                  image_crc32,
                                                  OTA_SLOT_A_OFFSET,
                                                  failure);
}
#endif

static bool bootloader_validate_slot_direct(const ota_metadata_t *metadata,
                                            ota_slot_t slot,
                                            ota_boot_result_t *failure)
{
    if (failure != NULL) {
        *failure = OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    }
    const uint32_t image_size = bootloader_metadata_slot_size(metadata, slot);
    const uint32_t image_crc32 = bootloader_metadata_slot_crc32(metadata, slot);
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    ota_boot_result_t manifest_failure = OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    if (!bootloader_slot_manifest_is_valid(metadata, slot, &manifest_failure)) {
#if defined(PROJECT_DEBUG_ALLOW_UNSIGNED_FACTORY) && \
    PROJECT_DEBUG_ALLOW_UNSIGNED_FACTORY
        /* Debug migration images may boot once from factory metadata before
         * the first signed OTA commit creates the durable slot manifest.
         * Never bypass a non-erased/corrupt manifest. */
        if (!bootloader_slot_manifest_region_is_erased(slot)) {
            if (failure != NULL) {
                *failure = manifest_failure;
            }
            return false;
        }
#else
        if (failure != NULL) {
            *failure = manifest_failure;
        }
        return false;
#endif
    }
#endif
    return bootloader_validate_slot_at_run_offset(slot,
                                                  image_size,
                                                  image_crc32,
                                                  ota_partition_slot_offset(slot),
                                                  failure);
}

static bool bootloader_direct_slot_is_bootable(const ota_metadata_t *metadata, ota_slot_t slot)
{
    if (!bootloader_slot_is_valid(slot)) {
        return false;
    }

    if (bootloader_metadata_slot_size(metadata, slot) != 0u) {
        return bootloader_validate_slot_direct(metadata, slot, NULL);
    }

    return bootloader_app_vector_is_valid(ota_partition_slot_offset(slot));
}

#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2
static bool bootloader_copy_transaction_matches(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    return metadata->copy_source_slot == (uint32_t)BOOTLOADER_STAGING_SLOT &&
           metadata->copy_destination_slot == (uint32_t)BOOTLOADER_ACTIVE_SLOT &&
           metadata->copy_size == metadata->slot_b_size &&
           metadata->copy_crc32 == metadata->slot_b_crc32;
}

static bool bootloader_store_copy_transaction(ota_metadata_t *metadata,
                                              ota_copy_txn_state_t state,
                                              uint32_t written,
                                              uint32_t last_error)
{
    if (metadata == NULL) {
        return false;
    }

    bool ok = false;
    if (state == OTA_COPY_TXN_STARTED) {
        ok = portable_ota_port_metadata_begin_copy_transaction(metadata,
                                                              BOOTLOADER_STAGING_SLOT,
                                                              BOOTLOADER_ACTIVE_SLOT,
                                                              metadata->slot_b_size,
                                                              metadata->slot_b_crc32);
    } else {
        ok = portable_ota_port_metadata_update_copy_transaction(metadata,
                                                               (uint32_t)state,
                                                               written,
                                                               last_error);
    }

    return ok ? ota_metadata_store(metadata) : false;
}

static bool bootloader_store_copy_failure(ota_metadata_t *metadata,
                                          ota_boot_result_t result,
                                          uint32_t written)
{
    (void)bootloader_store_copy_transaction(metadata,
                                            OTA_COPY_TXN_FAILED,
                                            written,
                                            (uint32_t)result);

    return bootloader_store_result(metadata,
                                   result,
                                   BOOTLOADER_STAGING_SLOT,
                                   false);
}

static bool bootloader_copy_slot(ota_metadata_t *metadata,
                                 ota_slot_t source,
                                 ota_slot_t destination,
                                 uint32_t image_size)
{
    const uint32_t src_offset = ota_partition_slot_offset(source);
    const uint32_t dst_offset = ota_partition_slot_offset(destination);
    const uint32_t dst_size = ota_partition_slot_size(destination);

    if (metadata == NULL ||
        src_offset == 0u ||
        dst_offset == 0u ||
        image_size == 0u ||
        image_size > dst_size) {
        return false;
    }

    const uint32_t erase_size = (image_size + DRV_FLASH_SECTOR_SIZE - 1u) &
                                ~(DRV_FLASH_SECTOR_SIZE - 1u);
    if (!boot_flash_service_erase(dst_offset, erase_size)) {
        return false;
    }

    if (!bootloader_store_copy_transaction(metadata,
                                           OTA_COPY_TXN_ERASED_ACTIVE,
                                           0u,
                                           (uint32_t)OTA_BOOT_RESULT_NONE)) {
        return false;
    }

    if (!bootloader_store_copy_transaction(metadata,
                                           OTA_COPY_TXN_PROGRAMMING,
                                           0u,
                                           (uint32_t)OTA_BOOT_RESULT_NONE)) {
        return false;
    }

    uint8_t page[BOOTLOADER_FLASH_COPY_BUFFER_SIZE];
    uint32_t copied = 0u;
    uint32_t next_progress_store = BOOTLOADER_COPY_PROGRESS_STORE_BYTES;
    while (copied < image_size) {
        const uint32_t remain = image_size - copied;
        const uint32_t chunk = remain > sizeof(page) ? (uint32_t)sizeof(page) : remain;
        const uint32_t program_size = (chunk + DRV_FLASH_PAGE_SIZE - 1u) &
                                      ~(DRV_FLASH_PAGE_SIZE - 1u);

        memset(page, 0xFF, sizeof(page));
        memcpy(page, drv_flash_xip_ptr(src_offset + copied), chunk);

        if (!boot_flash_service_program(dst_offset + copied, page, program_size)) {
            return false;
        }

        copied += chunk;

        if (copied >= next_progress_store || copied >= image_size) {
            if (!bootloader_store_copy_transaction(metadata,
                                                   OTA_COPY_TXN_PROGRAMMING,
                                                   copied,
                                                   (uint32_t)OTA_BOOT_RESULT_NONE)) {
                return false;
            }
            next_progress_store += BOOTLOADER_COPY_PROGRESS_STORE_BYTES;
        }
    }

    return true;
}

static bool bootloader_store_result(ota_metadata_t *metadata,
                                    ota_boot_result_t result,
                                    ota_slot_t source_slot,
                                    bool clear_pending)
{
    if (!portable_ota_port_metadata_record_boot_result(metadata,
                                                       result,
                                                       source_slot,
                                                       clear_pending)) {
        return false;
    }

    return ota_metadata_store(metadata);
}

static bool bootloader_apply_pending_image(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    if (metadata->pending_slot == (uint32_t)OTA_SLOT_NONE) {
        return false;
    }

    if (metadata->pending_slot != (uint32_t)BOOTLOADER_STAGING_SLOT) {
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_NO_PENDING,
                                      (ota_slot_t)metadata->pending_slot,
                                      true);
        return false;
    }

    if (metadata->copy_txn_state == (uint32_t)OTA_COPY_TXN_DONE &&
        bootloader_copy_transaction_matches(metadata) &&
        bootloader_validate_slot_as_slot_a(BOOTLOADER_ACTIVE_SLOT,
                                           metadata->slot_b_size,
                                           metadata->slot_b_crc32,
                                           NULL)) {
        if (!portable_ota_port_metadata_apply_copy_to_active_done(metadata,
                                                                  BOOTLOADER_STAGING_SLOT,
                                                                  BOOTLOADER_ACTIVE_SLOT)) {
            return false;
        }
        return ota_metadata_store(metadata);
    }

    if (metadata->slot_b_size == 0u ||
        metadata->boot_attempts >= BOOTLOADER_MAX_BOOT_ATTEMPTS) {
        (void)bootloader_store_copy_failure(metadata,
                                            OTA_BOOT_RESULT_MAX_ATTEMPTS,
                                            metadata->copy_written);
        return false;
    }

    if (!bootloader_store_copy_transaction(metadata,
                                           OTA_COPY_TXN_STARTED,
                                           0u,
                                           (uint32_t)OTA_BOOT_RESULT_NONE)) {
        return false;
    }

    if (portable_ota_port_metadata_increment_boot_attempts(metadata)) {
        (void)ota_metadata_store(metadata);
    }

    if (!bootloader_validate_slot_as_slot_a(BOOTLOADER_STAGING_SLOT,
                                            metadata->slot_b_size,
                                            metadata->slot_b_crc32,
                                            NULL)) {
        (void)bootloader_store_copy_failure(metadata,
                                            OTA_BOOT_RESULT_STAGE_VALIDATE_FAILED,
                                            0u);
        return false;
    }

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    if ((metadata->fault_injection_flags & OTA_FAULT_INJECT_COPY_FAIL) != 0u) {
        metadata->fault_injection_flags &= ~OTA_FAULT_INJECT_COPY_FAIL;
        (void)bootloader_store_copy_failure(metadata,
                                            OTA_BOOT_RESULT_COPY_FAILED,
                                            0u);
        return false;
    }
#endif

    if (!bootloader_copy_slot(metadata,
                              BOOTLOADER_STAGING_SLOT,
                              BOOTLOADER_ACTIVE_SLOT,
                              metadata->slot_b_size)) {
        (void)bootloader_store_copy_failure(metadata,
                                            OTA_BOOT_RESULT_COPY_FAILED,
                                            metadata->copy_written);
        return false;
    }

    if (!bootloader_store_copy_transaction(metadata,
                                           OTA_COPY_TXN_VERIFYING,
                                           metadata->slot_b_size,
                                           (uint32_t)OTA_BOOT_RESULT_NONE)) {
        return false;
    }

    if (!bootloader_validate_slot_as_slot_a(BOOTLOADER_ACTIVE_SLOT,
                                            metadata->slot_b_size,
                                            metadata->slot_b_crc32,
                                            NULL)) {
        (void)bootloader_store_copy_failure(metadata,
                                            OTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED,
                                            metadata->slot_b_size);
        return false;
    }

    if (!bootloader_store_copy_transaction(metadata,
                                           OTA_COPY_TXN_DONE,
                                           metadata->slot_b_size,
                                           (uint32_t)OTA_BOOT_RESULT_NONE)) {
        return false;
    }

    if (!portable_ota_port_metadata_apply_copy_to_active_done(metadata,
                                                              BOOTLOADER_STAGING_SLOT,
                                                              BOOTLOADER_ACTIVE_SLOT)) {
        return false;
    }
    return ota_metadata_store(metadata);
}
#endif

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
static bool bootloader_store_result(ota_metadata_t *metadata,
                                    ota_boot_result_t result,
                                    ota_slot_t source_slot,
                                    bool clear_pending)
{
    if (!portable_ota_port_metadata_record_boot_result(metadata,
                                                       result,
                                                       source_slot,
                                                       clear_pending)) {
        return false;
    }

    return ota_metadata_store(metadata);
}
#endif

static bool bootloader_apply_direct_ab_pending(ota_metadata_t *metadata)
{
    if (metadata == NULL ||
        metadata->pending_slot == (uint32_t)OTA_SLOT_NONE) {
        return false;
    }

    pota_direct_ab_decision_t decision;
    if (!portable_ota_port_metadata_direct_ab_decide(
            metadata, BOOTLOADER_MAX_BOOT_ATTEMPTS, &decision)) {
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_NO_PENDING,
                                      (ota_slot_t)metadata->pending_slot,
                                      true);
        return false;
    }

    if (decision.kind == POTA_DIRECT_AB_DECISION_NO_PENDING) {
        return false;
    }

    const ota_slot_t pending_slot = (ota_slot_t)decision.pending_slot;
    if (decision.kind == POTA_DIRECT_AB_DECISION_ROLLBACK) {
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_MAX_ATTEMPTS,
                                      pending_slot,
                                      true);
        return false;
    }

    ota_boot_result_t validation_failure = OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    if (!bootloader_validate_slot_direct(metadata, pending_slot,
                                         &validation_failure)) {
        (void)bootloader_store_result(metadata,
                                      validation_failure,
                                      pending_slot,
                                      true);
        return false;
    }

    if (!portable_ota_port_metadata_apply_direct_ab_pending(metadata, pending_slot)) {
        return false;
    }
    return ota_metadata_store(metadata);
}

static ota_slot_t bootloader_select_direct_rollback_slot(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return OTA_SLOT_NONE;
    }

    const ota_slot_t confirmed_slot = (ota_slot_t)metadata->confirmed_slot;
    if (bootloader_slot_is_valid(confirmed_slot) &&
        bootloader_direct_slot_is_bootable(metadata, confirmed_slot)) {
        return confirmed_slot;
    }

    const ota_slot_t previous_slot = (ota_slot_t)metadata->previous_slot;
    if (bootloader_slot_is_valid(previous_slot) &&
        bootloader_direct_slot_is_bootable(metadata, previous_slot)) {
        return previous_slot;
    }

    return OTA_SLOT_NONE;
}

static bool bootloader_direct_rollback(ota_metadata_t *metadata,
                                       ota_boot_result_t reason,
                                       ota_slot_t failed_slot)
{
    if (metadata == NULL) {
        return false;
    }

    const ota_slot_t rollback_slot = bootloader_select_direct_rollback_slot(metadata);
    if (rollback_slot == OTA_SLOT_NONE) {
        return false;
    }

    if (!portable_ota_port_metadata_rollback_direct_ab(metadata,
                                                       reason,
                                                       failed_slot,
                                                       rollback_slot)) {
        return false;
    }
    return ota_metadata_store(metadata);
}

static bool bootloader_prepare_direct_active_boot(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    const ota_slot_t active_slot = (ota_slot_t)metadata->active_slot;
    if (!bootloader_slot_is_valid(active_slot)) {
        return false;
    }

    if (metadata->active_slot == metadata->confirmed_slot) {
        return true;
    }

    if (metadata->boot_attempts >= BOOTLOADER_MAX_BOOT_ATTEMPTS) {
        return bootloader_direct_rollback(metadata,
                                          OTA_BOOT_RESULT_MAX_ATTEMPTS,
                                          active_slot);
    }

    ota_boot_result_t validation_failure = OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    if (!bootloader_validate_slot_direct(metadata, active_slot,
                                         &validation_failure)) {
        return bootloader_direct_rollback(metadata,
                                          validation_failure,
                                          active_slot);
    }

    if (!portable_ota_port_metadata_increment_boot_attempts(metadata)) {
        return false;
    }
    return ota_metadata_store(metadata);
}

static bool bootloader_app_vector_is_valid(uint32_t vector_offset)
{
    uint32_t vector[2];
    if (!drv_flash_read(vector_offset, vector, sizeof(vector))) {
        return false;
    }

    if (vector[0] < RP2350_SRAM_BASE || vector[0] > RP2350_SRAM_END) {
        return false;
    }

    return (vector[1] & 1u) != 0u;
}

#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2
static bool bootloader_active_app_is_valid(const ota_metadata_t *metadata)
{
    if (metadata != NULL &&
        metadata->active_slot == (uint32_t)BOOTLOADER_ACTIVE_SLOT &&
        metadata->slot_a_size != 0u) {
        return bootloader_validate_slot_as_slot_a(BOOTLOADER_ACTIVE_SLOT,
                                                  metadata->slot_a_size,
                                                  metadata->slot_a_crc32,
                                                  NULL);
    }

    return bootloader_app_vector_is_valid(BOOTLOADER_APP_VECTOR_OFFSET);
}
#endif

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
static bool bootloader_recovery_is_valid(void)
{
    return ota_image_validate_app_vector(
        FLASH_DEPLOYMENT_MAP_RECOVERY_OFFSET,
        FLASH_DEPLOYMENT_MAP_RECOVERY_SIZE,
        FLASH_DEPLOYMENT_MAP_RECOVERY_OFFSET);
}
#endif

static bool bootloader_direct_active_app_is_valid(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    return bootloader_direct_slot_is_bootable(metadata, (ota_slot_t)metadata->active_slot);
}

static void bootloader_jump_to_app(uint32_t vector_offset)
{
    const uint32_t vector_addr = DRV_FLASH_XIP_BASE + vector_offset;
    const uint32_t *vector = (const uint32_t *)(uintptr_t)vector_addr;
    const uint32_t initial_sp = vector[0];
    const uint32_t reset_handler = vector[1];

    __asm volatile("cpsid i");
    scb_hw->vtor = vector_addr;
    __asm volatile("msr msp, %0" : : "r"(initial_sp) : );
    __asm volatile("isb");

    ((app_entry_t)(uintptr_t)reset_handler)();
}

int main(void)
{
    ota_metadata_t metadata;
    bool metadata_loaded = false;
    bool direct_pending_applied = false;
    if (ota_metadata_load(&metadata)) {
        metadata_loaded = true;
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
        /* v2 has no fixed-address copy path.  A legacy mode record is not a
         * migration hint; it is invalid state and must fall through to the
         * recovery image instead of copying into the active slot. */
        if (metadata.boot_mode != (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
            metadata_loaded = false;
        } else {
            direct_pending_applied = bootloader_apply_direct_ab_pending(&metadata);
        }
#else
        if (metadata.boot_mode == (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
            direct_pending_applied = bootloader_apply_direct_ab_pending(&metadata);
        } else {
            (void)bootloader_apply_pending_image(&metadata);
        }
#endif
    }

    if (metadata_loaded &&
        metadata.boot_mode == (uint32_t)OTA_BOOT_MODE_DIRECT_AB &&
        (direct_pending_applied || bootloader_prepare_direct_active_boot(&metadata)) &&
        bootloader_direct_active_app_is_valid(&metadata)) {
        bootloader_jump_to_app(ota_partition_slot_offset((ota_slot_t)metadata.active_slot));
    }

#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2
    if (bootloader_active_app_is_valid(metadata_loaded ? &metadata : NULL)) {
        bootloader_jump_to_app(BOOTLOADER_APP_VECTOR_OFFSET);
    }
#endif

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    if (bootloader_recovery_is_valid()) {
        bootloader_jump_to_app(FLASH_DEPLOYMENT_MAP_RECOVERY_OFFSET);
    }
    /* A malformed/missing Recovery image is itself a durable fault.  Keep the
     * reason in BCB so the next factory/recovery probe can distinguish it from
     * an ordinary A/B validation failure.  Do not spin writes once the reason
     * is already recorded. */
    if (metadata_loaded &&
        metadata.last_boot_result !=
            (uint32_t)OTA_BOOT_RESULT_RECOVERY_UNAVAILABLE) {
        const ota_slot_t source =
            bootloader_slot_is_valid((ota_slot_t)metadata.active_slot)
                ? (ota_slot_t)metadata.active_slot
                : OTA_SLOT_NONE;
        (void)bootloader_store_result(&metadata,
                                      OTA_BOOT_RESULT_RECOVERY_UNAVAILABLE,
                                      source, false);
    }
#endif

    while (true) {
        tight_loop_contents();
    }
}
