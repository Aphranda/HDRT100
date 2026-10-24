#include <string.h>
#include "distributed_refmem_internal.h"

#include "refmem_cmd_reply.h"
#include "refmem_command.h"
#include "tdma_flight_engine.h"

#define DISTRIBUTED_REFMEM_CMD_REPLY_PUBLISH_COUNT 4u
#define DISTRIBUTED_REFMEM_CMD_REPLY_SLOT_COUNT \
    TDMA_FLIGHT_SHORT_SLOT_COUNT

typedef struct {
    uint32_t command_seq;
    uint32_t state;
    uint32_t reason;
    uint32_t origin_slot;
    uint32_t publish_remaining;
} distributed_refmem_cmd_reply_tx_t;

typedef struct {
    uint32_t valid;
    uint32_t seq16;
    uint32_t flags;
    uint32_t reason;
} distributed_refmem_cmd_reply_rx_t;

static distributed_refmem_cmd_reply_tx_t s_cmd_reply_tx;
static distributed_refmem_cmd_reply_rx_t
    s_cmd_reply_rx[DISTRIBUTED_REFMEM_CMD_REPLY_SLOT_COUNT];
static uint32_t s_cmd_reply_last_seq;
static uint32_t s_cmd_reply_last_complete;
static uint32_t s_cmd_reply_armed_count;
static uint32_t s_cmd_reply_published_count;
static uint32_t s_cmd_reply_rx_count;
static uint32_t s_cmd_reply_applied_count;
static uint32_t s_cmd_reply_stale_count;
static uint32_t s_cmd_reply_bad_target_count;
static uint32_t s_cmd_reply_duplicate_count;

void distributed_refmem_cmd_reply_arm(uint32_t command_seq,
                                      uint32_t origin_slot,
                                      uint32_t state,
                                      uint32_t reason)
{
    const uint32_t local = distributed_refmem_local_slot();
    if (command_seq == 0u ||
        origin_slot >= DISTRIBUTED_REFMEM_CMD_REPLY_SLOT_COUNT ||
        origin_slot == local) {
        return; /* a local-only command needs no wire reply */
    }
    s_cmd_reply_tx.command_seq = command_seq;
    s_cmd_reply_tx.origin_slot = origin_slot;
    s_cmd_reply_tx.state = state;
    s_cmd_reply_tx.reason = reason;
    s_cmd_reply_tx.publish_remaining =
        DISTRIBUTED_REFMEM_CMD_REPLY_PUBLISH_COUNT;
    s_cmd_reply_armed_count++;
}

bool distributed_refmem_cmd_reply_pending(void)
{
    return s_cmd_reply_tx.publish_remaining != 0u &&
           s_cmd_reply_tx.command_seq != 0u;
}

bool distributed_refmem_cmd_reply_tx_get(refmem_cmd_reply_t *reply)
{
    if (!distributed_refmem_cmd_reply_pending() || reply == NULL) {
        return false;
    }
    reply->command_seq = s_cmd_reply_tx.command_seq;
    reply->state = s_cmd_reply_tx.state;
    reply->reason = s_cmd_reply_tx.reason;
    reply->source_slot = s_cmd_reply_tx.origin_slot;
    return true;
}

uint32_t distributed_refmem_cmd_reply_target_mask(void)
{
    return s_cmd_reply_tx.origin_slot < DISTRIBUTED_REFMEM_CMD_REPLY_SLOT_COUNT
               ? (1u << s_cmd_reply_tx.origin_slot)
               : 0u;
}

void distributed_refmem_cmd_reply_note_published(void)
{
    s_cmd_reply_published_count++;
    if (s_cmd_reply_tx.publish_remaining != 0u) {
        s_cmd_reply_tx.publish_remaining--;
    }
}

void distributed_refmem_cmd_reply_store(uint32_t source_slot,
                                        uint16_t seq16,
                                        uint8_t flags,
                                        uint8_t reason)
{
    if (source_slot >= DISTRIBUTED_REFMEM_CMD_REPLY_SLOT_COUNT ||
        (flags & (REFMEM_CMD_REPLY_FLAG_VALID |
                  REFMEM_CMD_REPLY_FLAG_COMMAND)) !=
            (REFMEM_CMD_REPLY_FLAG_VALID | REFMEM_CMD_REPLY_FLAG_COMMAND)) {
        return;
    }
    s_cmd_reply_rx[source_slot].valid = 1u;
    s_cmd_reply_rx[source_slot].seq16 = (uint32_t)seq16;
    s_cmd_reply_rx[source_slot].flags = (uint32_t)flags;
    s_cmd_reply_rx[source_slot].reason = (uint32_t)reason;
    s_cmd_reply_rx_count++;
}

/* Fold the newest reply of every mirrored source slot into the origin's own
 * node control record.  The record is the local REFMEM fact; no second ACK
 * region and no coordinator-private state are introduced.  A partial result
 * is published as POSTED with the acked bits so CTRL:STATus? shows progress;
 * the terminal result is latched once and not overwritten by later services. */
