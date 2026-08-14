#include "refmem_claim_protocol.h"

#include <stddef.h>
#include <string.h>

#include "ota_crc32.h"

static uint32_t refmem_claim_crc32(const void *data, size_t size)
{
    return ota_crc32_update(0xFFFFFFFFu, (const uint8_t *)data, size) ^ 0xFFFFFFFFu;
}

static uint32_t refmem_claim_raw_payload_crc32(const void *payload,
                                               size_t payload_size,
                                               uint32_t payload_count)
{
    uint32_t crc = 0xFFFFFFFFu;
    crc = ota_crc32_update(crc,
                           (const uint8_t *)&payload_count,
                           sizeof(payload_count));
    if (payload_size != 0u && payload != NULL) {
        crc = ota_crc32_update(crc, (const uint8_t *)payload, payload_size);
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t refmem_claim_payload_crc32(const refmem_claim_propose_frame_t *frame)
{
    if (frame == NULL ||
        frame->header.payload_count > REFMEM_CLAIM_FRAME_PROPOSAL_MAX) {
        return 0u;
    }

    return refmem_claim_raw_payload_crc32(frame->proposal,
                                          frame->header.payload_count *
                                              sizeof(frame->proposal[0]),
                                          frame->header.payload_count);
}

static uint32_t refmem_claim_header_crc32(const refmem_claim_frame_header_t *header)
{
    if (header == NULL) {
        return 0u;
    }
    return refmem_claim_crc32(header,
                                  sizeof(*header) -
                                  sizeof(header->header_crc32));
}

static void refmem_claim_header_init(refmem_claim_frame_header_t *header,
                                     uint32_t frame_type,
                                     uint32_t claim_epoch,
                                     uint32_t claim_seq,
                                     uint32_t source_board_id,
                                     uint32_t source_board_uuid_crc32,
                                     uint32_t payload_count,
                                     uint32_t payload_crc32)
{
    header->magic = REFMEM_CLAIM_FRAME_MAGIC;
    header->version = REFMEM_CLAIM_FRAME_VERSION;
    header->frame_type = frame_type;
    header->claim_epoch = claim_epoch;
    header->claim_seq = claim_seq;
    header->source_board_id = source_board_id;
    header->source_board_uuid_crc32 = source_board_uuid_crc32;
    header->payload_count = payload_count;
    header->payload_crc32 = payload_crc32;
    header->header_crc32 = refmem_claim_header_crc32(header);
}

static refmem_claim_frame_result_t refmem_claim_header_validate(
    const refmem_claim_frame_header_t *header,
    uint32_t expected_type,
    uint32_t expected_count,
    uint32_t payload_crc32)
{
    if (header == NULL) {
        return REFMEM_CLAIM_FRAME_BAD_ARGUMENT;
    }
    if (header->magic != REFMEM_CLAIM_FRAME_MAGIC) {
        return REFMEM_CLAIM_FRAME_BAD_MAGIC;
    }
    if (header->version != REFMEM_CLAIM_FRAME_VERSION) {
        return REFMEM_CLAIM_FRAME_BAD_VERSION;
    }
    if (header->frame_type != expected_type) {
        return REFMEM_CLAIM_FRAME_BAD_TYPE;
    }
    if (header->payload_count != expected_count) {
        return REFMEM_CLAIM_FRAME_BAD_COUNT;
    }
    if (header->payload_crc32 != payload_crc32) {
        return REFMEM_CLAIM_FRAME_BAD_PAYLOAD_CRC;
    }
    if (header->header_crc32 != refmem_claim_header_crc32(header)) {
        return REFMEM_CLAIM_FRAME_BAD_HEADER_CRC;
    }
    return REFMEM_CLAIM_FRAME_OK;
}

bool refmem_claim_propose_frame_init(
    refmem_claim_propose_frame_t *frame,
    uint32_t claim_epoch,
    uint32_t claim_seq,
    uint32_t source_board_id,
    uint32_t source_board_uuid_crc32,
    const refmem_slot_claim_proposal_t *proposal,
    uint32_t proposal_count)
{
    if (frame == NULL ||
        proposal_count > REFMEM_CLAIM_FRAME_PROPOSAL_MAX ||
        (proposal_count != 0u && proposal == NULL)) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    if (proposal_count != 0u) {
        memcpy(frame->proposal,
               proposal,
               proposal_count * sizeof(frame->proposal[0]));
    }

    refmem_claim_header_init(&frame->header,
                             REFMEM_CLAIM_FRAME_PROPOSE,
                             claim_epoch,
                             claim_seq,
                             source_board_id,
                             source_board_uuid_crc32,
                             proposal_count,
                             refmem_claim_payload_crc32(frame));
    return true;
}

refmem_claim_frame_result_t refmem_claim_propose_frame_validate(
    const refmem_claim_propose_frame_t *frame)
{
    if (frame == NULL) {
        return REFMEM_CLAIM_FRAME_BAD_ARGUMENT;
    }
    if (frame->header.payload_count > REFMEM_CLAIM_FRAME_PROPOSAL_MAX) {
        return REFMEM_CLAIM_FRAME_BAD_COUNT;
    }
    refmem_claim_frame_result_t result =
        refmem_claim_header_validate(&frame->header,
                                     REFMEM_CLAIM_FRAME_PROPOSE,
                                     frame->header.payload_count,
                                     refmem_claim_payload_crc32(frame));
    if (result != REFMEM_CLAIM_FRAME_OK) {
        return result;
    }
    return REFMEM_CLAIM_FRAME_OK;
}

bool refmem_claim_hello_frame_init(
    refmem_claim_hello_frame_t *frame,
    uint32_t claim_epoch,
    uint32_t claim_seq,
    const refmem_claim_hello_payload_t *hello)
{
    if (frame == NULL || hello == NULL) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->hello = *hello;
    const uint32_t payload_crc32 =
        refmem_claim_raw_payload_crc32(&frame->hello, sizeof(frame->hello), 1u);
    refmem_claim_header_init(&frame->header,
                             REFMEM_CLAIM_FRAME_HELLO,
                             claim_epoch,
                             claim_seq,
                             hello->board_id,
                             hello->board_uuid_crc32,
                             1u,
                             payload_crc32);
    return true;
}

refmem_claim_frame_result_t refmem_claim_hello_frame_validate(
    const refmem_claim_hello_frame_t *frame)
{
    if (frame == NULL) {
        return REFMEM_CLAIM_FRAME_BAD_ARGUMENT;
    }
    const uint32_t payload_crc32 =
        refmem_claim_raw_payload_crc32(&frame->hello, sizeof(frame->hello), 1u);
    return refmem_claim_header_validate(&frame->header,
                                        REFMEM_CLAIM_FRAME_HELLO,
                                        1u,
                                        payload_crc32);
}

bool refmem_claim_commit_frame_init(
    refmem_claim_commit_frame_t *frame,
    uint32_t claim_epoch,
    uint32_t claim_seq,
    uint32_t source_board_id,
    uint32_t source_board_uuid_crc32,
    const refmem_claim_commit_payload_t *commit)
{
    if (frame == NULL || commit == NULL) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->commit = *commit;
    const uint32_t payload_crc32 =
        refmem_claim_raw_payload_crc32(&frame->commit, sizeof(frame->commit), 1u);
    refmem_claim_header_init(&frame->header,
                             REFMEM_CLAIM_FRAME_COMMIT,
                             claim_epoch,
                             claim_seq,
                             source_board_id,
                             source_board_uuid_crc32,
                             1u,
                             payload_crc32);
    return true;
}

refmem_claim_frame_result_t refmem_claim_commit_frame_validate(
    const refmem_claim_commit_frame_t *frame)
{
    if (frame == NULL) {
        return REFMEM_CLAIM_FRAME_BAD_ARGUMENT;
    }
    const uint32_t payload_crc32 =
        refmem_claim_raw_payload_crc32(&frame->commit, sizeof(frame->commit), 1u);
    return refmem_claim_header_validate(&frame->header,
                                        REFMEM_CLAIM_FRAME_COMMIT,
                                        1u,
                                        payload_crc32);
}
