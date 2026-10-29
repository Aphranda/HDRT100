/* N5.7 read-only aggregate over the per-node command records.  Kept in its
 * own translation unit so the counting/terminal-selection logic is host
 * testable with a stubbed distributed_refmem_node_control_read(); the function
 * derives everything from that single existing fact and owns no state. */
#include "distributed_refmem.h"

#include <string.h>

bool distributed_refmem_command_aggregate(refmem_command_aggregate_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->node_count = DISTRIBUTED_REFMEM_NODE_COUNT;
    uint32_t newest_seq = 0u;
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_COUNT; ++i) {
        refmem_command_snapshot_t snapshot;
        if (!distributed_refmem_node_control_read(i, &snapshot, NULL, NULL)) {
            continue;
        }
        switch ((refmem_command_state_t)snapshot.state) {
        case REFMEM_COMMAND_STATE_POSTED: out->posted++; break;
        case REFMEM_COMMAND_STATE_TAKEN: out->taken++; break;
        case REFMEM_COMMAND_STATE_BUSY: out->busy++; break;
        case REFMEM_COMMAND_STATE_ACKED: out->acked++; break;
        case REFMEM_COMMAND_STATE_NACKED: out->nacked++; break;
        case REFMEM_COMMAND_STATE_TIMED_OUT: out->timed_out++; break;
        default: break;
        }
        const bool terminal =
            snapshot.state == (uint32_t)REFMEM_COMMAND_STATE_ACKED ||
            snapshot.state == (uint32_t)REFMEM_COMMAND_STATE_NACKED ||
            snapshot.state == (uint32_t)REFMEM_COMMAND_STATE_TIMED_OUT;
        if (terminal && snapshot.command_seq >= newest_seq) {
            newest_seq = snapshot.command_seq;
            out->last_state = snapshot.state;
            out->last_reason = snapshot.last_reason;
            out->last_reason_slot = i;
            out->last_completed_seq =
                snapshot.last_completed_seq != 0u ? snapshot.last_completed_seq
                                                  : snapshot.command_seq;
        }
    }
    return true;
}
