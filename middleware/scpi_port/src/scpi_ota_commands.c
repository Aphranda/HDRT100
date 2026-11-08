#include "scpi_ota_commands.h"
#include <stdio.h>
#include "osal.h"

#include <stddef.h>
#include <stdint.h>

#include "distributed_config.h"
#include "drv_flash.h"
#include "drv_watchdog.h"
#include "flash_transaction.h"
#include "board_identity.h"
#include "ota_ao.h"
#include "ota_metadata.h"
#include "ota_partition.h"
#include "ota_tdma_binding.h"
#include "tdma_maintenance_runtime.h"
#include "ota_journal.h"
#include "product_config.h"
#include "scpi_port_internal.h"
#include "portable_ota_port.h"
#include "resource_arbiter.h"

static bool scpi_ota_request_admission(scpi_t *context)
{
    if (resource_arbiter_ota_admission_active() ||
        resource_arbiter_request_ota_admission()) {
        return true;
    }
    scpi_port_push_exec_error(context, "OTA_ADMISSION_DENIED");
    return false;
}

static uint8_t scpi_ota_local_node(void)
{
    uint32_t slot = 0u;
    if (tdma_maintenance_runtime_get_local_slot(&slot) && slot < 32u) {
        return (uint8_t)slot;
    }
    const uint8_t no = board_identity_get_no();
    return no == 0u ? 0xFFu : (uint8_t)(no - 1u);
}

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
    /* 4th field (append-only): last flash-execute token.  0 = OK, 1 = range or
     * permission rejected, else 200 + 10*error_class + failing_state, which
     * separates a core1 park failure from a raw erase/verify failure. */
    SCPI_ResultUInt32(context, portable_ota_port_flash_stage());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_begin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (!scpi_ota_request_admission(context)) {
        return SCPI_RES_ERR;
    }
    if (portable_ota_port_stream_is_active()) {
        ota_ao_request_admission_release();
        return SCPI_RES_ERR;
    }

    uint32_t size;
    uint32_t crc32;
    if (!scpi_port_read_u32(context, &size) || !scpi_port_read_u32(context, &crc32)) {
        ota_ao_request_admission_release();
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

    if (!ota_ao_post_event(&event)) {
        ota_ao_request_admission_release();
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_ota_package_begin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (!scpi_ota_request_admission(context)) {
        return SCPI_RES_ERR;
    }
    if (portable_ota_port_stream_is_active()) {
        ota_ao_request_admission_release();
        return SCPI_RES_ERR;
    }

    uint32_t size;
    uint32_t crc32;
    if (!scpi_port_read_u32(context, &size) || !scpi_port_read_u32(context, &crc32)) {
        ota_ao_request_admission_release();
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

    if (!ota_ao_post_event(&event)) {
        ota_ao_request_admission_release();
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_ota_data(scpi_t *context)
{
    /* The arbitrary block is part of this command's message: consume it before
     * any rejection.  Returning early with the block still buffered leaves the
     * payload to be parsed as command text, so the next query reads garbage
     * (OTA-HAOFV-013).  Rejections therefore happen after the block is parsed
     * and length-checked. */
    const char *data = NULL;
    size_t length = 0u;
    if (SCPI_ParamArbitraryBlock(context, &data, &length, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (portable_ota_port_stream_is_active()) {
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
    if (type != OTA_EVENT_ABORT &&
        !scpi_ota_request_admission(context)) {
        return SCPI_RES_ERR;
    }

    const ota_event_t event = {
        .type = type,
    };

    if (!ota_ao_post_event(&event)) {
        if (type != OTA_EVENT_ABORT) {
            ota_ao_request_admission_release();
        }
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
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
    /* A stream session reaches READY only after its END already wrote the
     * pending BCB/manifest.  Treat COMM as idempotent in that state instead of
     * rejecting it and making a legacy host wait out its full timeout. */
    if (portable_ota_port_stream_is_active()) {
        pota_stream_ingress_status_t status;
        if (portable_ota_port_stream_get_status(&status) &&
            status.state == POTA_STREAM_STATE_READY_TO_REBOOT) {
            return scpi_port_result_ok(context);
        }
        return SCPI_RES_ERR;
    }
    return scpi_cmd_ota_simple_event_ack(context, OTA_EVENT_COMMIT);
}

scpi_result_t scpi_cmd_ota_confirm(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT)) {
        return SCPI_RES_ERR;
    }
    if (!ota_metadata_confirm_active()) {
        return SCPI_RES_ERR;
    }
    ota_metadata_t metadata;
    if (ota_metadata_load(&metadata)) {
        ota_ao_publish_metadata_snapshot(&metadata);
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_ota_bcb_q(scpi_t *context)
{
    ota_metadata_bcb_health_t health;
    ota_metadata_bcb_wear_t wear;
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
    if (ota_metadata_get_bcb_wear(&wear)) {
        SCPI_ResultUInt32(context, wear.program_page_count);
        SCPI_ResultUInt32(context, wear.erase_lane_count);
    } else {
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, 0u);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_ft_q(scpi_t *context)
{
    flash_transaction_vector_t vector;
    if (!flash_transaction_ao_get_vector(&vector)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, vector.state);
    SCPI_ResultUInt32(context, vector.last_result);
    SCPI_ResultUInt32(context, vector.last_error);
    SCPI_ResultUInt32(context, vector.policy_gate_reason);
    SCPI_ResultUInt32(context, vector.verify_failure_count);
    SCPI_ResultUInt32(context, vector.requested_bytes);
    SCPI_ResultUInt32(context, vector.processed_bytes);
    SCPI_ResultUInt32(context, vector.completion_level);
    /* Append-only ownership diagnostics: the legacy 8 fields above stay
     * stable.  requester/submit_reject distinguish a stale 256-byte BCB or
     * journal transaction from the current erase request; occupied explains
     * 4000+2 submit rejects. */
    SCPI_ResultUInt32(context, vector.requester);
    SCPI_ResultUInt32(context, vector.job_id);
    SCPI_ResultUInt32(context, flash_transaction_submit_reject());
    SCPI_ResultUInt32(context, flash_transaction_first_error());
    SCPI_ResultUInt32(context,
                      flash_transaction_ao_is_occupied() ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_slot_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata_snapshot(&metadata)) {
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
    if (ota_ao_get_metadata_snapshot(&metadata)) {
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
    if (!ota_ao_get_metadata_snapshot(&metadata)) {
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
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    /* v2 has no COPY_TO_ACTIVE runtime branch. A legacy value can still be
     * observed in imported/history metadata, but it is never executable. */
    if (mode == (uint32_t)OTA_BOOT_MODE_COPY_TO_ACTIVE) {
        return "LEGACY_UNSUPPORTED";
    }
    if (mode == (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return "DIRECT_AB";
    }
    return "UNSUPPORTED";
#else
    switch ((ota_boot_mode_t)mode) {
    case OTA_BOOT_MODE_COPY_TO_ACTIVE:
        return "COPY_TO_ACTIVE";
    case OTA_BOOT_MODE_DIRECT_AB:
        return "DIRECT_AB";
    default:
        return "UNKNOWN";
    }
#endif
}

static uint32_t scpi_ota_next_target_slot(const ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return (uint32_t)OTA_SLOT_NONE;
    }

    if (metadata->boot_mode != (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
        return (uint32_t)OTA_SLOT_NONE;
#else
        return (uint32_t)OTA_SLOT_B;
#endif
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
    if (!ota_ao_get_metadata_snapshot(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, scpi_ota_boot_mode_to_string(metadata.boot_mode));
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    SCPI_ResultUInt32(
        context,
        metadata.boot_mode == (uint32_t)OTA_BOOT_MODE_DIRECT_AB
            ? (uint32_t)OTA_BOOT_MODE_DIRECT_AB
            : UINT32_MAX);
#else
    SCPI_ResultUInt32(context, metadata.boot_mode);
#endif
    return SCPI_RES_OK;
}

#if PROJECT_ENABLE_OTA_FAULT_INJECTION && \
    (!defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2)
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
    if (!ota_ao_get_metadata_snapshot(&metadata)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, scpi_ota_next_target_slot(&metadata));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_capability_q(scpi_t *context)
{
    ota_metadata_t metadata;
    if (!ota_ao_get_metadata_snapshot(&metadata)) {
        return SCPI_RES_ERR;
    }

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    SCPI_ResultUInt32(
        context,
        metadata.boot_mode == (uint32_t)OTA_BOOT_MODE_DIRECT_AB
            ? (uint32_t)OTA_BOOT_CAP_DIRECT_AB
            : 0u);
#else
    SCPI_ResultUInt32(context, metadata.boot_capabilities);
#endif
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

scpi_result_t scpi_cmd_ota_bcb_wear_q(scpi_t *context)
{
    ota_metadata_bcb_wear_t wear;
    if (!ota_metadata_get_bcb_wear(&wear)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, wear.program_page_count);
    SCPI_ResultUInt32(context, wear.erase_lane_count);
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

static bool s_stream_bootstrapped;

scpi_result_t scpi_cmd_ota_stream_open(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_OTA_MAINT) ||
        ota_ao_is_active()) {
        return SCPI_RES_ERR;
    }
    if (!scpi_ota_request_admission(context)) {
        return SCPI_RES_ERR;
    }

    pota_stream_ingress_source_t source;
    const char *data = NULL;
    size_t length = 0u;
    if (!scpi_ota_stream_source_read(context, &source) ||
        !scpi_ota_stream_source_matches_control_plane(source) ||
        SCPI_ParamArbitraryBlock(context, &data, &length, TRUE) != TRUE ||
        length > UINT32_MAX) {
        ota_ao_request_admission_release();
        return SCPI_RES_ERR;
    }

    /* A previous relay record must not shadow a new local session. */
    ota_tdma_binding_relay_clear();
    pota_stream_open_t open;
    if (!pota_stream_open_decode_le((const uint8_t *)data,
                                    (uint32_t)length, &open)) {
        ota_ao_request_admission_release();
        return SCPI_RES_ERR;
    }

    if ((open.capability_mask & POTA_STREAM_CAP_RELAY) != 0u &&
        open.relay_node != scpi_ota_local_node()) {
        /* Addressed to another ring node: forward without local flash.  A new
         * OPEN supersedes any stale relay left by an interrupted attempt. */
        drv_watchdog_mark_ota_phase(70u); /* relay branch entered */
        if (ota_tdma_binding_relay_active()) {
            ota_tdma_binding_relay_abort();
        }
        drv_watchdog_mark_ota_phase(71u); /* stale relay cleared */
        ota_tdma_binding_relay_clear();
        if (!ota_tdma_binding_relay_start(&open)) {
            ota_ao_request_admission_release();
            return SCPI_RES_ERR;
        }
        drv_watchdog_mark_ota_phase(
            700000u + ota_tdma_binding_relay_wire_code()); /* relay wire */
        return scpi_port_result_ok(context);
    }
    /* A new OPEN supersedes any stale local stream session (for example one
     * left in RECEIVING by an interrupted host run); otherwise the ingress
     * rejects the new OPEN and the maker never sees a response. */
    if (portable_ota_port_stream_is_active()) {
        (void)portable_ota_port_stream_abort(source);
    }
    pota_stream_ingress_result_t open_result =
        portable_ota_port_stream_open(source, &open);
    if (open_result == POTA_STREAM_INGRESS_SESSION &&
        !s_stream_bootstrapped) {
        /* A factory-blank V2 board has no BCB yet, so ota_ao_init() never
         * initialises the stream surfaces.  The first maintenance update must
         * still be able to open a session: bootstrap the portable platform
         * with factory-default slot metadata (active slot A, no pending). */
        ota_metadata_t factory_metadata;
        memset(&factory_metadata, 0, sizeof(factory_metadata));
        factory_metadata.magic = OTA_METADATA_MAGIC;
        factory_metadata.version = OTA_METADATA_VERSION;
        factory_metadata.active_slot = (uint32_t)OTA_SLOT_A;
        factory_metadata.pending_slot = (uint32_t)OTA_SLOT_NONE;
        factory_metadata.confirmed_slot = (uint32_t)OTA_SLOT_A;
        factory_metadata.slot_a_size = ota_partition_slot_size(OTA_SLOT_A);
        factory_metadata.slot_b_size = ota_partition_slot_size(OTA_SLOT_B);
        if (portable_ota_port_stream_init(&factory_metadata)) {
            s_stream_bootstrapped = true;
            /* A factory-blank V2 board has no BCB at all, so a later COMMIT
             * would fail with METADATA.  Provision the factory default record
             * once, before the first stream session. */
            (void)ota_metadata_store(&factory_metadata);
            open_result = portable_ota_port_stream_open(source, &open);
        }
    }
    if (open_result == POTA_STREAM_INGRESS_OK) {
        /* Bounded maintenance window: finalize (manifest hash/BCB) can take
         * longer than the 3 s supervisor budget.  The host workflow enforces
         * per-step deadlines, and every terminal path restores the budget. */
        drv_watchdog_enable(120000u);
    }
    if (open_result != POTA_STREAM_INGRESS_OK) {
        static char open_error[32];
        (void)snprintf(open_error, sizeof(open_error),
                       "STREAM_OPEN_%u", (unsigned)open_result);
        scpi_port_push_exec_error(context, open_error);
        ota_ao_request_admission_release();
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
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

    if (ota_tdma_binding_relay_active()) {
        return ota_tdma_binding_relay_feed(offset, (const uint8_t *)data,
                                           (uint32_t)length)
                   ? scpi_port_result_ok(context)
                   : SCPI_RES_ERR;
    }
    /* Data is copied into the EventBus fast-block pool and written by OtaAO.
     * The SCPI/USB task only enqueues; it must not call the flash path
     * synchronously (HAOFV single-owner / non-blocking budget). */
    drv_watchdog_enable(30000u);
    const uint32_t core_state = portable_ota_port_stream_core_state();
    if (core_state == (uint32_t)POTA_STATE_CHECK_PERMISSION ||
        core_state == (uint32_t)POTA_STATE_ERASE_SLOT) {
        SCPI_ResultCharacters(context, "BUSY", 4u);
        return SCPI_RES_OK;
    }
    if (core_state == (uint32_t)POTA_STATE_FAILED ||
        core_state == (uint32_t)POTA_STATE_ABORTED ||
        !portable_ota_port_stream_is_active()) {
        return SCPI_RES_ERR;
    }
    /* The first block (package header, or first raw block) is what schedules
     * the inactive-slot erase.  A host that pipelines the next four blocks can
     * reach this handler before OtaAO has consumed that first block; at that
     * instant durable_offset is still zero and the core still says RECEIVING.
     * Returning QUEUED here would let the next DATA overtake the erase
     * transition and turn back-pressure into INVALID_STATE (5002).  Keep the
     * first block single-file: require durable_offset to advance before any
     * non-zero offset is accepted. */
    if (offset > 0u) {
        pota_stream_ingress_status_t stream_status;
        if (portable_ota_port_stream_get_status(&stream_status) &&
            stream_status.durable_offset == 0u) {
            SCPI_ResultCharacters(context, "BUSY", 4u);
            return SCPI_RES_OK;
        }
    }
    /* OtaAO recomputes the CRC from the queued copy; the SCPI-side value was
     * already parsed for protocol compatibility. */
    (void)crc32;
    const ota_event_t event = {
        .type = OTA_EVENT_STREAM_DATA,
        .payload.data = {
            .data = (const uint8_t *)data,
            .length = (uint32_t)length,
            .block_index = offset,
        },
    };
    if (!ota_ao_post_event(&event)) {
        SCPI_ResultCharacters(context, "BUSY", 4u);
        return SCPI_RES_OK;
    }
    drv_watchdog_mark_ota_phase(21u); /* stream block queued */
    SCPI_ResultCharacters(context, "QUEUED", 6u);
    return SCPI_RES_OK;
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
    if (ota_tdma_binding_relay_active() ||
        ota_tdma_binding_relay_done()) {
        /* The relay owns the target-side finalisation, but this board still has
         * to survive the same asynchronous END window; keep the widened
         * supervisor budget until the relay reports a terminal state. */
        drv_watchdog_enable(30000u);
        drv_watchdog_mark_ota_phase(26u);
        return scpi_port_result_ok(context);
    }
    /* Finalization (manifest hash + signature + BCB commit) legitimately takes
     * longer than the 3 s supervisor budget; widen it for the close path. */
    drv_watchdog_enable(30000u);
    if (!portable_ota_port_stream_is_active()) {
        return SCPI_RES_ERR;
    }
    const ota_event_t event = {
        .type = OTA_EVENT_STREAM_CLOSE,
        .payload.value = (uint32_t)source,
    };
    if (!ota_ao_post_event(&event)) {
        return SCPI_RES_ERR;
    }
    drv_watchdog_mark_ota_phase(24u); /* close queued */
    /* CLOSE is queued; OtaAO owns the session and runs END -> READY.  The host
     * polls STREAM:STAT? and releases the widened watchdog on the terminal
     * state. */
    SCPI_ResultCharacters(context, "QUEUED", 6u);
    return SCPI_RES_OK;
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
    if (ota_tdma_binding_relay_active() || ota_tdma_binding_relay_done()) {
        ota_tdma_binding_relay_abort();
        ota_ao_request_admission_release();
        return scpi_port_result_ok(context);
    }
    const pota_stream_ingress_result_t result =
        portable_ota_port_stream_abort(source);
    drv_watchdog_enable(PROJECT_WATCHDOG_TIMEOUT_MS);
    if (result == POTA_STREAM_INGRESS_OK) {
        ota_ao_request_admission_release();
    }
    return result == POTA_STREAM_INGRESS_OK ? scpi_port_result_ok(context)
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
    ota_ao_request_admission_release();
    drv_watchdog_reboot(50u);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_ota_stream_status_q(scpi_t *context)
{
    if (ota_tdma_binding_relay_active() || ota_tdma_binding_relay_done()) {
        SCPI_ResultUInt32(context, (uint32_t)POTA_STREAM_INGRESS_TDMA);
        SCPI_ResultUInt32(context,
                          ota_tdma_binding_relay_done()
                              ? (uint32_t)POTA_STREAM_STATE_READY_TO_REBOOT
                              : (uint32_t)POTA_STREAM_STATE_RECEIVING);
        SCPI_ResultUInt32(context, ota_tdma_binding_relay_durable());
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, ota_tdma_binding_last_result());
        return SCPI_RES_OK;
    }
    if (ota_tdma_binding_relay_failed()) {
        /* The relay ended in failure: report it as FAILED so the host stops
         * waiting for a durable offset that can never arrive (the target's
         * ABORT otherwise only surfaces as the host's own timeout). */
        SCPI_ResultUInt32(context, (uint32_t)POTA_STREAM_INGRESS_TDMA);
        SCPI_ResultUInt32(context, (uint32_t)POTA_STREAM_STATE_FAILED);
        SCPI_ResultUInt32(context, ota_tdma_binding_relay_durable());
        SCPI_ResultUInt32(context, 0u);
        SCPI_ResultUInt32(context, ota_tdma_binding_last_result());
        return SCPI_RES_OK;
    }
    pota_stream_ingress_status_t status;
    if (!portable_ota_port_stream_get_status(&status)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, (uint32_t)status.source);
    SCPI_ResultUInt32(context, (uint32_t)status.state);
    SCPI_ResultUInt32(context, status.durable_offset);
    SCPI_ResultUInt32(context, status.stream_token);
    SCPI_ResultUInt32(context, (uint32_t)status.last_result);
    /* The close path widens the supervisor window so the asynchronous END
     * transaction can finish; release it here once the session reaches a
     * terminal state so normal 3 s supervision resumes. */
    if (status.state == POTA_STREAM_STATE_READY_TO_REBOOT ||
        status.state == POTA_STREAM_STATE_FAILED ||
        status.state == POTA_STREAM_STATE_ABORTED ||
        status.state == POTA_STREAM_STATE_IDLE) {
        drv_watchdog_enable(PROJECT_WATCHDOG_TIMEOUT_MS);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_tdma_diag_q(scpi_t *context)
{
    uint32_t diag[26] = {0};
    ota_tdma_binding_diag(diag);
    for (uint32_t i = 0; i < 26u; ++i) {
        SCPI_ResultUInt32(context, diag[i]);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_stream_capability_q(scpi_t *context)
{
    /* Report the accepted DATA payload, stream capability mask and the
     * deployment map compiled into this firmware. FLASH:MAP? describes the
     * catalog candidate and therefore cannot select a live OTA transport. */
    SCPI_ResultUInt32(context, POTA_MAX_DATA_BLOCK_SIZE);
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    SCPI_ResultUInt32(context,
                      POTA_STREAM_CAP_INACTIVE_WRITE |
                          POTA_STREAM_CAP_DURABLE_ACK |
                          POTA_STREAM_CAP_RELAY);
    SCPI_ResultUInt32(context, 2u);
#else
    /* v1 compatibility builds can still accelerate legacy OTA DATA blocks,
     * but they do not have durable stream checkpoints. */
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
#endif
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_stream_init_q(scpi_t *context)
{
    /* Stream-init diagnostics: stage tells which sub-step failed, the two
     * results are the raw pota_stream_result_t/ingress codes. */
    SCPI_ResultUInt32(context, portable_ota_port_stream_init_stage());
    SCPI_ResultUInt32(context, portable_ota_port_stream_last_session_result());
    SCPI_ResultUInt32(context, portable_ota_port_stream_last_service_result());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_stream_erase_q(scpi_t *context)
{
    uint32_t core_state = 0u;
    uint32_t erased_bytes = 0u;
    uint32_t erase_bytes = 0u;
    if (!portable_ota_port_stream_erase_progress(
            &core_state, &erased_bytes, &erase_bytes)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, core_state);
    SCPI_ResultUInt32(context, erased_bytes);
    SCPI_ResultUInt32(context, erase_bytes);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_ota_perf_q(scpi_t *context)
{
    uint32_t last_us[PORTABLE_OTA_PORT_PERF_COUNT] = {0};
    uint32_t max_us[PORTABLE_OTA_PORT_PERF_COUNT] = {0};
    uint32_t count[PORTABLE_OTA_PORT_PERF_COUNT] = {0};
    portable_ota_port_perf_get(last_us, max_us, count);
    for (uint32_t index = 0u; index < PORTABLE_OTA_PORT_PERF_COUNT; ++index) {
        SCPI_ResultUInt32(context, last_us[index]);
        SCPI_ResultUInt32(context, max_us[index]);
        SCPI_ResultUInt32(context, count[index]);
    }
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
    if (!ota_ao_get_metadata_snapshot(&metadata)) {
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
