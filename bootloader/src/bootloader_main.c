#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bootloader_config.h"
#include "drv_flash.h"
#include "hardware/structs/scb.h"
#include "hardware/sync.h"
#include "ota_crc32.h"
#include "ota_image.h"
#include "ota_metadata.h"
#include "ota_partition.h"
#include "pico/stdlib.h"

#define RP2350_SRAM_BASE 0x20000000u
#define RP2350_SRAM_END  0x20082000u

typedef void (*app_entry_t)(void);

static bool bootloader_validate_slot(ota_slot_t slot, uint32_t image_size, uint32_t image_crc32)
{
    const uint32_t slot_offset = ota_partition_slot_offset(slot);
    const uint32_t slot_size = ota_partition_slot_size(slot);

    if (slot_offset == 0u || image_size == 0u || image_size > slot_size) {
        return false;
    }

    if (!ota_image_validate_app_vector(slot_offset, image_size, OTA_SLOT_A_OFFSET)) {
        return false;
    }

    const uint8_t *image = drv_flash_xip_ptr(slot_offset);
    if (image == NULL) {
        return false;
    }

    return ota_crc32_compute(image, image_size) == image_crc32;
}

static bool bootloader_copy_slot(ota_slot_t source, ota_slot_t destination, uint32_t image_size)
{
    const uint32_t src_offset = ota_partition_slot_offset(source);
    const uint32_t dst_offset = ota_partition_slot_offset(destination);
    const uint32_t dst_size = ota_partition_slot_size(destination);

    if (src_offset == 0u || dst_offset == 0u || image_size == 0u || image_size > dst_size) {
        return false;
    }

    const uint32_t erase_size = (image_size + DRV_FLASH_SECTOR_SIZE - 1u) &
                                ~(DRV_FLASH_SECTOR_SIZE - 1u);
    if (!drv_flash_erase(dst_offset, erase_size)) {
        return false;
    }

    uint8_t page[BOOTLOADER_FLASH_COPY_BUFFER_SIZE];
    uint32_t copied = 0u;
    while (copied < image_size) {
        const uint32_t remain = image_size - copied;
        const uint32_t chunk = remain > sizeof(page) ? (uint32_t)sizeof(page) : remain;
        const uint32_t program_size = (chunk + DRV_FLASH_PAGE_SIZE - 1u) &
                                      ~(DRV_FLASH_PAGE_SIZE - 1u);

        memset(page, 0xFF, sizeof(page));
        memcpy(page, drv_flash_xip_ptr(src_offset + copied), chunk);

        if (!drv_flash_program(dst_offset + copied, page, program_size)) {
            return false;
        }

        copied += chunk;
    }

    return true;
}

static bool bootloader_store_result(ota_metadata_t *metadata,
                                    ota_boot_result_t result,
                                    ota_slot_t source_slot,
                                    bool clear_pending)
{
    metadata->last_boot_result = (uint32_t)result;
    metadata->last_boot_source_slot = (uint32_t)source_slot;
    metadata->last_boot_size = (source_slot == OTA_SLOT_B) ? metadata->slot_b_size : metadata->slot_a_size;
    metadata->last_boot_crc32 = (source_slot == OTA_SLOT_B) ? metadata->slot_b_crc32 : metadata->slot_a_crc32;

    if (clear_pending) {
        metadata->pending_slot = (uint32_t)OTA_SLOT_NONE;
        metadata->boot_attempts = 0u;
        if (result != OTA_BOOT_RESULT_APPLIED && result != OTA_BOOT_RESULT_NO_PENDING) {
            metadata->rollback_count++;
        }
    }

    metadata->sequence++;
    metadata->metadata_crc32 = ota_metadata_crc32(metadata);
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

    if (metadata->slot_b_size == 0u ||
        metadata->boot_attempts >= BOOTLOADER_MAX_BOOT_ATTEMPTS) {
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_MAX_ATTEMPTS,
                                      BOOTLOADER_STAGING_SLOT,
                                      true);
        return false;
    }

    metadata->boot_attempts++;
    metadata->sequence++;
    metadata->metadata_crc32 = ota_metadata_crc32(metadata);
    (void)ota_metadata_store(metadata);

    if (!bootloader_validate_slot(BOOTLOADER_STAGING_SLOT,
                                  metadata->slot_b_size,
                                  metadata->slot_b_crc32)) {
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_STAGE_VALIDATE_FAILED,
                                      BOOTLOADER_STAGING_SLOT,
                                      true);
        return false;
    }

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
    if ((metadata->fault_injection_flags & OTA_FAULT_INJECT_COPY_FAIL) != 0u) {
        metadata->fault_injection_flags &= ~OTA_FAULT_INJECT_COPY_FAIL;
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_COPY_FAILED,
                                      BOOTLOADER_STAGING_SLOT,
                                      true);
        return false;
    }
#endif

    if (!bootloader_copy_slot(BOOTLOADER_STAGING_SLOT,
                              BOOTLOADER_ACTIVE_SLOT,
                              metadata->slot_b_size)) {
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_COPY_FAILED,
                                      BOOTLOADER_STAGING_SLOT,
                                      true);
        return false;
    }

    if (!bootloader_validate_slot(BOOTLOADER_ACTIVE_SLOT,
                                  metadata->slot_b_size,
                                  metadata->slot_b_crc32)) {
        (void)bootloader_store_result(metadata,
                                      OTA_BOOT_RESULT_ACTIVE_VALIDATE_FAILED,
                                      BOOTLOADER_STAGING_SLOT,
                                      true);
        return false;
    }

    metadata->active_slot = (uint32_t)BOOTLOADER_ACTIVE_SLOT;
    metadata->confirmed_slot = (uint32_t)BOOTLOADER_ACTIVE_SLOT;
    metadata->pending_slot = (uint32_t)OTA_SLOT_NONE;
    metadata->boot_attempts = 0u;
    metadata->slot_a_size = metadata->slot_b_size;
    metadata->slot_a_crc32 = metadata->slot_b_crc32;
    return bootloader_store_result(metadata,
                                   OTA_BOOT_RESULT_APPLIED,
                                   BOOTLOADER_STAGING_SLOT,
                                   false);
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
    if (ota_metadata_load(&metadata)) {
        (void)bootloader_apply_pending_image(&metadata);
    }

    if (bootloader_app_vector_is_valid(BOOTLOADER_APP_VECTOR_OFFSET)) {
        bootloader_jump_to_app(BOOTLOADER_APP_VECTOR_OFFSET);
    }

    while (true) {
        tight_loop_contents();
    }
}
