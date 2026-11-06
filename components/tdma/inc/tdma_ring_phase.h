#ifndef TDMA_RING_PHASE_H
#define TDMA_RING_PHASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ring state negotiation ("barrier") over the maintenance plane.
 *
 * The guide ladder's C->T->R->Q->S handoff used to be decided per board: each
 * board stopped and re-formed the ring on its own, so whoever moved first broke
 * the shared ring for the others.  At 10 kHz the maintenance channel carries
 * one 10-byte chunk per ring revolution, and a GUIDE command takes ~50 s
 * because its frame is 216 bytes (54 chunks) -- not because the channel is
 * slow.  A phase message is a single chunk, so one revolution carries it.
 *
 * Every node broadcasts {phase, round} when its phase changes and refreshes it
 * periodically.  A barrier is satisfied when every node reports a phase at or
 * past the target for the same round, so all boards cross it within about one
 * revolution instead of within one command broadcast (20-78 s measured).
 *
 * The barrier is POLLED: the caller keeps its own deadline and fails closed.
 * Nothing here blocks, sleeps or services another component. */

#define TDMA_RING_PHASE_TYPE 0x56u
/* Compact guide trigger: the same maintenance plane, but 8 bytes instead of a
 * 160-byte REFMEM command frame.  The frame size is the whole difference: a
 * REFMEM trigger needs five maintenance frames and takes 20-65 s to reach the
 * boards, which is exactly the skew that kept the four windows apart. */
#define TDMA_RING_TRIGGER_TYPE 0x57u
#define TDMA_RING_TRIGGER_MSG_SIZE 8u
#define TDMA_RING_PHASE_VERSION 1u
#define TDMA_RING_PHASE_MSG_SIZE 8u
#define TDMA_RING_PHASE_NODE_MAX 8u

/* Phases are ordered: a node already past the target counts as arrived, so a
 * board that moved early is never waited for twice. */
typedef enum {
    TDMA_RING_PHASE_IDLE = 0u,
    TDMA_RING_PHASE_READY = 1u,
    TDMA_RING_PHASE_STOPPING = 2u,
    TDMA_RING_PHASE_STOPPED = 3u,
    TDMA_RING_PHASE_FORMING = 4u,
    TDMA_RING_PHASE_FORMED = 5u,
    TDMA_RING_PHASE_TRIAL = 6u,
    TDMA_RING_PHASE_RESTORING = 7u,
} tdma_ring_phase_t;

typedef struct {
    uint8_t version;
    uint8_t phase;
    uint16_t round;
    uint8_t source_slot;
    /* Ring sequence at which every node performs the transition.  A phase
     * announced with target 0 means "as soon as received" (the ring is not
     * running, so no shared sequence exists). */
    uint16_t target_seq;
    uint8_t reserved;
} tdma_ring_phase_msg_t;

typedef struct {
    uint8_t phase;
    uint16_t round;
    bool valid;
} tdma_ring_phase_peer_t;

typedef struct {
    uint8_t local_phase;
    uint16_t round;
    uint32_t publish_count;
    uint32_t receive_count;
    uint32_t reject_count;
    uint32_t ready_fail_count;
} tdma_ring_phase_diag_t;

void tdma_ring_phase_reset(void);

uint32_t tdma_ring_phase_encode(const tdma_ring_phase_msg_t *msg,
                                uint8_t out[TDMA_RING_PHASE_MSG_SIZE]);
bool tdma_ring_phase_decode(const uint8_t *payload, size_t size,
                            tdma_ring_phase_msg_t *msg);

/* Broadcast the local phase/round to every node.  False when the maintenance
 * transport is not ready; the caller keeps its own state and retries later. */
bool tdma_ring_phase_publish(uint8_t phase, uint16_t round);

/* Announce a phase to take effect at ring sequence `target_seq`.  The message
 * must go out while the ring is still running -- the maintenance channel has no
 * carrier once it is stopped, which is exactly why an immediate "stop now"
 * broadcast never reached the followers. */
bool tdma_ring_phase_publish_at(uint8_t phase, uint16_t round,
                                uint16_t target_seq);

/* Latest announced phase and its ring-sequence target (0 = on receipt). */
bool tdma_ring_phase_next(uint8_t *phase, uint16_t *round,
                          uint16_t *target_seq);

/* Apply the pending announcement once the local ring has reached its target.
 * Call once per service tick with the ring's current sequence; it is a no-op
 * until then, and a no-op when nothing is pending. */
void tdma_ring_phase_service(uint32_t ring_seq);

/* Set the local phase directly and drop any pending announcement.  Used at the
 * start of a round: the phase is sticky, and a follower still holding TRIAL
 * from the previous round would otherwise start its dwell immediately and
 * abandon its forwarding persona part-way through the origin's window. */
void tdma_ring_phase_set_local(uint8_t phase, uint16_t round);

/* Parser entry: records one peer message (allocation-free, O(1)). */
bool tdma_ring_phase_accept(const uint8_t *payload, size_t size);

/* True when every node in [0, node_count) reports the same `round` and a phase
 * at or past `target`.  The local node is authoritative for itself.  A peer
 * whose entry is older than `stale_ms` does not satisfy the barrier. */
bool tdma_ring_phase_ready(uint8_t target, uint16_t round, uint32_t node_count,
                           uint32_t stale_ms);

/* The phase this board is in.  A follower adopts the origin's phase when the
 * broadcast arrives, so this is the negotiated state, not a local guess. */
uint8_t tdma_ring_phase_local(void);

bool tdma_ring_phase_get_peer(uint32_t slot, tdma_ring_phase_peer_t *peer);
void tdma_ring_phase_get_diag(tdma_ring_phase_diag_t *diag);

/* Compact guide trigger over the same maintenance plane.  One 8-byte message
 * carries {action, epoch}; the origin consumes it through the same take() path
 * as the targets, so all four boards see the trigger within one ring
 * revolution instead of within a 160-byte command broadcast. */
bool tdma_ring_trigger_publish(uint8_t action, uint32_t epoch);
bool tdma_ring_trigger_accept(const uint8_t *payload, size_t size);
bool tdma_ring_trigger_take(uint8_t *action, uint32_t *epoch);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_RING_PHASE_H */
