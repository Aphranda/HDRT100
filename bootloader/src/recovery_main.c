#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flash_deployment_map.h"
#include "ota_metadata.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pota_image.h"
#include "pota_metadata.h"
#include "pota_package.h"
#include "pota_slot_manifest.h"
#include "pota_boot_control_store.h"
#include "portable_ota_crypto.h"
#include "project_config.h"
#include "project_build_info.h"

#define RECOVERY_COMMAND_CAPACITY 96u
#define RECOVERY_BCB_LANE_SIZE \
    (FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_SIZE / POTA_BCB_LANE_COUNT)
#define RECOVERY_BCB_LANE_PAGE_COUNT \
    (RECOVERY_BCB_LANE_SIZE / POTA_BCB_PAGE_SIZE)

_Static_assert(FLASH_DEPLOYMENT_MAP_HAS_RECOVERY == 1u,
               "Recovery target requires a deployment map with Recovery");
_Static_assert((FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_SIZE %
                POTA_BCB_LANE_COUNT) == 0u,
               "Boot Control must split evenly into BCB lanes");
_Static_assert(sizeof(ota_metadata_t) == sizeof(pota_metadata_t),
               "Recovery and portable BCB payload layouts must match");
_Static_assert(offsetof(ota_metadata_t, metadata_ab_crc32) ==
                   offsetof(pota_metadata_t, metadata_ab_crc32),
               "Recovery and portable BCB payload fields must remain aligned");

typedef struct {
    const char *name;
    uint32_t id;
    uint32_t offset;
    uint32_t size;
} recovery_partition_t;

