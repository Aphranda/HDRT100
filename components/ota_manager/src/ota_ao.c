#include "ota_ao.h"

#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "event_bus.h"
#include "flash_transaction.h"
#include "ota_ao_private.h"
#include "ota_error.h"
#include "ota_fb.h"
#include "ota_metadata.h"
#include "ota_partition.h"
#include "ota_progress_watch.h"
#include "portable_ota_port.h"
#include "resource_arbiter.h"

#ifndef OTA_SESSION_NO_PROGRESS_TIMEOUT_MS
#define OTA_SESSION_NO_PROGRESS_TIMEOUT_MS OTA_PROGRESS_WATCH_DEFAULT_TIMEOUT_MS
#endif

/* The TDMA bridge only produces one write per complete block, and a block is
 * several maintenance frames.  Until the first block is acknowledged the AO
 * sees no progress by design, so give the first block a budget that covers a
 * lossy maintenance window instead of declaring NO_PROGRESS after 10 s. */
#ifndef OTA_FIRST_BLOCK_TIMEOUT_MS
#define OTA_FIRST_BLOCK_TIMEOUT_MS 40000u
#endif

static struct ota_ao_context s_ota_context;
static bool s_boot_confirm_done;
static bool s_release_admission_pending;

/* Never drop the OTA admission while a FlashTransaction still owns the
 * transaction slot.  Terminal journal/BCB appends are OTA requesters too; if
 * the lease disappears first they fail with MODE and leave the owner occupied,
 * so the next OTA erase is rejected with submit_reject=2 (4002). */
void ota_ao_request_admission_release(void)
{
    if (flash_transaction_ao_is_occupied()) {
        s_release_admission_pending = true;
        return;
    }
    resource_arbiter_release_ota_admission();
}

static void ota_ao_service_pending_admission_release(void)
{
    if (!s_release_admission_pending) {
        return;
    }
    if (portable_ota_port_stream_is_active()) {
        /* A new session already reused the lease; do not release it. */
        s_release_admission_pending = false;
        return;
    }
    if (flash_transaction_ao_is_occupied()) {
        /* A terminal transaction can still own the slot if its last service
         * step was skipped.  Drain only terminal states here; an active
         * transaction is not ours to abort from the admission-release path. */
        flash_transaction_vector_t vector;
        if (flash_transaction_ao_get_vector(&vector) &&
            (vector.state == FLASH_TRANSACTION_STATE_FAILED ||
             vector.state == FLASH_TRANSACTION_STATE_ABORTED ||
             vector.state == FLASH_TRANSACTION_STATE_COMPLETE)) {
            flash_transaction_ao_service();
        }
        if (flash_transaction_ao_is_occupied()) {
            return;
        }
    }
    resource_arbiter_release_ota_admission();
    s_release_admission_pending = false;
}
static ota_vector_t s_vector_snapshot[2];
static ota_metadata_t s_metadata_snapshot[2];
static uint32_t s_vector_active_index;
static uint32_t s_metadata_active_index;
static bool s_metadata_snapshot_valid;
static ota_progress_watch_t s_progress_watch;

static void ota_ao_fail_no_progress(struct ota_ao_context *context)
{
    /* Stop the in-flight transport before publishing FAILED: the next BEGIN is
     * only accepted if the session/Flash transaction is already released. */
    if (portable_ota_port_stream_is_active()) {
        pota_stream_ingress_status_t status;
        if (portable_ota_port_stream_get_status(&status)) {
            (void)portable_ota_port_stream_abort(status.source);
        }
    } else {
        (void)portable_ota_port_core_abort(&context->vector);
    }
    ota_ao_request_admission_release();
    context->vector.error_code = (uint32_t)OTA_ERR_NO_PROGRESS;
    context->vector.last_result = (uint32_t)OTA_RESULT_FAILED;
    context->vector.state = (uint32_t)OTA_STATE_FAILED;
    LOG_INFO("ota", "OTA session failed: no host progress");
}

void ota_ao_publish_vector(const ota_ao_context_t *context)
{
    if (context == NULL) {
        return;
    }
    const uint32_t next =
        (__atomic_load_n(&s_vector_active_index, __ATOMIC_RELAXED) + 1u) & 1u;
    s_vector_snapshot[next] = context->vector;
    __atomic_store_n(&s_vector_active_index, next, __ATOMIC_RELEASE);
}

void ota_ao_publish_metadata(ota_ao_context_t *context,
                             const ota_metadata_t *metadata)
{
    if (context == NULL || metadata == NULL) {
        return;
    }
    const uint32_t next =
        (__atomic_load_n(&s_metadata_active_index, __ATOMIC_RELAXED) + 1u) & 1u;
    s_metadata_snapshot[next] = *metadata;
    context->metadata_snapshot = *metadata;
    context->metadata_snapshot_valid = true;
    s_metadata_snapshot_valid = true;
    __atomic_store_n(&s_metadata_active_index, next, __ATOMIC_RELEASE);
}

