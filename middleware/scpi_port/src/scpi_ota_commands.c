#include "scpi_ota_commands.h"

#include <stddef.h>
#include <stdint.h>

#include "distributed_config.h"
#include "drv_flash.h"
#include "drv_watchdog.h"
#include "ota_ao.h"
#include "ota_journal.h"
#include "product_config.h"
#include "scpi_port_internal.h"
#include "portable_ota_port.h"

scpi_result_t scpi_cmd_ota_status_q(scpi_t *context)
{
    ota_vector_t vector;
    ota_ao_get_vector(&vector);
    SCPI_ResultText(context, ota_state_to_string((ota_state_t)vector.state));
    SCPI_ResultUInt32(context, vector.target_slot);
    SCPI_ResultText(context, ota_error_to_string(vector.error_code));
    SCPI_ResultUInt32(context, vector.last_result);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_progress_q(scpi_t *context)
{
    ota_vector_t vector;
    ota_ao_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.received_size);
    SCPI_ResultUInt32(context, vector.expected_size);
    SCPI_ResultUInt32(context, vector.progress_permille);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_begin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (portable_ota_port_stream_is_active()) {
        return SCPI_RES_ERR;
    }

    uint32_t size;
    uint32_t crc32;
    if (!scpi_port_read_u32(context, &size) || !scpi_port_read_u32(context, &crc32)) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = OTA_EVENT_BEGIN,
        .payload.begin = {
            .size = size,
            .crc32 = crc32,
            .image_version = 0u,
            .flags = 0u,
        },
    };

    return ota_ao_post_event(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_package_begin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (portable_ota_port_stream_is_active()) {
        return SCPI_RES_ERR;
    }

    uint32_t size;
    uint32_t crc32;
    if (!scpi_port_read_u32(context, &size) || !scpi_port_read_u32(context, &crc32)) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = OTA_EVENT_BEGIN,
        .payload.begin = {
            .size = size,
            .crc32 = crc32,
            .image_version = 0u,
            .flags = OTA_BEGIN_FLAG_PACKAGE,
        },
    };

    return ota_ao_post_event(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_data(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (portable_ota_port_stream_is_active()) {
        return SCPI_RES_ERR;
    }

    const char *data = NULL;
    size_t length = 0u;
    if (SCPI_ParamArbitraryBlock(context, &data, &length, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (length == 0u || length > OTA_EVENT_MAX_DATA_SIZE) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = OTA_EVENT_DATA_BLOCK,
        .payload.data = {
            .data = (const uint8_t *)data,
            .length = (uint32_t)length,
            .block_index = 0u,
        },
    };

    return ota_ao_post_event(&event) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_ota_simple_event_ack(scpi_t *context, ota_event_type_t type)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (portable_ota_port_stream_is_active()) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = type,
    };

    return ota_ao_post_event(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_end(scpi_t *context)
{
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_END);
}

scpi_result_t scpi_cmd_ota_abort(scpi_t *context)
{
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_ABORT);
}

scpi_result_t scpi_cmd_ota_boot(scpi_t *context)
{
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_BOOT);
}

scpi_result_t scpi_cmd_ota_commit(scpi_t *context)
{
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_COMMIT);
}

