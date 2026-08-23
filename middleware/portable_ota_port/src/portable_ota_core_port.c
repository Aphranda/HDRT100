#include "portable_ota_port.h"

#include "ota_error.h"
#include "ota_partition.h"
#include "pota_slot_manifest.h"
#include "pota.h"
#include "project_config.h"
#include "drv_flash.h"

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
#include "drv_watchdog.h"
#include "flash_transaction.h"
#include "ota_journal.h"
#if PROJECT_FLASH_DEPLOYMENT_V2
#include "portable_ota_crypto.h"
#include "portable_ota_key_registry.generated.h"
#endif
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
static pota_stream_session_t s_stream_session;
static pota_stream_ingress_t s_stream_ingress;
static pota_platform_t s_stream_platform;
static bool s_stream_initialized;
static uint32_t s_provider_generation;
static uint32_t s_store_generation;
static uint32_t s_provider_refs;
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
static pota_slot_manifest_store_t s_slot_manifest_store;
static uint32_t s_slot_manifest_base;
#endif

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
        .completion_lease = flash_transaction_ao_get_completion_lease(),
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool portable_core_flash_erase(uint32_t offset, uint32_t size)
{
    return portable_core_flash_execute(FLASH_TRANSACTION_OPERATION_ERASE,
                                       offset, NULL, size);
}

static bool portable_core_flash_read(uint32_t offset, void *data, uint32_t size)
{
    return drv_flash_read(offset, data, size);
}

static bool portable_core_flash_program(uint32_t offset, const void *data,
                                        uint32_t size)
{
    return portable_core_flash_execute(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                       offset, data, size);
}

static bool portable_core_mark_pending(pota_slot_t slot, uint32_t image_size,
                                       uint32_t image_crc32,
                                       uint32_t security_counter)
{
    /* BCB lane rotation is a bounded maintenance transaction but can exceed
     * the normal 3 s runtime watchdog on a cold external-flash lane. */
    drv_watchdog_enable(30000u);
    const bool ok = ota_metadata_mark_pending((ota_slot_t)slot, image_size,
                                              image_crc32, security_counter);
    drv_watchdog_enable(PROJECT_WATCHDOG_TIMEOUT_MS);
    return ok;
}

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
static bool portable_core_manifest_read(void *context, uint32_t offset,
                                        void *data, uint32_t length)
{
    (void)context;
    return data != NULL && drv_flash_read(s_slot_manifest_base + offset,
                                          data, length);
}

