#include "refmem_claim_protocol.h"

#include <stddef.h>
#include <string.h>

#include "ota_crc32.h"

static uint32_t refmem_claim_crc32(const void *data, size_t size)
{
    return ota_crc32_update(0xFFFFFFFFu, (const uint8_t *)data, size) ^ 0xFFFFFFFFu;
}

static uint32_t refmem_claim_payload_crc32(const refmem_claim_propose_frame_t *frame)
{
    if (frame == NULL ||
        frame->header.payload_count > REFMEM_CLAIM_FRAME_PROPOSAL_MAX) {
        return 0u;
    }

    uint32_t crc = 0xFFFFFFFFu;
    crc = ota_crc32_update(crc,
                           (const uint8_t *)&frame->header.payload_count,
                           sizeof(frame->header.payload_count));
    crc = ota_crc32_update(crc,
                           (const uint8_t *)frame->proposal,
                           frame->header.payload_count *
                               sizeof(frame->proposal[0]));
    return crc ^ 0xFFFFFFFFu;
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
    frame->header.magic = REFMEM_CLAIM_FRAME_MAGIC;
    frame->header.version = REFMEM_CLAIM_FRAME_VERSION;
    frame->header.frame_type = REFMEM_CLAIM_FRAME_PROPOSE;
    frame->header.claim_epoch = claim_epoch;
    frame->header.claim_seq = claim_seq;
    frame->header.source_board_id = source_board_id;
    frame->header.source_board_uuid_crc32 = source_board_uuid_crc32;
    frame->header.payload_count = proposal_count;
    if (proposal_count != 0u) {
        memcpy(frame->proposal,
               proposal,
               proposal_count * sizeof(frame->proposal[0]));
    }

    frame->header.payload_crc32 = refmem_claim_payload_crc32(frame);
    frame->header.header_crc32 = refmem_claim_header_crc32(&frame->header);
    return true;
}

refmem_claim_frame_result_t refmem_claim_propose_frame_validate(
    const refmem_claim_propose_frame_t *frame)
{
    if (frame == NULL) {
        return REFMEM_CLAIM_FRAME_BAD_ARGUMENT;
    }
    if (frame->header.magic != REFMEM_CLAIM_FRAME_MAGIC) {
        return REFMEM_CLAIM_FRAME_BAD_MAGIC;
    }
    if (frame->header.version != REFMEM_CLAIM_FRAME_VERSION) {
        return REFMEM_CLAIM_FRAME_BAD_VERSION;
    }
    if (frame->header.frame_type != REFMEM_CLAIM_FRAME_PROPOSE) {
        return REFMEM_CLAIM_FRAME_BAD_TYPE;
    }
    if (frame->header.payload_count > REFMEM_CLAIM_FRAME_PROPOSAL_MAX) {
        return REFMEM_CLAIM_FRAME_BAD_COUNT;
    }
    if (frame->header.payload_crc32 != refmem_claim_payload_crc32(frame)) {
        return REFMEM_CLAIM_FRAME_BAD_PAYLOAD_CRC;
    }
    if (frame->header.header_crc32 != refmem_claim_header_crc32(&frame->header)) {
        return REFMEM_CLAIM_FRAME_BAD_HEADER_CRC;
    }
    return REFMEM_CLAIM_FRAME_OK;
}