#define RECOVERY_PARTITION(name, id, offset, size, alignment, boot, app, factory, executable) \
    {#name, id, offset, size},

static const recovery_partition_t s_recovery_partitions[] = {
    FLASH_DEPLOYMENT_MAP_PARTITION_TABLE(RECOVERY_PARTITION)
};

#undef RECOVERY_PARTITION

static bool recovery_bcb_read_page(void *context, uint32_t lane,
                                   uint32_t page, uint8_t *data,
                                   uint32_t length)
{
    (void)context;
    if (data == NULL || lane >= POTA_BCB_LANE_COUNT ||
        page >= RECOVERY_BCB_LANE_PAGE_COUNT ||
        length != POTA_BCB_PAGE_SIZE) {
        return false;
    }
    const uint32_t offset = FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_OFFSET +
                            lane * RECOVERY_BCB_LANE_SIZE +
                            page * POTA_BCB_PAGE_SIZE;
    if (offset >= FLASH_DEPLOYMENT_GEOMETRY_TOTAL_SIZE ||
        length > FLASH_DEPLOYMENT_GEOMETRY_TOTAL_SIZE - offset) {
        return false;
    }
    memcpy(data, (const void *)(uintptr_t)(
                     FLASH_DEPLOYMENT_GEOMETRY_XIP_BASE + offset),
           length);
    return true;
}

static bool recovery_flash_read(void *context, uint32_t offset,
                                void *data, uint32_t length)
{
    (void)context;
    if (data == NULL || offset >= FLASH_DEPLOYMENT_GEOMETRY_TOTAL_SIZE ||
        length > FLASH_DEPLOYMENT_GEOMETRY_TOTAL_SIZE - offset) {
        return false;
    }
    memcpy(data, (const void *)(uintptr_t)(
                     FLASH_DEPLOYMENT_GEOMETRY_XIP_BASE + offset), length);
    return true;
}

static bool recovery_pota_flash_read(uint32_t offset, void *data,
                                     size_t length)
{
    return recovery_flash_read(NULL, offset, data, (uint32_t)length);
}

static bool recovery_crypto_flash_read(void *context, uint32_t offset,
                                       void *data, uint32_t length)
{
    (void)context;
    return recovery_flash_read(NULL, offset, data, length);
}

static bool recovery_get_bcb_metadata(ota_metadata_t *metadata,
                                      uint32_t *security_counter)
{
    const pota_bcb_platform_t platform = {
        .read_page = recovery_bcb_read_page,
    };
    pota_bcb_store_t store;
    pota_bcb_view_t view;
    if (metadata == NULL ||
        pota_bcb_store_init_read_only(
            &store, &platform,
            FLASH_DEPLOYMENT_MAP_SCHEMA_VERSION,
            FLASH_DEPLOYMENT_MAP_VERSION,
            RECOVERY_BCB_LANE_PAGE_COUNT) != POTA_BCB_RESULT_OK ||
        pota_bcb_store_select_newest(&store, &view) != POTA_BCB_RESULT_OK ||
        view.update.payload_length != sizeof(*metadata)) {
        return false;
    }
    memcpy(metadata, view.update.payload, sizeof(*metadata));
    if (security_counter != NULL) {
        *security_counter = view.update.security_counter;
    }
    return pota_metadata_is_valid((const pota_metadata_t *)metadata);
}

typedef struct {
    uint32_t base;
} recovery_manifest_flash_context_t;

static bool recovery_manifest_read(void *context, uint32_t offset,
                                   void *data, uint32_t length)
{
    const recovery_manifest_flash_context_t *flash = context;
    return flash != NULL && data != NULL &&
           recovery_flash_read(NULL, flash->base + offset, data, length);
}

static ota_boot_result_t recovery_manifest_error_to_result(pota_error_t error)
{
    return error == POTA_ERR_SIGNATURE_INVALID
               ? OTA_BOOT_RESULT_SIGNATURE_INVALID
               : OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
}

static uint32_t recovery_validate_slot(const ota_metadata_t *metadata,
                                       ota_slot_t slot,
                                       uint32_t security_counter)
{
    if (metadata == NULL || (slot != OTA_SLOT_A && slot != OTA_SLOT_B)) {
        return OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    }
    const uint32_t image_size = slot == OTA_SLOT_A ? metadata->slot_a_size
                                                   : metadata->slot_b_size;
    const uint32_t image_crc = slot == OTA_SLOT_A ? metadata->slot_a_crc32
                                                  : metadata->slot_b_crc32;
    const uint32_t image_offset = ota_partition_slot_offset(slot);
    const uint32_t image_capacity = ota_partition_slot_size(slot);
    if (image_size == 0u) {
        return OTA_BOOT_RESULT_SLOT_EMPTY;
    }
    if (image_offset == 0u || image_size > image_capacity) {
        return OTA_BOOT_RESULT_SLOT_RANGE_INVALID;
    }
    const pota_image_vector_constraints_t constraints = {
        .sram_base = 0x20000000u,
        .sram_end = 0x20082000u,
        .xip_base = FLASH_DEPLOYMENT_GEOMETRY_XIP_BASE,
        .flash_read = recovery_pota_flash_read,
    };
    if (!pota_image_validate_app_vector(image_offset, image_size,
                                        image_offset, &constraints)) {
        return OTA_BOOT_RESULT_VECTOR_INVALID;
    }

    recovery_manifest_flash_context_t manifest_flash = {
        .base = OTA_SLOT_MANIFEST_BASE_OFFSET(slot),
    };
    pota_slot_manifest_store_t manifest_store;
    const pota_slot_manifest_config_t manifest_config = {
        .context = &manifest_flash,
        .read = recovery_manifest_read,
        .program = NULL,
        .erase = NULL,
        .base_offset = 0u,
        .lane_size = OTA_SLOT_MANIFEST_LANE_SIZE,
        .page_size = FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
        .erase_size = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
        .map_version = FLASH_DEPLOYMENT_MAP_VERSION,
        .slot = (pota_slot_t)slot,
    };
    if (pota_slot_manifest_init(&manifest_store, &manifest_config) !=
            POTA_SLOT_MANIFEST_OK) {
        return OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    }
    pota_slot_manifest_t durable;
    if (pota_slot_manifest_load(&manifest_store, &durable) !=
            POTA_SLOT_MANIFEST_OK) {
        return OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    }
    const pota_package_constraints_t package_constraints = {
        .product_id = PROJECT_PRODUCT_ID,
        .hardware_id = PROJECT_HARDWARE_ID,
        .bootloader_version = POTA_PACK_VERSION(
            PROJECT_BOOTLOADER_VERSION_MAJOR,
            PROJECT_BOOTLOADER_VERSION_MINOR,
            PROJECT_BOOTLOADER_VERSION_PATCH),
        .minimum_security_counter = security_counter,
        .require_signature = true,
        .require_image_hashes = true,
        .verify_signature = portable_ota_crypto_verify_manifest,
    };
    pota_package_manifest_t package_manifest;
    const pota_error_t manifest_result = pota_package_parse_header(
        durable.header, sizeof(durable.header), &package_constraints,
        &package_manifest);
    if (manifest_result != POTA_ERR_NONE) {
        return recovery_manifest_error_to_result(manifest_result);
    }
    const pota_package_image_t *package_image =
        pota_package_find_image(&package_manifest, (pota_slot_t)slot);
    if (package_image == NULL || package_image->size != image_size ||
        package_image->run_offset != image_offset ||
        package_image->crc32 != image_crc) {
        return OTA_BOOT_RESULT_COMPATIBILITY_INVALID;
    }
    uint8_t buffer[256];
    uint32_t crc = 0u;
    uint32_t offset = 0u;
    while (offset < image_size) {
        const uint32_t chunk = image_size - offset > sizeof(buffer)
                                   ? (uint32_t)sizeof(buffer)
                                   : image_size - offset;
        if (!recovery_flash_read(NULL, image_offset + offset, buffer, chunk)) {
            return OTA_BOOT_RESULT_SLOT_RANGE_INVALID;
        }
        crc = pota_crc32_update(crc, buffer, chunk);
        offset += chunk;
    }
    if (crc != image_crc) {
        return OTA_BOOT_RESULT_IMAGE_CRC_INVALID;
    }
    uint8_t digest[POTA_SHA256_SIZE];
    if (!portable_ota_crypto_sha256_flash(
            recovery_crypto_flash_read, NULL, image_offset, image_size,
            digest) ||
        memcmp(digest, package_image->sha256, sizeof(digest)) != 0) {
        return OTA_BOOT_RESULT_IMAGE_HASH_INVALID;
    }
    return OTA_BOOT_RESULT_APPLIED;
}

static void recovery_print_ab_status(void)
{
    ota_metadata_t metadata;
    uint32_t security_counter = 0u;
    if (!recovery_get_bcb_metadata(&metadata, &security_counter)) {
        puts("ERR,AB_METADATA\r");
        return;
    }
    const uint32_t slot_a = recovery_validate_slot(
        &metadata, OTA_SLOT_A, security_counter);
    const uint32_t slot_b = recovery_validate_slot(
        &metadata, OTA_SLOT_B, security_counter);
    const bool recovery_vector = pota_image_validate_app_vector(
        FLASH_DEPLOYMENT_MAP_RECOVERY_OFFSET,
        FLASH_DEPLOYMENT_MAP_RECOVERY_SIZE,
        FLASH_DEPLOYMENT_MAP_RECOVERY_OFFSET,
        &(const pota_image_vector_constraints_t){
            .sram_base = 0x20000000u,
            .sram_end = 0x20082000u,
            .xip_base = FLASH_DEPLOYMENT_GEOMETRY_XIP_BASE,
            .flash_read = recovery_pota_flash_read,
        });
    printf("%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
           (unsigned long)FLASH_DEPLOYMENT_MAP_VERSION,
           (unsigned long)metadata.active_slot,
           (unsigned long)metadata.pending_slot,
           (unsigned long)metadata.confirmed_slot,
           (unsigned long)slot_a,
           (unsigned long)slot_b,
           (unsigned long)metadata.last_boot_result,
           (unsigned long)(recovery_vector ? 1u : 0u),
           (unsigned long)metadata.boot_generation);
}

__attribute__((noinline)) bool recovery_get_bcb_health(
    pota_bcb_health_snapshot_t *health)
{
    const pota_bcb_platform_t platform = {
        .read_page = recovery_bcb_read_page,
    };
    pota_bcb_store_t store;
    return health != NULL &&
           pota_bcb_store_init_read_only(
               &store, &platform,
               FLASH_DEPLOYMENT_MAP_SCHEMA_VERSION,
               FLASH_DEPLOYMENT_MAP_VERSION,
               RECOVERY_BCB_LANE_PAGE_COUNT) == POTA_BCB_RESULT_OK &&
           pota_bcb_store_get_health_snapshot(&store, health);
}

__attribute__((noinline)) bool recovery_get_bcb_wear(
    pota_bcb_wear_snapshot_t *wear)
{
    const pota_bcb_platform_t platform = {
        .read_page = recovery_bcb_read_page,
    };
    pota_bcb_store_t store;
    return wear != NULL &&
           pota_bcb_store_init_read_only(
               &store, &platform,
               FLASH_DEPLOYMENT_MAP_SCHEMA_VERSION,
               FLASH_DEPLOYMENT_MAP_VERSION,
               RECOVERY_BCB_LANE_PAGE_COUNT) == POTA_BCB_RESULT_OK &&
           pota_bcb_store_get_wear_snapshot(&store, wear);
}

static void recovery_print_map(void)
{
    for (uint32_t index = 0u;
         index < sizeof(s_recovery_partitions) /
                     sizeof(s_recovery_partitions[0]);
         index++) {
        const recovery_partition_t *partition = &s_recovery_partitions[index];
        printf("%s,%lu,%lu,%lu%s", partition->name,
               (unsigned long)partition->id,
               (unsigned long)partition->offset,
               (unsigned long)partition->size,
               index + 1u == sizeof(s_recovery_partitions) /
                                  sizeof(s_recovery_partitions[0])
                   ? "\r\n"
                   : ";");
    }
}

static void recovery_print_bcb_health(void)
{
    pota_bcb_health_snapshot_t health;
    if (!recovery_get_bcb_health(&health)) {
        puts("ERR,BCB_READ\r");
        return;
    }
    printf("%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
           (unsigned long)health.valid_lane_count,
           (unsigned long)health.valid_record_count,
           (unsigned long)health.newest_lane_generation,
           (unsigned long)health.newest_sequence,
           (unsigned long)health.newest_security_counter,
           (unsigned long)health.newest_lane,
           (unsigned long)health.newest_record_page);
}

static void recovery_print_bcb_wear(void)
{
    pota_bcb_wear_snapshot_t wear;
    if (!recovery_get_bcb_wear(&wear)) {
        puts("ERR,BCB_READ\r");
        return;
    }
    printf("%lu,%lu\r\n",
           (unsigned long)wear.program_page_count,
           (unsigned long)wear.erase_lane_count);
}

static void recovery_process_command(const char *command)
{
    if (strcmp(command, "*IDN?") == 0) {
        printf("Aphranda,DHRT100,RECOVERY,%s\r\n", g_project_build_id);
    } else if (strcmp(command, "SYST:RECOVERY:STATUS?") == 0) {
        printf("RECOVERY,%lu,%s,%s\r\n",
               (unsigned long)FLASH_DEPLOYMENT_MAP_VERSION,
               FLASH_DEPLOYMENT_MAP_STATE, g_project_build_id);
    } else if (strcmp(command, "SYST:RECOVERY:MAP?") == 0) {
        recovery_print_map();
    } else if (strcmp(command, "SYST:RECOVERY:BCB:HEALTH?") == 0) {
        recovery_print_bcb_health();
    } else if (strcmp(command, "SYST:RECOVERY:BCB:WEAR?") == 0) {
        recovery_print_bcb_wear();
    } else if (strcmp(command, "SYST:RECOVERY:AB:STATUS?") == 0) {
        recovery_print_ab_status();
    } else if (strcmp(command, "SYST:RECOVERY:BOOTSEL DHRT100") == 0) {
        puts("OK,BOOTSEL\r");
        stdio_flush();
        sleep_ms(20u);
        reset_usb_boot(0u, 0u);
    } else {
        puts("ERR,UNKNOWN_COMMAND\r");
    }
}

int main(void)
{
    stdio_init_all();
    char command[RECOVERY_COMMAND_CAPACITY];
    uint32_t length = 0u;

    while (true) {
        const int character = getchar_timeout_us(1000u);
        if (character == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }
        if (character == '\r' || character == '\n') {
            if (length != 0u) {
                command[length] = '\0';
                recovery_process_command(command);
                length = 0u;
            }
            continue;
        }
        if (length + 1u < sizeof(command)) {
            command[length++] = (char)character;
        } else {
            length = 0u;
            puts("ERR,COMMAND_TOO_LONG\r");
        }
    }
}