void distributed_refmem_cmd_reply_service(void)
{
    const uint32_t local = distributed_refmem_local_slot();
    refmem_command_snapshot_t snapshot;
    if (!distributed_refmem_node_control_read(local, &snapshot, NULL, NULL)) {
        return;
    }
    if (snapshot.command_seq == 0u) {
        s_cmd_reply_last_seq = 0u;
        s_cmd_reply_last_complete = 0u;
        return;
    }
    if (s_cmd_reply_last_seq != snapshot.command_seq) {
        s_cmd_reply_last_seq = snapshot.command_seq;
        s_cmd_reply_last_complete = 0u;
    }
    if (s_cmd_reply_last_complete != 0u) {
        return;
    }
    /* Only the command issuer aggregates replies.  A target has the same
     * request bytes mirrored but must not absorb another target's result. */
    if (snapshot.source_node != local) {
        return;
    }
    const uint32_t remote_mask = snapshot.target_mask & ~(1u << local);
    if (remote_mask == 0u) {
        return;
    }

    refmem_cmd_reply_aggregate_t aggregate;
    refmem_cmd_reply_aggregate_begin(&aggregate, snapshot.command_seq,
                                     snapshot.target_mask,
                                     snapshot.required_mask);
    aggregate.acked_mask = snapshot.ack_flags;
    aggregate.nacked_mask = snapshot.nack_flags;
    aggregate.last_slot = snapshot.last_reason_slot;
    aggregate.last_reason = snapshot.last_reason;

    for (uint32_t slot = 0u;
         slot < DISTRIBUTED_REFMEM_CMD_REPLY_SLOT_COUNT; slot++) {
        if ((remote_mask & (1u << slot)) == 0u) {
            continue;
        }
        const distributed_refmem_cmd_reply_rx_t *entry = &s_cmd_reply_rx[slot];
        if (entry->valid == 0u) {
            continue;
        }
        refmem_cmd_reply_wire_t wire = {
            .seq16 = (uint16_t)entry->seq16,
            .flags = (uint8_t)entry->flags,
            .reason = (uint8_t)entry->reason,
        };
        refmem_cmd_reply_t reply;
        if (!refmem_cmd_reply_decode(&wire, &reply)) {
            s_cmd_reply_bad_target_count++;
            continue;
        }
        reply.source_slot = slot;
        const uint32_t stale_before = aggregate.stale_count;
        const uint32_t duplicate_before = aggregate.duplicate_count;
        (void)refmem_cmd_reply_aggregate_apply(&aggregate, &reply);
        s_cmd_reply_stale_count += aggregate.stale_count - stale_before;
        s_cmd_reply_duplicate_count +=
            aggregate.duplicate_count - duplicate_before;
    }
    if (aggregate.applied_count == 0u) {
        return;
    }
    s_cmd_reply_applied_count += aggregate.applied_count;
    const uint32_t state = refmem_cmd_reply_aggregate_finish(&aggregate);
    if (state == snapshot.state &&
        aggregate.acked_mask == snapshot.ack_flags &&
        aggregate.nacked_mask == snapshot.nack_flags) {
        return;
    }
    const uint32_t answered =
        (aggregate.acked_mask | aggregate.nacked_mask) &
        snapshot.required_mask;
    const bool all_required_terminal =
        snapshot.required_mask != 0u &&
        answered == snapshot.required_mask;
    (void)distributed_refmem_node_control_update_status(
        local, state, aggregate.acked_mask, aggregate.nacked_mask, 0u, 0u,
        aggregate.last_reason, aggregate.last_slot, 0u,
        all_required_terminal ? snapshot.command_seq : 0u);
    if (all_required_terminal) {
        s_cmd_reply_last_complete = 1u;
    }
}

bool distributed_refmem_cmd_reply_status(refmem_cmd_reply_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->rx_count = s_cmd_reply_rx_count;
    status->applied_count = s_cmd_reply_applied_count;
    status->stale_count = s_cmd_reply_stale_count;
    status->bad_target_count = s_cmd_reply_bad_target_count;
    status->duplicate_count = s_cmd_reply_duplicate_count;
    status->armed_count = s_cmd_reply_armed_count;
    status->published_count = s_cmd_reply_published_count;
    status->publish_remaining = s_cmd_reply_tx.publish_remaining;
    refmem_command_snapshot_t snapshot;
    if (distributed_refmem_node_control_read(
            distributed_refmem_local_slot(), &snapshot, NULL, NULL)) {
        status->pending_seq = snapshot.command_seq;
        status->aggregate_state = snapshot.state;
        status->acked_mask = snapshot.ack_flags;
        status->nacked_mask = snapshot.nack_flags;
        status->last_slot = snapshot.last_reason_slot;
        status->last_reason = snapshot.last_reason;
        status->last_state = snapshot.state;
    }
    return true;
}
