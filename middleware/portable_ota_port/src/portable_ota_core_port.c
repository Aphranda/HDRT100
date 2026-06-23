#include "portable_ota_port.h"

#include "drv_flash.h"
#include "ota_error.h"
#include "ota_partition.h"
#include "pota_core.h"

#define PORTABLE_OTA_PRODUCT_ID "RP2350_TRIG"
#define PORTABLE_OTA_HARDWARE_ID "rp2350_trig"
#define PORTABLE_OTA_BOOTLOADER_VERSION POTA_PACK_VERSION(0u, 1u, 0u)

static pota_context_t s_core;

static bool portable_core_flash_erase(uint32_t offset, uint32_t size)
{
    return drv_flash_erase(offset, size);
}

static bool portable_core_flash_program(uint32_t offset, const void *data, uint32_t size)
{
    return drv_flash_program(offset, data, size);
}

static bool portable_core_mark_pending(pota_slot_t slot, uint32_t image_size, uint32_t image_crc32)
{
    return ota_metadata_mark_pending((ota_slot_t)slot, image_size, image_crc32);
}

static bool portable_core_confirm_active(void)
{
    return ota_metadata_confirm_active();
}

static bool portable_core_validate_vector(uint32_t slot_offset,
                                          uint32_t image_size,
                                          uint32_t run_offset)
{
    return portable_ota_port_validate_app_vector(slot_offset, image_size, run_offset);
}

static uint32_t portable_core_map_error(uint32_t error)
{
    switch ((pota_error_t)error) {
    case POTA_ERR_NONE:
        return (uint32_t)OTA_ERR_NONE;
    case POTA_ERR_BUSY:
        return (uint32_t)OTA_ERR_BUSY;
    case POTA_ERR_INVALID_STATE:
        return (uint32_t)OTA_ERR_INVALID_STATE;
    case POTA_ERR_IMAGE_TOO_LARGE:
        return (uint32_t)OTA_ERR_IMAGE_TOO_LARGE;
    case POTA_ERR_BAD_HEADER:
        return (uint32_t)OTA_ERR_BAD_HEADER;
    case POTA_ERR_PRODUCT_MISMATCH:
    case POTA_ERR_HARDWARE_MISMATCH:
        return (uint32_t)OTA_ERR_BOARD_MISMATCH;
    case POTA_ERR_BOOTLOADER_TOO_OLD:
        return (uint32_t)OTA_ERR_VERSION_REJECTED;
    case POTA_ERR_FLASH_ERASE:
        return (uint32_t)OTA_ERR_FLASH_ERASE;
    case POTA_ERR_FLASH_PROGRAM:
        return (uint32_t)OTA_ERR_FLASH_PROGRAM;
    case POTA_ERR_READBACK:
        return (uint32_t)OTA_ERR_READBACK;
    case POTA_ERR_CRC:
        return (uint32_t)OTA_ERR_CRC;
    case POTA_ERR_VECTOR:
        return (uint32_t)OTA_ERR_VECTOR;
    case POTA_ERR_METADATA:
        return (uint32_t)OTA_ERR_METADATA;
    case POTA_ERR_ABORTED:
        return (uint32_t)OTA_ERR_ABORTED;
    case POTA_ERR_BAD_ARGUMENT:
    default:
        return (uint32_t)OTA_ERR_BAD_ARGUMENT;
    }
}

static uint32_t portable_core_map_result(uint32_t result)
{
    switch ((pota_result_t)result) {
    case POTA_RESULT_NONE:
        return (uint32_t)OTA_RESULT_NONE;
    case POTA_RESULT_ACCEPTED:
        return (uint32_t)OTA_RESULT_ACCEPTED;
    case POTA_RESULT_IMAGE_STAGED:
        return (uint32_t)OTA_RESULT_IMAGE_STAGED;
    case POTA_RESULT_COMMITTED:
        return (uint32_t)OTA_RESULT_COMMITTED;
    case POTA_RESULT_FAILED:
        return (uint32_t)OTA_RESULT_FAILED;
    case POTA_RESULT_ABORTED:
        return (uint32_t)OTA_RESULT_ABORTED;
    default:
        return (uint32_t)OTA_RESULT_FAILED;
    }
}

