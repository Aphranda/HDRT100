#include "ota_tdma_v2_bridge_app.h"

#include <string.h>

#include "ota_ao.h"
#include "ota_event.h"
#include "ota_tdma_v2_adapter.h"

static ota_tdma_v2_adapter_t s_adapter;
static ota_tdma_v2_send_frame_fn s_send_frame;
static bool s_initialized;
/* The AO failure ABORT is sent once per session by the service path. */
static bool s_failed_abort_sent;

static bool bridge_app_send(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    return s_send_frame != NULL && length != 0u && s_send_frame(frame, length);
}

/* The bridge stages one flash-sized block and hands its address here; the
 * block stays valid until ota_tdma_v2_bridge_app_release(), which the OTA AO
 * calls after the event has been consumed. */
static bool bridge_app_sink(void *context, ota_tdma_bridge_op_t operation,
                            const ota_tdma_bridge_begin_t *begin,
                            const uint8_t *data, uint32_t length)
{
    (void)context;
    ota_event_t event;
    memset(&event, 0, sizeof(event));
    switch (operation) {
    case OTA_TDMA_BRIDGE_OP_BEGIN: {
        if (begin == NULL) {
            return false;
        }
        event.type = OTA_EVENT_BEGIN;
        event.payload.begin.size = begin->size;
        event.payload.begin.crc32 = begin->crc32;
        event.payload.begin.flags =
            begin->package_mode ? OTA_BEGIN_FLAG_PACKAGE : 0u;
        break;
    }
    case OTA_TDMA_BRIDGE_OP_DATA:
        if (data == NULL || length == 0u) {
            return false;
        }
        event.type = OTA_EVENT_DATA_BLOCK;
        event.payload.data.data = data;
        event.payload.data.length = length;
        break;
    case OTA_TDMA_BRIDGE_OP_END:
        event.type = OTA_EVENT_END;
        break;
    case OTA_TDMA_BRIDGE_OP_ABORT:
        event.type = OTA_EVENT_ABORT;
        break;
    default:
        return false;
    }
    return ota_ao_post_event(&event);
}

static void bridge_app_data_done(void *context)
{
    (void)context;
    ota_tdma_v2_bridge_app_release();
}

static uint32_t s_last_vector_state;
static uint32_t s_last_ready;

/* DATA is only accepted once the existing AB pipeline has finished the
 * permission/erase steps and is receiving image bytes. */
static bool bridge_app_ready(void *context)
{
    (void)context;
    ota_vector_t vector;
    ota_ao_get_vector(&vector);
    s_last_vector_state = vector.state;
    s_last_ready = vector.state == (uint32_t)OTA_STATE_RECEIVING ? 1u : 0u;
    return s_last_ready != 0u;
}

bool ota_tdma_v2_bridge_app_init(ota_tdma_v2_send_frame_fn send)
{
    if (send == NULL) {
        return false;
    }
    s_send_frame = send;
    s_initialized = ota_tdma_v2_adapter_init(&s_adapter, bridge_app_sink, NULL,
                                             bridge_app_send, NULL,
                                             bridge_app_ready, NULL);
    if (s_initialized) {
        ota_ao_set_data_done_hook(bridge_app_data_done, NULL);
        /* The maintenance ring must keep running while the AB session is
         * active.  This used to be enabled unconditionally at init, which made
         * the AO advertise a maintenance dependency for every session —
         * including local USB OTA with no ring running (M4 A/B: erase stalled
         * in ERASE_SLOT even with the ring stopped).  It is now owned by the
         * session: set on the first bridged frame, cleared on release. */
    }
    return s_initialized;
}

bool ota_tdma_v2_bridge_app_frame(const uint8_t *payload, size_t size)
{
    const bool ok = s_initialized && payload != NULL && size != 0u &&
                    ota_tdma_v2_adapter_frame(&s_adapter, payload, size);
    if (s_initialized && ota_tdma_v2_bridge_app_active()) {
        ota_ao_set_tdma_maintenance_required(true);
    }
    return ok;
}

