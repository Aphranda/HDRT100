#ifndef REFMEM_CMD_REPLY_H
#define REFMEM_CMD_REPLY_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_command.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compact per-target command reply.
 *
 * The reply is the ACK/NACK of one target for one REFMEM_CMD.  It reuses the
 * per-node process-image ACK seat instead of adding a command bus or a second
 * command slot: the target publishes `seq16 + flags + reason`, and the origin
 * aggregates one row per target from its local REFMEM mirror.
 *
 * The wire form is not contiguous: `seq16` is the mailbox ACK seq field,
 * `flags` the ACK flag byte and `reason` the optional diagnostic byte.  Keep
 * the semantics here so host tests do not depend on the TDMA layout. */
#define REFMEM_CMD_REPLY_SEQ16_MASK 0xFFFFu
#define REFMEM_CMD_REPLY_FLAG_VALID (1u << 0u)
#define REFMEM_CMD_REPLY_FLAG_NACK (1u << 1u)
#define REFMEM_CMD_REPLY_FLAG_COMMAND (1u << 3u)

typedef struct {
    uint32_t command_seq;
    uint32_t state;
    uint32_t reason;
    uint32_t source_slot;
} refmem_cmd_reply_t;

typedef struct {
    uint16_t seq16;
    uint8_t flags;
    uint8_t reason;
} refmem_cmd_reply_wire_t;

typedef struct {
    uint32_t command_seq;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t acked_mask;
    uint32_t nacked_mask;
    uint32_t applied_count;
    uint32_t stale_count;
    uint32_t duplicate_count;
    uint32_t bad_target_count;
    uint32_t last_slot;
    uint32_t last_state;
    uint32_t last_reason;
    uint32_t state;
} refmem_cmd_reply_aggregate_t;

bool refmem_cmd_reply_encode(const refmem_cmd_reply_t *reply,
                             refmem_cmd_reply_wire_t *wire);
bool refmem_cmd_reply_decode(const refmem_cmd_reply_wire_t *wire,
                             refmem_cmd_reply_t *reply);
void refmem_cmd_reply_aggregate_begin(refmem_cmd_reply_aggregate_t *aggregate,
                                      uint32_t command_seq,
                                      uint32_t target_mask,
                                      uint32_t required_mask);
uint32_t refmem_cmd_reply_aggregate_finish(
    refmem_cmd_reply_aggregate_t *aggregate);
bool refmem_cmd_reply_aggregate_apply(
    refmem_cmd_reply_aggregate_t *aggregate,
    const refmem_cmd_reply_t *reply);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_CMD_REPLY_H */
