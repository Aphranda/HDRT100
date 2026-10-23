#ifndef REFMEM_CMD_FRAGMENT_H
#define REFMEM_CMD_FRAGMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* C1 command-frame fragmentation for the small mailbox slots.
 *
 * The process-image mailbox carries 4 B per frame (VDC precedent), so a
 * command frame is split into 4 B fragments with a fragment index/count.  The
 * reassembler accepts exactly one active source at a time and rejects stale /
 * out-of-order fragments.  Storage-only and host testable. */
#define REFMEM_CMD_FRAGMENT_DATA_SIZE 4u
#define REFMEM_CMD_FRAGMENT_MAX_BYTES 256u
#define REFMEM_CMD_FRAGMENT_MAX_COUNT \
    (REFMEM_CMD_FRAGMENT_MAX_BYTES / REFMEM_CMD_FRAGMENT_DATA_SIZE)

typedef struct {
    uint8_t active;
    uint8_t source_slot;
    uint8_t fragment_count;
    uint8_t next_fragment;
    uint32_t frame_seq;
    uint32_t accepted;
    uint32_t rejected;
    uint8_t data[REFMEM_CMD_FRAGMENT_MAX_BYTES];
} refmem_cmd_fragment_ctx_t;

void refmem_cmd_fragment_reset(refmem_cmd_fragment_ctx_t *ctx);
uint32_t refmem_cmd_fragment_count(size_t frame_size);
bool refmem_cmd_fragment_get(const uint8_t *frame,
                             size_t frame_size,
                             uint32_t index,
                             uint32_t *word);
#define REFMEM_CMD_FRAGMENT_RECORD_SIZE 9u

uint32_t refmem_cmd_fragment_record_build(uint32_t frame_seq,
                                          uint8_t index,
                                          uint8_t count,
                                          uint8_t source_slot,
                                          uint32_t word,
                                          uint8_t record[REFMEM_CMD_FRAGMENT_RECORD_SIZE]);
bool refmem_cmd_fragment_record_parse(const uint8_t *record,
                                      size_t record_size,
                                      uint32_t *frame_seq,
                                      uint8_t *index,
                                      uint8_t *count,
                                      uint8_t *source_slot,
                                      uint32_t *word);

bool refmem_cmd_fragment_accept(refmem_cmd_fragment_ctx_t *ctx,
                                uint8_t source_slot,
                                uint32_t frame_seq,
                                uint8_t fragment_count,
                                uint8_t index,
                                uint32_t word,
                                uint8_t *frame,
                                size_t frame_capacity,
                                size_t *frame_size);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_CMD_FRAGMENT_H */