void ota_tdma_v2_bridge_app_release(void)
{
    if (!s_initialized) {
        return;
    }
    (void)ota_tdma_v2_adapter_release(&s_adapter);
    if (!ota_tdma_v2_bridge_app_active()) {
        ota_ao_set_tdma_maintenance_required(false);
    }
    ota_vector_t vector;
    ota_ao_get_vector(&vector);
    if (vector.state != (uint32_t)OTA_STATE_FAILED) {
        return;
    }
    /* The AB pipeline died (BAD_HEADER, NO_PROGRESS, flash error...).  Tell the
     * source to stop so it does not burn its retry budget on a dead session.
     *
     * The local adapter is deliberately NOT aborted here: aborting it posts
     * OTA_EVENT_ABORT back into the AO and overwrites vector.error_code with
     * ABORTED, which is exactly what hid the real reason of the first block
     * failure for several rounds.  The next OPEN re-initialises the bridge, so
     * leaving it in RECEIVING costs nothing. */
    if (s_send_frame != NULL) {
        uint8_t frame[OTA_TDMA_FRAME_MAX];
        const size_t length = ota_tdma_encode_abort(
            frame, sizeof(frame), (uint8_t)vector.error_code);
        if (length != 0u) {
            (void)s_send_frame(frame, length);
        }
    }
}

bool ota_tdma_v2_bridge_app_active(void)
{
    return s_initialized &&
           ota_tdma_bridge_state(&s_adapter.bridge) ==
               OTA_TDMA_BRIDGE_RECEIVING;
}

/* Periodic failure watch.  The AB pipeline can die (e.g. NO_PROGRESS) without
 * another block ever completing, so the per-block data-done hook never runs.
 * Without this the source keeps retrying a session whose target is dead and
 * the host only ever sees its own timeout. */
bool ota_tdma_v2_bridge_app_service(void)
{
    if (!s_initialized || !ota_tdma_v2_bridge_app_active()) {
        return false;
    }
    if (s_failed_abort_sent) {
        return false;
    }
    ota_vector_t vector;
    ota_ao_get_vector(&vector);
    s_last_vector_state = vector.state;
    if (vector.state != (uint32_t)OTA_STATE_FAILED) {
        return false;
    }
    if (s_send_frame != NULL) {
        uint8_t frame[OTA_TDMA_FRAME_MAX];
        const size_t length = ota_tdma_encode_abort(
            frame, sizeof(frame), (uint8_t)vector.error_code);
        if (length != 0u && !s_send_frame(frame, length)) {
            /* Maintenance TX queue busy: keep the bridge session so the next
             * service tick retries the ABORT instead of losing it. */
            return false;
        }
    }
    /* No adapter_abort(): it would post OTA_EVENT_ABORT into the AO and replace
     * the error_code with ABORTED, hiding the real failure.  The bridge is
     * reset by the next OPEN, and `sent` keeps this from re-sending ABORTs. */
    s_failed_abort_sent = true;
    return true;
}

void ota_tdma_v2_bridge_app_stats(uint32_t out[9])
{
    if (out == NULL) {
        return;
    }
    out[0] = s_initialized ? s_adapter.bridge.stage_offset : 0u;
    out[1] = s_initialized ? s_adapter.bridge.stage_length : 0u;
    out[2] = s_initialized ? s_adapter.bridge.received_size : 0u;
    out[3] = s_initialized && s_adapter.bridge.block_in_flight ? 1u : 0u;
    out[4] = s_initialized ? s_adapter.last_type : 0u;
    out[5] = s_initialized ? s_adapter.last_offset : 0u;
    out[6] = s_initialized ? s_adapter.last_length : 0u;
    out[7] = s_last_vector_state;
    out[8] = s_last_ready;
}