void ota_ao_publish_metadata_snapshot(const ota_metadata_t *metadata)
{
    ota_ao_publish_metadata(&s_ota_context, metadata);
}

static ota_slot_t ota_ao_target_slot_from_metadata(const ota_metadata_t *metadata)
{
    if (metadata == NULL ||
        metadata->boot_mode != (uint32_t)OTA_BOOT_MODE_DIRECT_AB) {
        return OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_A) {
        return OTA_SLOT_B;
    }

    if (metadata->active_slot == (uint32_t)OTA_SLOT_B) {
        return OTA_SLOT_A;
    }

    return OTA_SLOT_B;
}

const char *ota_state_to_string(ota_state_t state)
{
    return portable_ota_port_state_to_string(state);
}

const char *ota_error_to_string(uint32_t error_code)
{
    return portable_ota_port_error_to_string(error_code);
}

const char *ota_result_to_string(ota_result_t result)
{
    return portable_ota_port_result_to_string(result);
}

bool ota_ao_init(void)
{
    memset(&s_ota_context, 0, sizeof(s_ota_context));
    memset(s_vector_snapshot, 0, sizeof(s_vector_snapshot));
    memset(s_metadata_snapshot, 0, sizeof(s_metadata_snapshot));
    s_vector_active_index = 0u;
    s_metadata_active_index = 0u;
    s_metadata_snapshot_valid = false;

    s_ota_context.vector.timestamp_ms = board_uptime_ms();
    s_ota_context.vector.state = (uint32_t)OTA_STATE_IDLE;
    s_ota_context.vector.target_slot = (uint32_t)OTA_SLOT_B;
    s_ota_context.vector.error_code = (uint32_t)OTA_ERR_NONE;
    s_ota_context.target_slot = OTA_SLOT_B;
    ota_progress_watch_reset(&s_progress_watch, board_uptime_ms());

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
    /* Completion durability must be established even when BCB metadata is
     * absent/corrupt and the stream session cannot be opened yet. */
    if (!portable_ota_port_durable_init()) {
        return false;
    }
#endif

    ota_metadata_t metadata;
    if (ota_metadata_load(&metadata)) {
        ota_ao_publish_metadata(&s_ota_context, &metadata);
        s_ota_context.target_slot = ota_ao_target_slot_from_metadata(&metadata);
        s_ota_context.vector.target_slot = metadata.pending_slot != (uint32_t)OTA_SLOT_NONE ?
                                               metadata.pending_slot :
                                               (uint32_t)s_ota_context.target_slot;
        s_ota_context.vector.expected_size = metadata.slot_b_size;
        s_ota_context.vector.crc32_expected = metadata.slot_b_crc32;
        s_ota_context.vector.boot_flags_summary = metadata.last_boot_result;
        const bool stream_initialized =
            portable_ota_port_stream_init(&metadata);
#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2
        if (!stream_initialized) {
            return false;
        }
#else
        (void)stream_initialized;
#endif
    }

    s_ota_context.target_offset = ota_partition_slot_offset(s_ota_context.target_slot);
    s_ota_context.target_size = ota_partition_slot_size(s_ota_context.target_slot);
    s_ota_context.target_run_offset = OTA_DEFAULT_APP_RUN_OFFSET;
    ota_ao_publish_vector(&s_ota_context);

    LOG_INFO("ota", "OTA AO initialized");
    return true;
}

static ota_ao_data_done_fn s_data_done_hook;
static void *s_data_done_context;

void ota_ao_set_data_done_hook(ota_ao_data_done_fn hook, void *context)
{
    s_data_done_hook = hook;
    s_data_done_context = context;
}

bool ota_ao_post_event(const ota_event_t *event)
{
    if (!event_bus_post_ota_event(event)) {
        s_ota_context.vector.error_code = (uint32_t)OTA_ERR_QUEUE_FULL;
        ota_ao_publish_vector(&s_ota_context);
        return false;
    }

    return true;
}

