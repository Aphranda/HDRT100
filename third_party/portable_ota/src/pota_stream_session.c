#include "pota_stream_session.h"

#include <stddef.h>
#include <string.h>

#include "pota_types.h"

static bool bytes_any_set(const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        if (bytes[index] != 0u) {
            return true;
        }
    }
    return false;
}

static bool destination_valid(const pota_stream_open_t *open)
{
    return open->destination_slot == (uint32_t)POTA_SLOT_A ||
           open->destination_slot == (uint32_t)POTA_SLOT_B;
}

static bool partition_matches_destination(const pota_stream_open_t *open)
{
    return (open->partition_id == POTA_STREAM_PARTITION_APP_A &&
            open->destination_slot == (uint32_t)POTA_SLOT_A) ||
           (open->partition_id == POTA_STREAM_PARTITION_APP_B &&
            open->destination_slot == (uint32_t)POTA_SLOT_B);
}

static bool open_valid(const pota_stream_open_t *open)
{
    return open != NULL && open->session_id != 0u && open->generation != 0u &&
           open->map_version != 0u && open->object_id != 0u &&
           open->total_size != 0u && destination_valid(open) &&
           partition_matches_destination(open) &&
           (open->capability_mask & POTA_STREAM_CAP_INACTIVE_WRITE) != 0u &&
           (open->capability_mask & POTA_STREAM_CAP_DURABLE_ACK) != 0u &&
           bytes_any_set(open->identity, POTA_STREAM_IDENTITY_SIZE) &&
           bytes_any_set(open->package_hash, POTA_STREAM_PACKAGE_HASH_SIZE);
}

static pota_stream_result_t core_result(pota_error_t error)
{
    return error == POTA_ERR_NONE ? POTA_STREAM_RESULT_OK :
           POTA_STREAM_RESULT_CORE;
}

bool pota_stream_session_init(pota_stream_session_t *session,
                              const pota_platform_t *platform)
{
    if (session == NULL || platform == NULL ||
        !pota_session_init(&session->core, platform)) {
        return false;
    }
    memset(&session->open, 0, sizeof(session->open));
    session->state = POTA_STREAM_STATE_IDLE;
    session->durable_offset = 0u;
    session->last_chunk_valid = false;
    return true;
}

pota_stream_result_t pota_stream_session_open(
    pota_stream_session_t *session, const pota_stream_open_t *open)
{
    if (session == NULL || open == NULL) {
        return POTA_STREAM_RESULT_BAD_ARGUMENT;
    }
    if (session->state != POTA_STREAM_STATE_IDLE) {
        return POTA_STREAM_RESULT_INVALID_STATE;
    }
    if (!open_valid(open)) {
        return POTA_STREAM_RESULT_CAPABILITY;
    }

    const pota_slot_t target = session->core.core.target_slot;
    if ((uint32_t)target != open->destination_slot) {
        return POTA_STREAM_RESULT_DESTINATION;
    }

    const pota_begin_t begin = {
        .size = open->total_size,
        .crc32 = open->package_crc32,
        .package_mode = open->package_mode,
    };
    if (pota_session_begin(&session->core, &begin) != POTA_ERR_NONE) {
        session->state = POTA_STREAM_STATE_FAILED;
        return POTA_STREAM_RESULT_CORE;
    }
    session->open = *open;
    session->state = POTA_STREAM_STATE_OPEN;
    session->durable_offset = 0u;
    session->last_chunk_valid = false;
    return POTA_STREAM_RESULT_OK;
}

pota_stream_result_t pota_stream_session_service(
    pota_stream_session_t *session, uint32_t budget_us)
{
    if (session == NULL) {
        return POTA_STREAM_RESULT_BAD_ARGUMENT;
    }
    if (session->state != POTA_STREAM_STATE_OPEN &&
        session->state != POTA_STREAM_STATE_RECEIVING) {
        return POTA_STREAM_RESULT_INVALID_STATE;
    }
    const pota_error_t error = pota_session_service(&session->core, budget_us);
    if (error != POTA_ERR_NONE) {
        session->state = POTA_STREAM_STATE_FAILED;
        return POTA_STREAM_RESULT_CORE;
    }
    if (session->core.core.status.state == (uint32_t)POTA_STATE_RECEIVING) {
        session->state = POTA_STREAM_STATE_RECEIVING;
    }
    return POTA_STREAM_RESULT_OK;
}