static pota_boot_mode_t portable_core_boot_mode_from_metadata(const ota_metadata_t *metadata)
{
    if (metadata != NULL &&
        metadata->boot_mode == (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return POTA_BOOT_MODE_DIRECT_AB;
    }

    return POTA_BOOT_MODE_COPY_TO_ACTIVE;
}

static pota_slot_t portable_core_active_slot_from_metadata(const ota_metadata_t *metadata)
{
    if (metadata != NULL && metadata->active_slot == (uint32_t)OTA_SLOT_B) {
        return POTA_SLOT_B;
    }

    return POTA_SLOT_A;
}

static void portable_core_copy_status(ota_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }

    pota_status_t status;
    pota_get_status(&s_core, &status);
    vector->state = status.state;
    vector->target_slot = status.target_slot;
    vector->expected_size = status.expected_size;
    vector->received_size = status.received_size;
    vector->programmed_size = status.programmed_size;
    vector->crc32_expected = status.crc32_expected;
    vector->crc32_running = status.crc32_running;
    vector->progress_permille = status.progress_permille;
    vector->error_code = portable_core_map_error(status.error_code);
    vector->last_result = portable_core_map_result(status.last_result);
}

static pota_platform_t portable_core_make_platform(const ota_metadata_t *metadata)
{
    const pota_platform_t platform = {
        .info = {
            .product_id = PORTABLE_OTA_PRODUCT_ID,
            .hardware_id = PORTABLE_OTA_HARDWARE_ID,
            .bootloader_version = PORTABLE_OTA_BOOTLOADER_VERSION,
            .boot_mode = portable_core_boot_mode_from_metadata(metadata),
            .active_slot = portable_core_active_slot_from_metadata(metadata),
            .slot_a = {
                .offset = OTA_SLOT_A_OFFSET,
                .size = OTA_SLOT_A_SIZE,
                .run_offset = OTA_SLOT_A_OFFSET,
            },
            .slot_b = {
                .offset = OTA_SLOT_B_OFFSET,
                .size = OTA_SLOT_B_SIZE,
                .run_offset = OTA_SLOT_B_OFFSET,
            },
            .flash_page_size = DRV_FLASH_PAGE_SIZE,
            .flash_sector_size = DRV_FLASH_SECTOR_SIZE,
        },
        .ops = {
            .flash_read = NULL,
            .flash_erase = portable_core_flash_erase,
            .flash_program = portable_core_flash_program,
            .mark_pending = portable_core_mark_pending,
            .confirm_active = portable_core_confirm_active,
            .validate_vector = portable_core_validate_vector,
        },
    };
    return platform;
}

bool portable_ota_port_core_begin(const ota_metadata_t *metadata,
                                  uint32_t size,
                                  uint32_t crc32,
                                  bool package_mode,
                                  ota_vector_t *vector)
{
    const pota_platform_t platform = portable_core_make_platform(metadata);
    if (!pota_init(&s_core, &platform)) {
        return false;
    }

    const pota_begin_t begin = {
        .size = size,
        .crc32 = crc32,
        .package_mode = package_mode,
    };

    const pota_error_t error = pota_begin(&s_core, &begin);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_service(uint32_t budget_us, ota_vector_t *vector)
{
    const pota_error_t error = pota_service(&s_core, budget_us);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_write(const uint8_t *data, uint32_t length, ota_vector_t *vector)
{
    const pota_error_t error = pota_write(&s_core, data, length);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_end(ota_vector_t *vector)
{
    const pota_error_t error = pota_end(&s_core);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_abort(ota_vector_t *vector)
{
    const pota_error_t error = pota_abort(&s_core);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}
