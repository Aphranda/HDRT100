#include "refmem_cmd_fragment.h"

#include <string.h>

static uint32_t refmem_cmd_fragment_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

void refmem_cmd_fragment_reset(refmem_cmd_fragment_ctx_t *ctx)
{
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

uint32_t refmem_cmd_fragment_count(size_t frame_size)
{
    return (uint32_t)((frame_size + REFMEM_CMD_FRAGMENT_DATA_SIZE - 1u) /
                      REFMEM_CMD_FRAGMENT_DATA_SIZE);
}

bool refmem_cmd_fragment_get(const uint8_t *frame,
                             size_t frame_size,
                             uint32_t index,
                             uint32_t *word)
{
    if (frame == NULL || word == NULL) {
        return false;
    }
    const size_t offset = (size_t)index * REFMEM_CMD_FRAGMENT_DATA_SIZE;
    if (offset >= frame_size) {
        return false;
    }
    uint8_t bytes[REFMEM_CMD_FRAGMENT_DATA_SIZE] = {0u, 0u, 0u, 0u};
    for (size_t i = 0u; i < REFMEM_CMD_FRAGMENT_DATA_SIZE; i++) {
        if (offset + i < frame_size) {
            bytes[i] = frame[offset + i];
        }
    }
    *word = refmem_cmd_fragment_le32(bytes);
    return true;
}

bool refmem_cmd_fragment_accept(refmem_cmd_fragment_ctx_t *ctx,
                                uint8_t source_slot,
                                uint32_t frame_seq,
                                uint8_t fragment_count,
                                uint8_t index,
                                uint32_t word,
                                uint8_t *frame,
                                size_t frame_capacity,
                                size_t *frame_size)
{
    if (ctx == NULL || frame == NULL || frame_size == NULL ||
        fragment_count == 0u ||
        fragment_count > REFMEM_CMD_FRAGMENT_MAX_COUNT ||
        index >= fragment_count ||
        (size_t)fragment_count * REFMEM_CMD_FRAGMENT_DATA_SIZE >
            frame_capacity) {
        if (ctx != NULL) {
            ctx->rejected++;
        }
        return false;
    }
    if (ctx->active == 0u || ctx->source_slot != source_slot ||
        ctx->frame_seq != frame_seq) {
        memset(ctx, 0, sizeof(*ctx));
        ctx->active = 1u;
        ctx->source_slot = source_slot;
        ctx->frame_seq = frame_seq;
        ctx->fragment_count = fragment_count;
        ctx->next_fragment = 0u;
    }
    if (index != ctx->next_fragment) {
        ctx->rejected++;
        return false;
    }
    memcpy(ctx->data + (size_t)index * REFMEM_CMD_FRAGMENT_DATA_SIZE, &word,
           REFMEM_CMD_FRAGMENT_DATA_SIZE);
    ctx->next_fragment++;
    ctx->accepted++;
    if (ctx->next_fragment < ctx->fragment_count) {
        return false;
    }
    const size_t assembled = (size_t)ctx->fragment_count *
                             REFMEM_CMD_FRAGMENT_DATA_SIZE;
    size_t actual = assembled;
    if (assembled >= 8u) {
        const size_t header_size = ctx->data[4];
        const size_t payload_size =
            (size_t)ctx->data[6] | ((size_t)ctx->data[7] << 8u);
        const size_t total = header_size + payload_size;
        if (total > 0u && total <= assembled) {
            actual = total;
        }
    }
    memcpy(frame, ctx->data, actual);
    *frame_size = actual;
    ctx->active = 0u;
    return true;
}

uint32_t refmem_cmd_fragment_record_build(uint32_t frame_seq,
                                          uint8_t index,
                                          uint8_t count,
                                          uint8_t source_slot,
                                          uint32_t word,
                                          uint8_t record[REFMEM_CMD_FRAGMENT_RECORD_SIZE])
{
    if (record == NULL) {
        return 0u;
    }
    record[0] = (uint8_t)(frame_seq & 0xFFu);
    record[1] = (uint8_t)((frame_seq >> 8u) & 0xFFu);
    record[2] = index;
    record[3] = count;
    record[4] = source_slot;
    record[5] = (uint8_t)(word & 0xFFu);
    record[6] = (uint8_t)((word >> 8u) & 0xFFu);
    record[7] = (uint8_t)((word >> 16u) & 0xFFu);
    record[8] = (uint8_t)((word >> 24u) & 0xFFu);
    return REFMEM_CMD_FRAGMENT_RECORD_SIZE;
}

bool refmem_cmd_fragment_record_parse(const uint8_t *record,
                                      size_t record_size,
                                      uint32_t *frame_seq,
                                      uint8_t *index,
                                      uint8_t *count,
                                      uint8_t *source_slot,
                                      uint32_t *word)
{
    if (record == NULL || record_size != REFMEM_CMD_FRAGMENT_RECORD_SIZE ||
        frame_seq == NULL || index == NULL || count == NULL ||
        source_slot == NULL || word == NULL) {
        return false;
    }
    *frame_seq = (uint32_t)record[0] | ((uint32_t)record[1] << 8u);
    *index = record[2];
    *count = record[3];
    *source_slot = record[4];
    *word = (uint32_t)record[5] | ((uint32_t)record[6] << 8u) |
            ((uint32_t)record[7] << 16u) | ((uint32_t)record[8] << 24u);
    return true;
}
