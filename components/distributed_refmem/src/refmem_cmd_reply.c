#include "refmem_cmd_reply.h"

#include <string.h>

#include "tdma_process_image_layout.h"

_Static_assert(REFMEM_CMD_REPLY_FLAG_VALID ==
                   TDMA_PROCESS_IMAGE_ACK_FLAG_VALID,
               "command reply VALID flag must alias the mailbox ACK seat");
_Static_assert(REFMEM_CMD_REPLY_FLAG_NACK ==
                   TDMA_PROCESS_IMAGE_ACK_FLAG_NACK,
               "command reply NACK flag must alias the mailbox ACK seat");
_Static_assert(REFMEM_CMD_REPLY_FLAG_COMMAND ==
                   TDMA_PROCESS_IMAGE_ACK_FLAG_COMMAND,
               "command reply COMMAND flag must own reserved bit 3");

bool refmem_cmd_reply_encode(const refmem_cmd_reply_t *reply,
                             refmem_cmd_reply_wire_t *wire)
{
    if (reply == NULL || wire == NULL || reply->command_seq == 0u) {
        return false;
    }
    uint8_t flags = REFMEM_CMD_REPLY_FLAG_VALID |
                    REFMEM_CMD_REPLY_FLAG_COMMAND;
    if (reply->state == (uint32_t)REFMEM_COMMAND_STATE_ACKED) {
        /* no NACK bit */
    } else if (reply->state == (uint32_t)REFMEM_COMMAND_STATE_NACKED ||
               reply->state == (uint32_t)REFMEM_COMMAND_STATE_TIMED_OUT) {
        flags |= REFMEM_CMD_REPLY_FLAG_NACK;
    } else {
        return false; /* only terminal results are reported */
    }
    wire->seq16 = (uint16_t)(reply->command_seq & REFMEM_CMD_REPLY_SEQ16_MASK);
    wire->flags = flags;
    wire->reason = (uint8_t)reply->reason;
    return true;
}

bool refmem_cmd_reply_decode(const refmem_cmd_reply_wire_t *wire,
                             refmem_cmd_reply_t *reply)
{
    if (wire == NULL || reply == NULL ||
        (wire->flags & (REFMEM_CMD_REPLY_FLAG_VALID |
                        REFMEM_CMD_REPLY_FLAG_COMMAND)) !=
            (REFMEM_CMD_REPLY_FLAG_VALID | REFMEM_CMD_REPLY_FLAG_COMMAND)) {
        return false;
    }
    memset(reply, 0, sizeof(*reply));
    reply->command_seq = (uint32_t)wire->seq16;
    reply->source_slot = REFMEM_COMMAND_INVALID_NODE;
    if ((wire->flags & REFMEM_CMD_REPLY_FLAG_NACK) != 0u) {
        reply->state = (uint32_t)REFMEM_COMMAND_STATE_NACKED;
        reply->reason = (uint32_t)wire->reason;
    } else {
        reply->state = (uint32_t)REFMEM_COMMAND_STATE_ACKED;
        reply->reason = (uint32_t)REFMEM_COMMAND_REASON_NONE;
    }
    return true;
}

void refmem_cmd_reply_aggregate_begin(refmem_cmd_reply_aggregate_t *aggregate,
                                      uint32_t command_seq,
                                      uint32_t target_mask,
                                      uint32_t required_mask)
{
    if (aggregate == NULL) {
        return;
    }
    memset(aggregate, 0, sizeof(*aggregate));
    aggregate->command_seq = command_seq;
    aggregate->target_mask = target_mask;
    aggregate->required_mask = required_mask;
    aggregate->last_slot = REFMEM_COMMAND_INVALID_NODE;
    aggregate->state = (uint32_t)REFMEM_COMMAND_STATE_POSTED;
}

uint32_t refmem_cmd_reply_aggregate_finish(
    refmem_cmd_reply_aggregate_t *aggregate)
{
    if (aggregate == NULL) {
        return (uint32_t)REFMEM_COMMAND_STATE_IDLE;
    }
    if ((aggregate->nacked_mask & aggregate->required_mask) != 0u) {
        aggregate->state = (uint32_t)REFMEM_COMMAND_STATE_NACKED;
    } else if (aggregate->required_mask != 0u &&
               (aggregate->acked_mask & aggregate->required_mask) ==
                   aggregate->required_mask) {
        aggregate->state = (uint32_t)REFMEM_COMMAND_STATE_ACKED;
    } else {
        aggregate->state = (uint32_t)REFMEM_COMMAND_STATE_POSTED;
    }
    return aggregate->state;
}

bool refmem_cmd_reply_aggregate_apply(
    refmem_cmd_reply_aggregate_t *aggregate,
    const refmem_cmd_reply_t *reply)
{
    if (aggregate == NULL || reply == NULL ||
        aggregate->command_seq == 0u) {
        return false;
    }
    if ((reply->command_seq & REFMEM_CMD_REPLY_SEQ16_MASK) !=
        (aggregate->command_seq & REFMEM_CMD_REPLY_SEQ16_MASK)) {
        aggregate->stale_count++;
        return false;
    }
    if (reply->source_slot >= 32u ||
        (aggregate->target_mask & (1u << reply->source_slot)) == 0u) {
        aggregate->bad_target_count++;
        return false;
    }
    const uint32_t bit = 1u << reply->source_slot;
    if ((aggregate->acked_mask & bit) != 0u ||
        (aggregate->nacked_mask & bit) != 0u) {
        aggregate->duplicate_count++;
        return false;
    }
    if (reply->state == (uint32_t)REFMEM_COMMAND_STATE_ACKED) {
        aggregate->acked_mask |= bit;
    } else if (reply->state == (uint32_t)REFMEM_COMMAND_STATE_NACKED ||
               reply->state == (uint32_t)REFMEM_COMMAND_STATE_TIMED_OUT) {
        aggregate->nacked_mask |= bit;
        aggregate->last_reason = reply->reason;
    } else {
        return false;
    }
    aggregate->last_slot = reply->source_slot;
    aggregate->last_state = reply->state;
    aggregate->applied_count++;
    (void)refmem_cmd_reply_aggregate_finish(aggregate);
    return true;
}