pota_stream_result_t pota_stream_session_write(
    pota_stream_session_t *session, uint32_t offset,
    const uint8_t *data, uint32_t size)
{
    if (session == NULL || data == NULL || size == 0u ||
        size > POTA_MAX_DATA_BLOCK_SIZE) {
        return POTA_STREAM_RESULT_BAD_ARGUMENT;
    }
    if (session->state != POTA_STREAM_STATE_RECEIVING) {
        return POTA_STREAM_RESULT_INVALID_STATE;
    }
    if (offset > session->open.total_size ||
        size > session->open.total_size - offset) {
        return POTA_STREAM_RESULT_OFFSET;
    }

    const uint32_t chunk_crc32 = pota_crc32_compute(data, size);
    if (offset < session->durable_offset) {
        if (session->last_chunk_valid && offset == session->last_chunk_offset &&
            size == session->last_chunk_size &&
            chunk_crc32 == session->last_chunk_crc32) {
            return POTA_STREAM_RESULT_OK;
        }
        return POTA_STREAM_RESULT_CONFLICT;
    }
    if (offset != session->durable_offset) {
        return POTA_STREAM_RESULT_OFFSET;
    }

    if (pota_session_write(&session->core, data, size) != POTA_ERR_NONE) {
        session->state = POTA_STREAM_STATE_FAILED;
        return POTA_STREAM_RESULT_CORE;
    }
    session->last_chunk_offset = offset;
    session->last_chunk_size = size;
    session->last_chunk_crc32 = chunk_crc32;
    session->last_chunk_valid = true;
    session->durable_offset += size;
    return POTA_STREAM_RESULT_OK;
}

pota_stream_result_t pota_stream_session_close(
    pota_stream_session_t *session)
{
    if (session == NULL) {
        return POTA_STREAM_RESULT_BAD_ARGUMENT;
    }
    if (session->state != POTA_STREAM_STATE_RECEIVING ||
        session->durable_offset != session->open.total_size) {
        return POTA_STREAM_RESULT_INVALID_STATE;
    }
    const pota_stream_result_t result =
        core_result(pota_session_end(&session->core));
    if (result == POTA_STREAM_RESULT_OK) {
        session->state = POTA_STREAM_STATE_READY_TO_REBOOT;
    } else {
        session->state = POTA_STREAM_STATE_FAILED;
    }
    return result;
}

pota_stream_result_t pota_stream_session_abort(
    pota_stream_session_t *session)
{
    if (session == NULL) {
        return POTA_STREAM_RESULT_BAD_ARGUMENT;
    }
    if (session->state == POTA_STREAM_STATE_IDLE ||
        session->state == POTA_STREAM_STATE_READY_TO_REBOOT) {
        return POTA_STREAM_RESULT_INVALID_STATE;
    }
    if (pota_session_abort(&session->core) != POTA_ERR_NONE) {
        session->state = POTA_STREAM_STATE_FAILED;
        return POTA_STREAM_RESULT_CORE;
    }
    session->state = POTA_STREAM_STATE_ABORTED;
    return POTA_STREAM_RESULT_OK;
}

uint32_t pota_stream_session_token(const pota_stream_session_t *session)
{
    if (session == NULL || session->state == POTA_STREAM_STATE_IDLE) {
        return 0u;
    }
    /* The token identifies the opened stream, not its moving durable cursor. */
    return pota_crc32_compute((const uint8_t *)&session->open,
                              sizeof(session->open));
}

uint32_t pota_stream_session_durable_offset(
    const pota_stream_session_t *session)
{
    return session == NULL ? 0u : session->durable_offset;
}

pota_stream_state_t pota_stream_session_state(
    const pota_stream_session_t *session)
{
    return session == NULL ? POTA_STREAM_STATE_FAILED : session->state;
}
