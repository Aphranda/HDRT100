#include "pota_stream_session.h"

#include <stddef.h>
#include <string.h>

#include "pota_stream_wire.h"
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

static bool partition_matches_destination(const pota_stream_session_t *session,
                                          const pota_stream_open_t *open)
{
    return session != NULL &&
           ((open->partition_id ==
                 session->core.core.platform.info.slot_a_partition_id &&
             open->destination_slot == (uint32_t)POTA_SLOT_A) ||
            (open->partition_id ==
                 session->core.core.platform.info.slot_b_partition_id &&
             open->destination_slot == (uint32_t)POTA_SLOT_B));
}

static bool open_valid(const pota_stream_session_t *session,
                       const pota_stream_open_t *open)
{
    return session != NULL && open != NULL && open->session_id != 0u &&
           open->generation != 0u &&
           open->map_version != 0u && open->object_id != 0u &&
           open->total_size != 0u && destination_valid(open) &&
           partition_matches_destination(session, open) &&
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
    session->checkpoint_context = NULL;
    session->checkpoint_append = NULL;
    session->checkpoint_recover = NULL;
    session->last_checkpoint_offset = 0u;
    session->resume_pending = false;
    session->resume_header_pending = false;
    memset(&session->resume_checkpoint, 0,
           sizeof(session->resume_checkpoint));
    return true;
}

bool pota_stream_session_set_checkpoint(
    pota_stream_session_t *session,
    void *context,
    pota_stream_checkpoint_append_fn append,
    const pota_stream_checkpoint_policy_t *policy)
{
    if (session == NULL || session->state != POTA_STREAM_STATE_IDLE ||
        context == NULL || append == NULL || policy == NULL ||
        !pota_stream_checkpoint_policy_valid(policy)) {
        return false;
    }
    session->checkpoint_context = context;
    session->checkpoint_append = append;
    session->checkpoint_recover = NULL;
    session->checkpoint_policy = *policy;
    session->last_checkpoint_offset = 0u;
    return true;
}

static bool checkpoint_store_append(void *context,
                                    const pota_stream_checkpoint_t *checkpoint)
{
    return pota_stream_checkpoint_append(
               (pota_stream_checkpoint_store_t *)context, checkpoint) ==
           POTA_STREAM_CHECKPOINT_OK;
}

static pota_stream_checkpoint_result_t checkpoint_store_recover(
    void *context, pota_stream_checkpoint_t *checkpoint, uint32_t *sequence)
{
    return pota_stream_checkpoint_recover_latest(
        (pota_stream_checkpoint_store_t *)context, checkpoint, sequence);
}

