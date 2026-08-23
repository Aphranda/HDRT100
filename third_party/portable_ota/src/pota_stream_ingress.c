#include "pota_stream_ingress.h"

#include <string.h>

#include "pota_types.h"

static bool source_valid(pota_stream_ingress_source_t source)
{
    return (uint32_t)source < (uint32_t)POTA_STREAM_INGRESS_SOURCE_COUNT;
}

static bool source_allowed(const pota_stream_ingress_t *ingress,
                           pota_stream_ingress_source_t source)
{
    return ingress != NULL && source_valid(source) &&
           (ingress->source_mask & POTA_STREAM_INGRESS_SOURCE_BIT(source)) != 0u;
}

static pota_stream_ingress_result_t session_result(pota_stream_result_t result)
{
    return result == POTA_STREAM_RESULT_OK ? POTA_STREAM_INGRESS_OK
                                            : POTA_STREAM_INGRESS_SESSION;
}

static pota_stream_ingress_result_t remember(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_result_t result)
{
    if (ingress != NULL) {
        ingress->last_result = result;
    }
    return result;
}

bool pota_stream_ingress_init(pota_stream_ingress_t *ingress,
                              pota_stream_session_t *session,
                              uint32_t source_mask,
                              uint32_t max_frame_size)
{
    if (ingress == NULL || session == NULL || source_mask == 0u ||
        max_frame_size == 0u || max_frame_size > POTA_MAX_DATA_BLOCK_SIZE) {
        return false;
    }
    memset(ingress, 0, sizeof(*ingress));
    ingress->session = session;
    ingress->source_mask = source_mask;
    ingress->max_frame_size = max_frame_size;
    ingress->active_source = POTA_STREAM_INGRESS_SOURCE_COUNT;
    ingress->last_result = POTA_STREAM_INGRESS_OK;
    return true;
}

pota_stream_ingress_result_t pota_stream_ingress_open(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source,
    const pota_stream_open_t *open)
{
    if (ingress == NULL || open == NULL) {
        return remember(ingress, POTA_STREAM_INGRESS_BAD_ARGUMENT);
    }
    if (!source_allowed(ingress, source)) {
        return remember(ingress, POTA_STREAM_INGRESS_SOURCE_REJECTED);
    }
    if (ingress->open) {
        return remember(ingress, POTA_STREAM_INGRESS_SESSION);
    }
    const pota_stream_result_t result =
        pota_stream_session_open(ingress->session, open);
    if (result != POTA_STREAM_RESULT_OK) {
        return remember(ingress, session_result(result));
    }
    ingress->active_source = source;
    ingress->open = true;
    return remember(ingress, POTA_STREAM_INGRESS_OK);
}

pota_stream_ingress_result_t pota_stream_ingress_write(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source,
    uint32_t offset,
    const uint8_t *data,
    uint32_t size,
    bool has_crc32,
    uint32_t crc32)
{
    if (ingress == NULL || data == NULL || size == 0u) {
        return remember(ingress, POTA_STREAM_INGRESS_BAD_ARGUMENT);
    }
    if (!source_allowed(ingress, source) || !ingress->open ||
        source != ingress->active_source) {
        return remember(ingress, POTA_STREAM_INGRESS_SOURCE_REJECTED);
    }
    if (size > ingress->max_frame_size) {
        return remember(ingress, POTA_STREAM_INGRESS_FRAME_TOO_LARGE);
    }
    if (has_crc32 && pota_crc32_compute(data, size) != crc32) {
        return remember(ingress, POTA_STREAM_INGRESS_CRC_MISMATCH);
    }
    return remember(ingress, session_result(pota_stream_session_write(
        ingress->session, offset, data, size)));
}

pota_stream_ingress_result_t pota_stream_ingress_service(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source,
    uint32_t budget_us)
{
    if (ingress == NULL || !source_allowed(ingress, source) ||
        !ingress->open || source != ingress->active_source) {
        return remember(ingress, POTA_STREAM_INGRESS_SOURCE_REJECTED);
    }
    const pota_stream_ingress_result_t result = session_result(
        pota_stream_session_service(ingress->session, budget_us));
    if (result == POTA_STREAM_INGRESS_OK &&
        pota_stream_session_state(ingress->session) ==
            POTA_STREAM_STATE_READY_TO_REBOOT) {
        ingress->open = false;
    }
    return remember(ingress, result);
}

pota_stream_ingress_result_t pota_stream_ingress_close(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source)
{
    if (ingress == NULL || !source_allowed(ingress, source) ||
        !ingress->open || source != ingress->active_source) {
        return remember(ingress, POTA_STREAM_INGRESS_SOURCE_REJECTED);
    }
    const pota_stream_ingress_result_t result =
        session_result(pota_stream_session_close(ingress->session));
    /* Keep the ingress lease until the END state machine reaches READY; the
     * AO must continue servicing FlashTransaction substeps after CLOSE ACK. */
    return remember(ingress, result);
}

pota_stream_ingress_result_t pota_stream_ingress_abort(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source)
{
    if (ingress == NULL || !source_allowed(ingress, source) ||
        !ingress->open || source != ingress->active_source) {
        return remember(ingress, POTA_STREAM_INGRESS_SOURCE_REJECTED);
    }
    const pota_stream_ingress_result_t result =
        session_result(pota_stream_session_abort(ingress->session));
    if (result == POTA_STREAM_INGRESS_OK) {
        ingress->open = false;
    }
    return remember(ingress, result);
}

bool pota_stream_ingress_get_status(const pota_stream_ingress_t *ingress,
                                    pota_stream_ingress_status_t *status)
{
    if (ingress == NULL || status == NULL || ingress->session == NULL) {
        return false;
    }
    status->source = ingress->active_source;
    status->state = pota_stream_session_state(ingress->session);
    status->durable_offset = pota_stream_session_durable_offset(ingress->session);
    status->stream_token = pota_stream_session_token(ingress->session);
    status->last_result = ingress->last_result;
    return true;
}
