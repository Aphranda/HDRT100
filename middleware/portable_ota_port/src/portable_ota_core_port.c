#include "portable_ota_port.h"

#include "ota_error.h"
#include "ota_partition.h"
#include "pota.h"

#define PORTABLE_OTA_PRODUCT_ID "RP2350_TRIG"
#define PORTABLE_OTA_HARDWARE_ID "rp2350_trig"
#define PORTABLE_OTA_BOOTLOADER_VERSION POTA_PACK_VERSION(0u, 1u, 0u)

/*
 * PORTABLE_OTA_PORT_ENABLE_SESSION=0 keeps only bootloader-safe base adapters:
 * CRC, string conversion, and product error mapping.
 * PORTABLE_OTA_PORT_ENABLE_SESSION=1 enables the app-side OTA session path that
 * erases/programs flash and updates pending-slot metadata.
 */
#ifndef PORTABLE_OTA_PORT_ENABLE_SESSION
#define PORTABLE_OTA_PORT_ENABLE_SESSION 0
#endif

#if PORTABLE_OTA_PORT_ENABLE_SESSION
#include "flash_transaction.h"
#include "resource_arbiter.h"
#endif

static const pota_compat_text_entry_t s_product_error_texts[] = {
    {OTA_ERR_BOARD_MISMATCH, "BOARD_MISMATCH"},
    {OTA_ERR_VERSION_REJECTED, "VERSION_REJECTED"},
    {OTA_ERR_BOOT_ROLLBACK, "BOOT_ROLLBACK"},
    {OTA_ERR_QUEUE_FULL, "QUEUE_FULL"},
};

uint32_t portable_ota_port_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    return pota_crc32_update(crc, data, length);
}

uint32_t portable_ota_port_crc32_compute(const uint8_t *data, size_t length)
{
    return pota_crc32_compute(data, length);
}

const char *portable_ota_port_state_to_string(ota_state_t state)
{
    return pota_state_to_string((pota_state_t)state);
}

const char *portable_ota_port_error_to_string(uint32_t error_code)
{
    const char *alias =
        pota_compat_text_u32(error_code,
                             s_product_error_texts,
                             sizeof(s_product_error_texts) / sizeof(s_product_error_texts[0]),
                             NULL);
    if (alias != NULL) {
        return alias;
    }

    const pota_error_t error = pota_compat_product_to_error(error_code,
                                                            NULL,
                                                            0u,
                                                            (pota_error_t)UINT32_MAX);
    return pota_error_to_string(error);
}

const char *portable_ota_port_result_to_string(ota_result_t result)
{
    const pota_result_t portable_result =
        pota_compat_product_to_result((uint32_t)result, NULL, 0u, (pota_result_t)UINT32_MAX);
    return pota_result_to_string(portable_result);
}

const char *portable_ota_port_boot_result_to_string(uint32_t result)
{
    return pota_boot_result_to_string((pota_boot_result_t)result);
}

#if PORTABLE_OTA_PORT_ENABLE_SESSION

static pota_session_t s_session;
static uint32_t s_provider_generation;
static uint32_t s_store_generation;
static uint32_t s_provider_refs;

static const pota_compat_map_entry_t s_product_error_aliases[] = {
    {POTA_ERR_PRODUCT_MISMATCH, OTA_ERR_BOARD_MISMATCH},
    {POTA_ERR_HARDWARE_MISMATCH, OTA_ERR_BOARD_MISMATCH},
    {POTA_ERR_BOOTLOADER_TOO_OLD, OTA_ERR_VERSION_REJECTED},
};

static uint32_t portable_core_next_provider_generation(void)
{
    s_provider_generation++;
    if (s_provider_generation == 0u) {
        s_provider_generation = 1u;
    }
    return s_provider_generation;
}

static bool portable_core_provider_retain(void *context)
{
    uint32_t *refs = context;
    if (refs == NULL || *refs == UINT32_MAX) {
        return false;
    }
    (*refs)++;
    return true;
}

static void portable_core_provider_release(void *context)
{
    uint32_t *refs = context;
    if (refs != NULL && *refs != 0u) {
        (*refs)--;
    }
}

static bool portable_core_flash_execute(uint32_t operation,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        uint32_t size)
{
    uint32_t partition_id = 0u;
    uint32_t relative_offset = 0u;
    if (!flash_transaction_ao_resolve_range(offset, size, &partition_id,
                                            &relative_offset)) {
        return false;
    }

    const uint32_t provider_generation =
        operation == FLASH_TRANSACTION_OPERATION_PROGRAM
            ? portable_core_next_provider_generation()
            : 0u;
    const flash_transaction_buffer_lease_t lease = {
        .data = data,
        .length = size,
        .generation = provider_generation,
        .context = &s_provider_refs,
        .retain = portable_core_provider_retain,
        .release = portable_core_provider_release,
    };
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_IMAGE,
        .partition_id = partition_id,
        .operation = operation,
        .relative_offset = relative_offset,
        .length = size,
        .data = data,
        .provider_generation = provider_generation,
        .store_generation = s_store_generation,
        .buffer_lease = operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                            ? &lease
                            : NULL,
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool portable_core_flash_erase(uint32_t offset, uint32_t size)
{
    return portable_core_flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                                       offset, NULL, size);
}

static bool portable_core_flash_program(uint32_t offset, const void *data,
                                        uint32_t size)
{
    return portable_core_flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                       offset, data, size);
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
    return pota_compat_error_to_product((pota_error_t)error,
                                        s_product_error_aliases,
                                        sizeof(s_product_error_aliases) /
                                            sizeof(s_product_error_aliases[0]),
                                        (uint32_t)OTA_ERR_BAD_ARGUMENT);
}

static uint32_t portable_core_map_result(uint32_t result)
{
    return pota_compat_result_to_product((pota_result_t)result,
                                         NULL,
                                         0u,
                                         (uint32_t)OTA_RESULT_FAILED);
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
    pota_session_get_status(&s_session, &status);
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
            .flash_page_size = FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES,
            .flash_sector_size = FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES,
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
    if (metadata == NULL ||
        (metadata->active_slot != (uint32_t)OTA_SLOT_A &&
         metadata->active_slot != (uint32_t)OTA_SLOT_B)) {
        return false;
    }
    const uint32_t active_partition_id =
        metadata->active_slot == (uint32_t)OTA_SLOT_A
            ? FLASH_COMPAT_MAP_APP_A_ID
            : FLASH_COMPAT_MAP_APP_B_ID;
    if (!flash_transaction_ao_set_active_app_partition(active_partition_id)) {
        return false;
    }
    s_store_generation = metadata->sequence;

    const pota_platform_t platform = portable_core_make_platform(metadata);
    if (!pota_session_init(&s_session, &platform)) {
        return false;
    }

    const pota_begin_t begin = {
        .size = size,
        .crc32 = crc32,
        .package_mode = package_mode,
    };

    const pota_error_t error = pota_session_begin(&s_session, &begin);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_service(uint32_t budget_us, ota_vector_t *vector)
{
    const pota_error_t error = pota_session_service(&s_session, budget_us);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_write(const uint8_t *data, uint32_t length, ota_vector_t *vector)
{
    const pota_error_t error = pota_session_write(&s_session, data, length);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_end(ota_vector_t *vector)
{
    const pota_error_t error = pota_session_end(&s_session);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

bool portable_ota_port_core_abort(ota_vector_t *vector)
{
    const pota_error_t error = pota_session_abort(&s_session);
    portable_core_copy_status(vector);
    return error == POTA_ERR_NONE;
}

#endif