bool pota_stream_session_set_checkpoint_store(
    pota_stream_session_t *session,
    pota_stream_checkpoint_store_t *store,
    const pota_stream_checkpoint_policy_t *policy)
{
    if (store == NULL || !store->initialized) {
        return false;
    }
    if (!pota_stream_session_set_checkpoint(session, store,
                                            checkpoint_store_append,
                                            policy)) {
        return false;
    }
    session->checkpoint_recover = checkpoint_store_recover;
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
    if (!open_valid(session, open)) {
        return POTA_STREAM_RESULT_CAPABILITY;
    }

    if (session->core.core.platform.info.map_version == 0u ||
        open->map_version != session->core.core.platform.info.map_version) {
        return POTA_STREAM_RESULT_MISMATCH;
    }

    const pota_slot_t target = session->core.core.target_slot;
    if ((uint32_t)target != open->destination_slot) {
        return POTA_STREAM_RESULT_DESTINATION;
    }

    const pota_begin_t begin = {
        .size = open->total_size,
        .crc32 = open->package_crc32,
        .package_mode = open->package_mode,
        .selected_object_mode = open->package_mode,
    };

    if (session->checkpoint_recover != NULL) {
        pota_stream_checkpoint_t checkpoint;
        uint32_t sequence = 0u;
        const pota_stream_checkpoint_result_t recovered =
            session->checkpoint_recover(session->checkpoint_context,
                                        &checkpoint, &sequence);
        if (recovered == POTA_STREAM_CHECKPOINT_OK) {
            const uint32_t token = pota_stream_open_token(open);
            const bool same_session =
                checkpoint.session_id == open->session_id &&
                checkpoint.generation == open->generation;
            if (same_session &&
                !pota_stream_checkpoint_matches(
                    &checkpoint, open->session_id, open->generation, token,
                    open->object_id, open->total_size, open->package_crc32)) {
                return POTA_STREAM_RESULT_MISMATCH;
            }
            if (pota_stream_checkpoint_matches(
                    &checkpoint, open->session_id, open->generation, token,
                    open->object_id, open->total_size, open->package_crc32)) {
                if (!open->package_mode &&
                    pota_session_resume_raw(&session->core, &begin,
                                            checkpoint.durable_offset,
                                            checkpoint.durable_crc32) !=
                        POTA_ERR_NONE) {
                    session->state = POTA_STREAM_STATE_FAILED;
                    return POTA_STREAM_RESULT_CHECKPOINT;
                }
                session->open = *open;
                session->state = POTA_STREAM_STATE_OPEN;
                session->durable_offset = 0u;
                session->last_chunk_valid = false;
                session->last_checkpoint_offset = checkpoint.durable_offset;
                session->resume_pending = true;
                session->resume_header_pending = open->package_mode;
                session->resume_checkpoint = checkpoint;
                return POTA_STREAM_RESULT_OK;
            }
        } else if (recovered != POTA_STREAM_CHECKPOINT_NO_VALID) {
            return POTA_STREAM_RESULT_CHECKPOINT;
        }
    }

    if (pota_session_begin(&session->core, &begin) != POTA_ERR_NONE) {
        session->state = POTA_STREAM_STATE_FAILED;
        return POTA_STREAM_RESULT_CORE;
    }
    session->open = *open;
    session->state = session->core.core.status.state ==
                             (uint32_t)POTA_STATE_RECEIVING
                         ? POTA_STREAM_STATE_RECEIVING
                         : POTA_STREAM_STATE_OPEN;
    session->durable_offset = 0u;
    session->last_chunk_valid = false;
    session->last_checkpoint_offset = 0u;
    session->resume_pending = false;
    session->resume_header_pending = false;
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
    if (session->resume_header_pending) {
        return POTA_STREAM_RESULT_OK;
    }
    const pota_error_t error = pota_session_service(&session->core, budget_us);
    if (error != POTA_ERR_NONE) {
        session->state = POTA_STREAM_STATE_FAILED;
        const bool resume_failed = session->resume_pending;
        session->resume_pending = false;
        return resume_failed ? POTA_STREAM_RESULT_CHECKPOINT
                             : POTA_STREAM_RESULT_CORE;
    }
    if (session->core.core.status.state == (uint32_t)POTA_STATE_RECEIVING) {
        session->state = POTA_STREAM_STATE_RECEIVING;
        session->durable_offset =
            session->resume_pending
                ? session->resume_checkpoint.durable_offset
                : session->open.package_mode
                      ? session->core.core.status.received_size
                      : session->core.core.status.programmed_size;
        session->resume_pending = false;
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
    if (session->resume_header_pending) {
        if (session->state != POTA_STREAM_STATE_OPEN ||
            !session->open.package_mode || offset != 0u ||
            size != POTA_PACKAGE_HEADER_SIZE) {
            return POTA_STREAM_RESULT_INVALID_STATE;
        }
        const pota_begin_t begin = {
            .size = session->open.total_size,
            .crc32 = session->open.package_crc32,
            .package_mode = true,
            .selected_object_mode = true,
        };
        if (pota_session_resume_package(
                &session->core, &begin, data, size,
                session->resume_checkpoint.durable_offset,
                session->resume_checkpoint.durable_crc32,
                session->resume_checkpoint.image_crc32) != POTA_ERR_NONE) {
            session->state = POTA_STREAM_STATE_FAILED;
            session->resume_header_pending = false;
            session->resume_pending = false;
            return POTA_STREAM_RESULT_CHECKPOINT;
        }
        session->resume_header_pending = false;
        return POTA_STREAM_RESULT_OK;
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
    const uint32_t next_offset = session->durable_offset + size;
    bool checkpoint_boundary = next_offset == session->open.total_size;
    uint32_t image_crc32 = session->core.core.status.crc32_running;
    if (session->open.package_mode) {
        const uint32_t image_size =
            session->core.core.selected_image_received_size;
        image_crc32 = session->core.core.selected_image_crc32_running;
        checkpoint_boundary = checkpoint_boundary ||
            (image_size != 0u &&
             (image_size == session->core.core.selected_image_size ||
              (image_size %
               session->core.core.platform.info.flash_sector_size) == 0u));
    } else {
        checkpoint_boundary = checkpoint_boundary ||
            (next_offset %
             session->core.core.platform.info.flash_sector_size) == 0u;
    }
    if (session->checkpoint_append != NULL &&
        checkpoint_boundary &&
        pota_stream_checkpoint_should_append(&session->checkpoint_policy,
                                              session->last_checkpoint_offset,
                                              next_offset,
                                              session->open.total_size)) {
        const pota_stream_checkpoint_t checkpoint = {
            .session_id = session->open.session_id,
            .generation = session->open.generation,
            .token = pota_stream_session_token(session),
            .object_id = session->open.object_id,
            .durable_offset = next_offset,
            .total_size = session->open.total_size,
            .package_crc32 = session->open.package_crc32,
            .image_crc32 = image_crc32,
            .durable_crc32 = session->core.core.status.crc32_running,
        };
        if (!session->checkpoint_append(session->checkpoint_context,
                                        &checkpoint)) {
            session->state = POTA_STREAM_STATE_FAILED;
            return POTA_STREAM_RESULT_CHECKPOINT;
        }
        session->last_checkpoint_offset = next_offset;
    }
    session->last_chunk_offset = offset;
    session->last_chunk_size = size;
    session->last_chunk_crc32 = chunk_crc32;
    session->last_chunk_valid = true;
    session->durable_offset = next_offset;
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
    if (session->checkpoint_append != NULL) {
        const pota_stream_checkpoint_t tombstone = {
            .session_id = session->open.session_id,
            .generation = session->open.generation,
            .token = pota_stream_session_token(session),
            .object_id = session->open.object_id,
            .durable_offset = session->durable_offset,
            .total_size = session->open.total_size,
            .package_crc32 = session->open.package_crc32,
            .durable_crc32 = session->core.core.status.crc32_running,
            .image_crc32 = session->open.package_mode
                               ? session->core.core.selected_image_crc32_running
                               : session->core.core.status.crc32_running,
            .flags = POTA_STREAM_CHECKPOINT_FLAG_ABORTED,
        };
        if (!session->checkpoint_append(session->checkpoint_context,
                                        &tombstone)) {
            session->state = POTA_STREAM_STATE_FAILED;
            return POTA_STREAM_RESULT_CHECKPOINT;
        }
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
    return pota_stream_open_token(&session->open);
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