scpi_result_t scpi_cmd_ota_slot_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.active_slot);
    SCPI_ResultUInt32(context, metadata.pending_slot);
    SCPI_ResultUInt32(context, metadata.confirmed_slot);
    SCPI_ResultUInt32(context, metadata.boot_attempts);
    SCPI_ResultUInt32(context, metadata.rollback_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_result_q(scpi_t *context)
{
    ota_vector_t vector;
    ota_metadata_t metadata;
    ota_ao_get_vector(&vector);

    SCPI_ResultUInt32(context, vector.last_result);
    SCPI_ResultText(context, ota_error_to_string(vector.error_code));
    if (ota_ao_get_metadata(&metadata)) {
        SCPI_ResultText(context, ota_metadata_boot_result_to_string(metadata.last_boot_result));
        SCPI_ResultUInt32(context, metadata.last_boot_source_slot);
        SCPI_ResultUInt32(context, metadata.last_boot_size);
        SCPI_ResultUInt32(context, metadata.last_boot_crc32);
    } else {
        SCPI_ResultText(context, "METADATA_LOAD_FAILED");
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_transaction_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.copy_txn_state);
    SCPI_ResultUInt32(context, metadata.copy_source_slot);
    SCPI_ResultUInt32(context, metadata.copy_destination_slot);
    SCPI_ResultUInt32(context, metadata.copy_size);
    SCPI_ResultUInt32(context, metadata.copy_crc32);
    SCPI_ResultUInt32(context, metadata.copy_written);
    SCPI_ResultUInt32(context, metadata.copy_attempts);
    SCPI_ResultUInt32(context, metadata.copy_last_error);
    return SCPI_RES_OK;
}

static const char *scpi_ota_boot_mode_to_string(uint32_t mode)
{
    switch ((ota_boot_mode_t)mode) {
    case OTA_BOOT_MODE_COPY_TO_ACTIVE:
        return "COPY_TO_ACTIVE";
    case OTA_BOOT_MODE_DIRECT_AB:
        return "DIRECT_AB";
    default:
        return "UNKNOWN";
    }
}

static uint32_t scpi_ota_next_target_slot(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return (uint32_t)OTA_SLOT_NONE;
    }

    if (metadata->boot_mode != (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return (uint32_t)OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_A) {
        return (uint32_t)OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_B) {
        return (uint32_t)OTA_SLOT_A;
    }

    return (uint32_t)OTA_SLOT_B;
}

scpi_result_t scpi_cmd_ota_mode_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, scpi_ota_boot_mode_to_string(metadata.boot_mode));
    SCPI_ResultUInt32(context, metadata.boot_mode);
    return SCPI_RES_OK;
}

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
scpi_result_t scpi_cmd_ota_mode(scpi_t *context)
{
    uint32_t mode;
    if (!scpi_port_read_u32(context, &mode) ||
        mode > (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return SCPI_RES_ERR;
    }

    return ota_metadata_set_boot_mode((ota_boot_mode_t)mode) ?
               scpi_port_result_ok(context) :
               SCPI_RES_ERR;
}
#endif

scpi_result_t scpi_cmd_ota_target_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, scpi_ota_next_target_slot(&metadata));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_capability_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.boot_capabilities);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_journal_q(scpi_t *context)
{
    ota_journal_snapshot_t snapshot;
    if (!ota_journal_get_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, snapshot.valid ? 1u : 0u);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.result);
    SCPI_ResultUInt32(context, snapshot.sequence);
    SCPI_ResultUInt32(context, snapshot.checkpoint.session_id);
    SCPI_ResultUInt32(context, snapshot.checkpoint.generation);
    SCPI_ResultUInt32(context, snapshot.checkpoint.token);
    SCPI_ResultUInt32(context, snapshot.checkpoint.object_id);
    SCPI_ResultUInt32(context, snapshot.checkpoint.durable_offset);
    SCPI_ResultUInt32(context, snapshot.checkpoint.total_size);
    SCPI_ResultUInt32(context, snapshot.checkpoint.package_crc32);
    SCPI_ResultUInt32(context, snapshot.checkpoint.image_crc32);
    SCPI_ResultUInt32(context, snapshot.checkpoint.durable_crc32);
    SCPI_ResultUInt32(context, snapshot.checkpoint.flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_bcb_health_q(scpi_t *context)
{
    ota_metadata_bcb_health_t health;
    if (!ota_metadata_get_bcb_health(&health)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, health.valid_lane_count);
    SCPI_ResultUInt32(context, health.valid_record_count);
    SCPI_ResultUInt32(context, health.newest_lane_generation);
    SCPI_ResultUInt32(context, health.newest_sequence);
    SCPI_ResultUInt32(context, health.newest_security_counter);
    SCPI_ResultUInt32(context, health.newest_lane);
    SCPI_ResultUInt32(context, health.newest_record_page);
    return SCPI_RES_OK;
}

static bool scpi_ota_stream_source_read(scpi_t *context,
                                        pota_stream_ingress_source_t *source)
{
    uint32_t value;
    if (source == NULL || !scpi_port_read_u32(context, &value) ||
        value >= (uint32_t)POTA_STREAM_INGRESS_SOURCE_COUNT) {
        return false;
    }
    *source = (pota_stream_ingress_source_t)value;
    return true;
}

static bool scpi_ota_stream_source_matches_control_plane(
    pota_stream_ingress_source_t source)
{
#if PROJECT_ENABLE_USB_RUNTIME_SWITCH
    product_config_usb_mode_t mode;
    if (!product_config_get_usb_mode(&mode)) {
        return false;
    }
    return (mode == PRODUCT_CONFIG_USB_MODE_CDC &&
            source == POTA_STREAM_INGRESS_USB_CDC) ||
           (mode == PRODUCT_CONFIG_USB_MODE_USBTMC &&
            source == POTA_STREAM_INGRESS_USBTMC);
#elif PROJECT_ENABLE_USBTMC
    return source == POTA_STREAM_INGRESS_USBTMC;
#else
    return source == POTA_STREAM_INGRESS_USB_CDC;
#endif
}

scpi_result_t scpi_cmd_ota_stream_open(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT) ||
        ota_ao_is_active()) {
        return SCPI_RES_ERR;
    }

    pota_stream_ingress_source_t source;
    const char *data = NULL;
    size_t length = 0u;
    if (!scpi_ota_stream_source_read(context, &source) ||
        !scpi_ota_stream_source_matches_control_plane(source) ||
        SCPI_ParamArbitraryBlock(context, &data, &length, TRUE) != TRUE ||
        length > UINT32_MAX) {
        return SCPI_RES_ERR;
    }

    pota_stream_open_t open;
    if (!pota_stream_open_decode_le((const uint8_t *)data,
                                    (uint32_t)length, &open)) {
        return SCPI_RES_ERR;
    }

    return portable_ota_port_stream_open(source, &open) ==
                   POTA_STREAM_INGRESS_OK
               ? scpi_port_result_ok(context)
               : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_stream_data(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }

    pota_stream_ingress_source_t source;
    uint32_t offset;
    uint32_t crc32;
    const char *data = NULL;
    size_t length = 0u;
    if (!scpi_ota_stream_source_read(context, &source) ||
        !scpi_ota_stream_source_matches_control_plane(source) ||
        !scpi_port_read_u32(context, &offset) ||
        !scpi_port_read_u32(context, &crc32) ||
        SCPI_ParamArbitraryBlock(context, &data, &length, TRUE) != TRUE ||
        length == 0u || length > UINT32_MAX) {
        return SCPI_RES_ERR;
    }

    return portable_ota_port_stream_write(
               source, offset, (const uint8_t *)data, (uint32_t)length,
               true, crc32) == POTA_STREAM_INGRESS_OK
               ? scpi_port_result_ok(context)
               : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_stream_close(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    pota_stream_ingress_source_t source;
    if (!scpi_ota_stream_source_read(context, &source) ||
        !scpi_ota_stream_source_matches_control_plane(source)) {
        return SCPI_RES_ERR;
    }
    return portable_ota_port_stream_close(source) == POTA_STREAM_INGRESS_OK
               ? scpi_port_result_ok(context)
               : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_stream_abort(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    pota_stream_ingress_source_t source;
    if (!scpi_ota_stream_source_read(context, &source) ||
        !scpi_ota_stream_source_matches_control_plane(source)) {
        return SCPI_RES_ERR;
    }
    return portable_ota_port_stream_abort(source) == POTA_STREAM_INGRESS_OK
               ? scpi_port_result_ok(context)
               : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_stream_boot(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    pota_stream_ingress_status_t status;
    if (!portable_ota_port_stream_get_status(&status) ||
        status.state != POTA_STREAM_STATE_READY_TO_REBOOT) {
        return SCPI_RES_ERR;
    }
    drv_watchdog_reboot(50u);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_ota_stream_status_q(scpi_t *context)
{
    pota_stream_ingress_status_t status;
    if (!portable_ota_port_stream_get_status(&status)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, (uint32_t)status.source);
    SCPI_ResultUInt32(context, (uint32_t)status.state);
    SCPI_ResultUInt32(context, status.durable_offset);
    SCPI_ResultUInt32(context, status.stream_token);
    SCPI_ResultUInt32(context, (uint32_t)status.last_result);
    return SCPI_RES_OK;
}

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
scpi_result_t scpi_cmd_ota_inject_copy(scpi_t *context)
{
    return ota_metadata_set_fault_injection(OTA_FAULT_INJECT_COPY_FAIL) ?
               scpi_port_result_ok(context) :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_inject_clear(scpi_t *context)
{
    const bool metadata_cleared =
        ota_metadata_set_fault_injection(OTA_FAULT_INJECT_NONE);
    drv_flash_clear_lockout_fault_injection();
    return metadata_cleared ?
               scpi_port_result_ok(context) :
               SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_inject_copy_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, metadata.fault_injection_flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_inject_lockout(scpi_t *context)
{
    uint32_t flags;
    const uint32_t allowed = DRV_FLASH_LOCKOUT_FAULT_CORE1_NO_ACK |
                             DRV_FLASH_LOCKOUT_FAULT_CORE1_RELEASE_STUCK;
    if (!scpi_port_read_u32(context, &flags) || (flags & ~allowed) != 0u) {
        return SCPI_RES_ERR;
    }
    drv_flash_set_lockout_fault_injection(flags);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_ota_inject_lockout_q(scpi_t *context)
{
    drv_flash_lockout_status_t status;
    drv_flash_get_lockout_status(&status);
    SCPI_ResultUInt32(context, status.fault_injection_flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_inject_metadata_corrupt(scpi_t *context)
{
    uint32_t copy_index;
    if (!scpi_port_read_u32(context, &copy_index)) {
        return SCPI_RES_ERR;
    }

    return ota_metadata_corrupt_copy(copy_index) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_ota_inject_metadata_repair(scpi_t *context)
{
    return ota_metadata_repair_copies() ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}
#endif