void ota_ao_service(uint32_t budget_us)
{
    if (budget_us == 0u) {
        return;
    }

    ota_ao_service_pending_admission_release();

    /* OTA-HAOFV-013: confirm the running slot once the application has been up
     * long enough to count as healthy.  Without this the confirmed slot stays
     * behind the active one, so the next update targets the only rollback
     * target and any interrupted finalise leaves no bootable slot. */
    if (!s_boot_confirm_done && board_uptime_ms() >= 1000u &&
        !portable_ota_port_stream_is_active() &&
        s_ota_context.vector.state == (uint32_t)OTA_STATE_IDLE) {
        ota_metadata_t boot_metadata;
        if (ota_metadata_load(&boot_metadata) &&
            boot_metadata.pending_slot == (uint32_t)OTA_SLOT_NONE &&
            boot_metadata.confirmed_slot != boot_metadata.active_slot &&
            boot_metadata.last_boot_result <=
                (uint32_t)OTA_BOOT_RESULT_APPLIED) {
            /* Only latch once the metadata is durable; a transient load/store
             * failure must not leave the confirmed slot behind the active one,
             * because that makes the next update overwrite the only rollback
             * image. */
            const bool confirmed = ota_metadata_confirm_active();
            if (confirmed) {
                ota_metadata_t confirmed_metadata;
                if (ota_metadata_load(&confirmed_metadata)) {
                    ota_ao_publish_metadata(&s_ota_context,
                                            &confirmed_metadata);
                }
            }
            s_boot_confirm_done = confirmed;
        } else if (boot_metadata.confirmed_slot == boot_metadata.active_slot) {
            s_boot_confirm_done = true;
        }
    }

    s_ota_context.vector.timestamp_ms = board_uptime_ms();
    ota_ao_publish_vector(&s_ota_context);

    /* A host that dies mid-transfer must not hold the maintenance admission
     * forever (OTA-HAOFV-012).  Watch the published vector before any routing
     * so both the legacy event path and the stream path are covered. */
    const bool awaiting_first_block =
        s_ota_context.vector.state == (uint32_t)OTA_STATE_RECEIVING &&
        s_ota_context.vector.received_size == 0u;
    if (ota_progress_watch_step(&s_progress_watch,
                                s_ota_context.vector.timestamp_ms,
                                s_ota_context.vector.state,
                                s_ota_context.vector.received_size,
                                s_ota_context.vector.programmed_size,
                                awaiting_first_block
                                    ? OTA_FIRST_BLOCK_TIMEOUT_MS
                                    : OTA_SESSION_NO_PROGRESS_TIMEOUT_MS) ==
        OTA_PROGRESS_WATCH_TIMEOUT) {
        ota_ao_fail_no_progress(&s_ota_context);
        ota_ao_publish_vector(&s_ota_context);
        return;
    }

    /* A newly opened stream is still in OPEN until the bounded service step
     * publishes RECEIVING.  Do not consume the queued header before that
     * transition: a write in OPEN returns INVALID_STATE and the header would
     * be lost, leaving ERASE? stuck at RECEIVING/0 on the host. */
    if (portable_ota_port_stream_is_active()) {
        pota_stream_ingress_status_t open_status;
        if (portable_ota_port_stream_get_status(&open_status) &&
            open_status.state == POTA_STREAM_STATE_OPEN) {
            (void)portable_ota_port_stream_service(budget_us);
            return;
        }
    }

    /* Service bounded permission/erase work before consuming DATA.  SCPI
     * producers can queue a DATA block while the inactive slot is still
     * erasing; consuming it early converts normal back-pressure into
     * INVALID_STATE and eventually QUEUE_FULL. */
    const ota_event_t tick = {
        .type = OTA_EVENT_TICK,
    };
    if (s_ota_context.vector.state == (uint32_t)OTA_STATE_CHECK_PERMISSION ||
        s_ota_context.vector.state == (uint32_t)OTA_STATE_ERASE_SLOT) {
        ota_fb_execute(&s_ota_context, &tick);
        return;
    }

    /* The package header starts the inactive-slot erase.  A pipelined DATA
     * event may already be queued behind it; advance the erase first and keep
     * that event queued instead of failing it with INVALID_STATE. */
    if (portable_ota_port_stream_is_active()) {
        const uint32_t stream_core_state = portable_ota_port_stream_core_state();
        if (stream_core_state == (uint32_t)POTA_STATE_CHECK_PERMISSION ||
            stream_core_state == (uint32_t)POTA_STATE_ERASE_SLOT) {
            (void)portable_ota_port_stream_service(budget_us);
            return;
        }
    }

    ota_event_t event;
    if (event_bus_try_recv_ota_event(&event)) {
        if (event.type == OTA_EVENT_STREAM_DATA) {
            /* Single writer: the USB/SCPI task only queued this block; OtaAO
             * performs the stream write and publishes the durable offset.
             * A queued block that outlived a failed/aborted/completed session
             * must not reach the core write path: it would overwrite the real
             * service failure with INVALID_STATE (5002) and touch flash after
             * the session is terminal. */
            pota_stream_ingress_status_t stream_status;
            const bool have_stream_status =
                portable_ota_port_stream_get_status(&stream_status);
            if (!have_stream_status ||
                stream_status.state != POTA_STREAM_STATE_RECEIVING) {
                event_bus_complete_ota_event(&event);
                if (have_stream_status &&
                    (stream_status.state == POTA_STREAM_STATE_FAILED ||
                     stream_status.state == POTA_STREAM_STATE_ABORTED)) {
                    ota_ao_request_admission_release();
                }
                return;
            }
            const uint32_t stream_crc = pota_crc32_compute(
                event.payload.data.data, event.payload.data.length);
            const pota_stream_ingress_result_t stream_result =
                portable_ota_port_stream_write(
                    POTA_STREAM_INGRESS_USB_CDC,
                    event.payload.data.block_index,
                    event.payload.data.data,
                    event.payload.data.length, true, stream_crc);
            event_bus_complete_ota_event(&event);
            if (stream_result != POTA_STREAM_INGRESS_OK) {
                pota_stream_ingress_status_t status;
                if (portable_ota_port_stream_get_status(&status) &&
                    (status.state == POTA_STREAM_STATE_FAILED ||
                     status.state == POTA_STREAM_STATE_ABORTED)) {
                    ota_ao_request_admission_release();
                }
            }
            return;
        }
        if (event.type == OTA_EVENT_STREAM_CLOSE) {
            const pota_stream_ingress_result_t close_result =
                portable_ota_port_stream_close(
                    (pota_stream_ingress_source_t)event.payload.value);
            event_bus_complete_ota_event(&event);
            if (close_result != POTA_STREAM_INGRESS_OK) {
                pota_stream_ingress_status_t status;
                if (portable_ota_port_stream_get_status(&status) &&
                    (status.state == POTA_STREAM_STATE_FAILED ||
                     status.state == POTA_STREAM_STATE_ABORTED)) {
                    ota_ao_request_admission_release();
                }
            }
            return;
        }
        const bool was_data = event.type == OTA_EVENT_DATA_BLOCK;
        ota_fb_execute(&s_ota_context, &event);
        event_bus_complete_ota_event(&event);
        if (was_data && s_data_done_hook != NULL) {
            s_data_done_hook(s_data_done_context);
        }
        return;
    }

    /* Stream ingress shares the same bounded AO service cadence as the legacy
     * event path.  Only one path advances a session per tick; the stream port
     * itself owns the FlashTransaction intent and readback boundary. */
    if (portable_ota_port_stream_is_active()) {
        pota_stream_ingress_status_t status;
        if (portable_ota_port_stream_get_status(&status) &&
            (status.state == POTA_STREAM_STATE_OPEN ||
             status.state == POTA_STREAM_STATE_RECEIVING ||
             status.state == POTA_STREAM_STATE_ENDING)) {
            const pota_stream_ingress_result_t stream_result =
                portable_ota_port_stream_service(budget_us);
            /* END is intentionally serviced after CLOSE.  If a bounded
             * FlashTransaction step fails asynchronously, release the
             * maintenance lease here; otherwise the board would remain
             * permanently admitted to OTA with no recoverable session. */
            if (stream_result != POTA_STREAM_INGRESS_OK) {
                pota_stream_ingress_status_t failed_status;
                if (portable_ota_port_stream_get_status(&failed_status) &&
                    (failed_status.state == POTA_STREAM_STATE_FAILED ||
                     failed_status.state == POTA_STREAM_STATE_ABORTED)) {
                    ota_ao_request_admission_release();
                }
            }
        }
        return;
    }

    ota_fb_execute(&s_ota_context, &tick);
}

void ota_ao_get_vector(ota_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }

    const uint32_t active =
        __atomic_load_n(&s_vector_active_index, __ATOMIC_ACQUIRE) & 1u;
    *vector = s_vector_snapshot[active];
}

bool ota_ao_get_metadata_snapshot(ota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }
    if (!s_metadata_snapshot_valid) {
        return false;
    }
    const uint32_t active =
        __atomic_load_n(&s_metadata_active_index, __ATOMIC_ACQUIRE) & 1u;
    *metadata = s_metadata_snapshot[active];
    return true;
}

static bool s_tdma_maintenance_required;

void ota_ao_set_tdma_maintenance_required(bool required)
{
    s_tdma_maintenance_required = required;
}

bool ota_ao_tdma_maintenance_allowed(void)
{
    return portable_ota_port_stream_is_active() || s_tdma_maintenance_required;
}

bool ota_ao_is_active(void)
{
    const uint32_t state = s_ota_context.vector.state;
    return state >= (uint32_t)OTA_STATE_CHECK_PERMISSION &&
           state <= (uint32_t)OTA_STATE_READY_TO_REBOOT;
}