static bool portable_core_manifest_program(void *context, uint32_t offset,
                                           const void *data, uint32_t length)
{
    (void)context;
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_MANIFEST,
        .partition_id = FLASH_DEPLOYMENT_MAP_OTA_STAGE_ID,
        .operation = FLASH_TRANSACTION_OPERATION_PROGRAM,
        .relative_offset = (s_slot_manifest_base -
                            FLASH_DEPLOYMENT_MAP_OTA_STAGE_OFFSET) + offset,
        .length = length,
        .data = data,
        .provider_generation = portable_core_next_provider_generation(),
        .store_generation = s_store_generation,
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool portable_core_manifest_erase(void *context, uint32_t offset,
                                         uint32_t length)
{
    (void)context;
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_MANIFEST,
        .partition_id = FLASH_DEPLOYMENT_MAP_OTA_STAGE_ID,
        .operation = FLASH_TRANSACTION_OPERATION_ERASE,
        .relative_offset = (s_slot_manifest_base -
                            FLASH_DEPLOYMENT_MAP_OTA_STAGE_OFFSET) + offset,
        .length = length,
        .store_generation = s_store_generation,
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool portable_core_commit_slot_manifest(pota_slot_t slot,
                                               const uint8_t *header,
                                               uint32_t header_size)
{
    if (header == NULL || header_size != POTA_PACKAGE_HEADER_SIZE ||
        (slot != POTA_SLOT_A && slot != POTA_SLOT_B)) {
        return false;
    }
    const ota_slot_t ota_slot = (ota_slot_t)slot;
    s_slot_manifest_base = OTA_SLOT_MANIFEST_BASE_OFFSET(ota_slot);
    const pota_slot_manifest_config_t config = {
        .context = NULL,
        .read = portable_core_manifest_read,
        .program = portable_core_manifest_program,
        .erase = portable_core_manifest_erase,
        .base_offset = 0u,
        .lane_size = OTA_SLOT_MANIFEST_LANE_BYTES,
        .page_size = FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
        .erase_size = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
        .map_version = FLASH_DEPLOYMENT_MAP_VERSION,
        .slot = slot,
    };
    if (pota_slot_manifest_init(&s_slot_manifest_store, &config) !=
        POTA_SLOT_MANIFEST_OK) {
        return false;
    }
    drv_watchdog_enable(30000u);
    const bool ok = pota_slot_manifest_append(&s_slot_manifest_store, header, NULL) ==
                    POTA_SLOT_MANIFEST_OK;
    drv_watchdog_enable(PROJECT_WATCHDOG_TIMEOUT_MS);
    return ok;
}
#endif

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
#if PROJECT_FLASH_DEPLOYMENT_V2
    ota_metadata_bcb_health_t health;
    const uint32_t security_counter = ota_metadata_get_bcb_health(&health)
        ? health.newest_security_counter
        : UINT32_MAX;
#endif
    const pota_platform_t platform = {
        .info = {
            .product_id = PROJECT_PRODUCT_ID,
            .hardware_id = PROJECT_HARDWARE_ID,
            .bootloader_version = PORTABLE_OTA_BOOTLOADER_VERSION,
            .map_version = FLASH_DEPLOYMENT_MAP_VERSION,
            .slot_a_partition_id = FLASH_DEPLOYMENT_MAP_APP_A_ID,
            .slot_b_partition_id = FLASH_DEPLOYMENT_MAP_APP_B_ID,
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
            .flash_page_size = FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
            .flash_sector_size = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
#if PROJECT_FLASH_DEPLOYMENT_V2
            .security_counter = security_counter,
            .require_signature = PORTABLE_OTA_REQUIRE_SIGNATURE != 0,
            .require_image_hashes = true,
            .verify_manifest_signature = portable_ota_crypto_verify_manifest,
#endif
        },
        .ops = {
            .flash_read = portable_core_flash_read,
            .flash_erase = portable_core_flash_erase,
            .flash_program = portable_core_flash_program,
            .mark_pending = portable_core_mark_pending,
            .confirm_active = portable_core_confirm_active,
            .validate_vector = portable_core_validate_vector,
#if PROJECT_FLASH_DEPLOYMENT_V2
            .commit_slot_manifest = portable_core_commit_slot_manifest,
#endif
        },
    };
    return platform;
}

static bool portable_core_init_stream_surfaces(void)
{
    if (!pota_stream_session_init(&s_stream_session, &s_stream_platform)) {
        return false;
    }
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    if (!ota_journal_init() || !ota_journal_attach(&s_stream_session)) {
        return false;
    }
#endif
    return pota_stream_ingress_init(
        &s_stream_ingress,
        &s_stream_session,
        (1u << (uint32_t)POTA_STREAM_INGRESS_SOURCE_COUNT) - 1u,
        POTA_MAX_DATA_BLOCK_SIZE);
}

bool portable_ota_port_stream_init(const ota_metadata_t *metadata)
{
    s_stream_initialized = false;
    if (metadata == NULL ||
        (metadata->active_slot != (uint32_t)OTA_SLOT_A &&
         metadata->active_slot != (uint32_t)OTA_SLOT_B)) {
        return false;
    }

    const uint32_t active_partition_id =
        metadata->active_slot == (uint32_t)OTA_SLOT_A
            ? FLASH_DEPLOYMENT_MAP_APP_A_ID
            : FLASH_DEPLOYMENT_MAP_APP_B_ID;
    if (!flash_transaction_ao_set_active_app_partition(active_partition_id)) {
        return false;
    }
    s_store_generation = metadata->sequence;

    s_stream_platform = portable_core_make_platform(metadata);
    if (!portable_core_init_stream_surfaces()) {
        return false;
    }
    s_stream_initialized = true;
    return true;
}

pota_stream_ingress_result_t portable_ota_port_stream_open(
    pota_stream_ingress_source_t source,
    const pota_stream_open_t *open)
{
    if (!s_stream_initialized) {
        return POTA_STREAM_INGRESS_SESSION;
    }
    const pota_stream_state_t state =
        pota_stream_session_state(&s_stream_session);
    if (state == POTA_STREAM_STATE_ABORTED ||
        state == POTA_STREAM_STATE_FAILED) {
        if (!portable_core_init_stream_surfaces()) {
            return POTA_STREAM_INGRESS_SESSION;
        }
    }
    return pota_stream_ingress_open(&s_stream_ingress, source, open);
}

pota_stream_ingress_result_t portable_ota_port_stream_write(
    pota_stream_ingress_source_t source,
    uint32_t offset,
    const uint8_t *data,
    uint32_t size,
    bool has_crc32,
    uint32_t crc32)
{
    if (!s_stream_initialized) {
        return POTA_STREAM_INGRESS_SESSION;
    }
    return pota_stream_ingress_write(&s_stream_ingress, source, offset, data,
                                     size, has_crc32, crc32);
}

pota_stream_ingress_result_t portable_ota_port_stream_service(uint32_t budget_us)
{
    if (!s_stream_initialized) {
        return POTA_STREAM_INGRESS_SESSION;
    }
    pota_stream_ingress_status_t status;
    if (!pota_stream_ingress_get_status(&s_stream_ingress, &status) ||
        status.source >= POTA_STREAM_INGRESS_SOURCE_COUNT) {
        return POTA_STREAM_INGRESS_SESSION;
    }
    return pota_stream_ingress_service(&s_stream_ingress, status.source,
                                       budget_us);
}

pota_stream_ingress_result_t portable_ota_port_stream_close(
    pota_stream_ingress_source_t source)
{
    if (!s_stream_initialized) {
        return POTA_STREAM_INGRESS_SESSION;
    }
    return pota_stream_ingress_close(&s_stream_ingress, source);
}

pota_stream_ingress_result_t portable_ota_port_stream_abort(
    pota_stream_ingress_source_t source)
{
    if (!s_stream_initialized) {
        return POTA_STREAM_INGRESS_SESSION;
    }
    return pota_stream_ingress_abort(&s_stream_ingress, source);
}

bool portable_ota_port_stream_is_active(void)
{
    if (!s_stream_initialized) {
        return false;
    }
    const pota_stream_state_t state =
        pota_stream_session_state(&s_stream_session);
    return state == POTA_STREAM_STATE_OPEN ||
           state == POTA_STREAM_STATE_RECEIVING ||
           state == POTA_STREAM_STATE_READY_TO_REBOOT;
}

bool portable_ota_port_stream_get_status(pota_stream_ingress_status_t *status)
{
    return s_stream_initialized &&
           pota_stream_ingress_get_status(&s_stream_ingress, status);
}

bool portable_ota_port_core_begin(const ota_metadata_t *metadata,
                                  uint32_t size,
                                  uint32_t crc32,
                                  bool package_mode,
                                  ota_vector_t *vector)
{
    if (portable_ota_port_stream_is_active() || metadata == NULL ||
        (metadata->active_slot != (uint32_t)OTA_SLOT_A &&
         metadata->active_slot != (uint32_t)OTA_SLOT_B)) {
        return false;
    }
    const uint32_t active_partition_id =
        metadata->active_slot == (uint32_t)OTA_SLOT_A
            ? FLASH_DEPLOYMENT_MAP_APP_A_ID
            : FLASH_DEPLOYMENT_MAP_APP_B_ID;
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

#if !PORTABLE_OTA_PORT_ENABLE_SESSION
bool portable_ota_port_stream_init(const ota_metadata_t *metadata)
{
    (void)metadata;
    return false;
}

pota_stream_ingress_result_t portable_ota_port_stream_open(
    pota_stream_ingress_source_t source, const pota_stream_open_t *open)
{
    (void)source;
    (void)open;
    return POTA_STREAM_INGRESS_SESSION;
}

pota_stream_ingress_result_t portable_ota_port_stream_write(
    pota_stream_ingress_source_t source, uint32_t offset, const uint8_t *data,
    uint32_t size, bool has_crc32, uint32_t crc32)
{
    (void)source;
    (void)offset;
    (void)data;
    (void)size;
    (void)has_crc32;
    (void)crc32;
    return POTA_STREAM_INGRESS_SESSION;
}

pota_stream_ingress_result_t portable_ota_port_stream_service(uint32_t budget_us)
{
    (void)budget_us;
    return POTA_STREAM_INGRESS_SESSION;
}

pota_stream_ingress_result_t portable_ota_port_stream_close(
    pota_stream_ingress_source_t source)
{
    (void)source;
    return POTA_STREAM_INGRESS_SESSION;
}

pota_stream_ingress_result_t portable_ota_port_stream_abort(
    pota_stream_ingress_source_t source)
{
    (void)source;
    return POTA_STREAM_INGRESS_SESSION;
}

bool portable_ota_port_stream_is_active(void)
{
    return false;
}

bool portable_ota_port_stream_get_status(pota_stream_ingress_status_t *status)
{
    (void)status;
    return false;
}
#endif
