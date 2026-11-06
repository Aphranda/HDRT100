#include "tdma_ring_phase.h"

#include <string.h>

#include "osal.h"
#include "tdma_maintenance_runtime.h"

static tdma_ring_phase_peer_t s_peers[TDMA_RING_PHASE_NODE_MAX];
static uint32_t s_peer_seen_ms[TDMA_RING_PHASE_NODE_MAX];
static uint8_t s_local_phase;
static uint16_t s_round;
static uint8_t s_generation;
static uint32_t s_publish_count;
static uint32_t s_receive_count;
static uint32_t s_reject_count;
static uint32_t s_ready_fail_count;
static uint32_t s_commit_count;
/* Announcement waiting for its ring sequence.  A phase is only applied when
 * the local ring has reached `s_next_target`, so every board switches on the
 * same ring sequence instead of on its own receipt time. */
static uint8_t s_next_phase;
static uint16_t s_next_round;
static uint16_t s_next_target;
static bool s_next_valid;

void tdma_ring_phase_reset(void)
{
    memset(s_peers, 0, sizeof(s_peers));
    memset(s_peer_seen_ms, 0, sizeof(s_peer_seen_ms));
    s_local_phase = (uint8_t)TDMA_RING_PHASE_IDLE;
    s_round = 0u;
    s_generation = 0u;
    s_publish_count = 0u;
    s_receive_count = 0u;
    s_reject_count = 0u;
    s_ready_fail_count = 0u;
    s_commit_count = 0u;
    s_next_phase = (uint8_t)TDMA_RING_PHASE_IDLE;
    s_next_round = 0u;
    s_next_target = 0u;
    s_next_valid = false;
}

uint32_t tdma_ring_phase_encode(const tdma_ring_phase_msg_t *msg,
                                uint8_t out[TDMA_RING_PHASE_MSG_SIZE])
{
    if (msg == NULL || out == NULL) {
        return 0u;
    }
    out[0] = msg->version;
    out[1] = msg->phase;
    out[2] = (uint8_t)(msg->round & 0xFFu);
    out[3] = (uint8_t)(msg->round >> 8u);
    out[4] = msg->source_slot;
    out[5] = (uint8_t)(msg->target_seq & 0xFFu);
    out[6] = (uint8_t)(msg->target_seq >> 8u);
    out[7] = msg->reserved;
    return TDMA_RING_PHASE_MSG_SIZE;
}

bool tdma_ring_phase_decode(const uint8_t *payload, size_t size,
                            tdma_ring_phase_msg_t *msg)
{
    if (payload == NULL || msg == NULL || size != TDMA_RING_PHASE_MSG_SIZE) {
        return false;
    }
    if (payload[0] != TDMA_RING_PHASE_VERSION) {
        return false;
    }
    const uint8_t phase = payload[1];
    if (phase > (uint8_t)TDMA_RING_PHASE_RESTORING) {
        return false;
    }
    if (payload[4] >= TDMA_RING_PHASE_NODE_MAX) {
        return false;
    }
    msg->version = payload[0];
    msg->phase = phase;
    msg->round = (uint16_t)((uint16_t)payload[2] |
                            ((uint16_t)payload[3] << 8u));
    msg->source_slot = payload[4];
    msg->target_seq = (uint16_t)((uint16_t)payload[5] |
                                 ((uint16_t)payload[6] << 8u));
    msg->reserved = payload[7];
    return true;
}

bool tdma_ring_phase_publish_at(uint8_t phase, uint16_t round,
                                uint16_t target_seq)
{
    /* Local state is authoritative for this node even when the broadcast is
     * refused (transport not ready), but the phase itself is applied later, in
     * tdma_ring_phase_service(), when the local ring reaches the target. */
    s_next_phase = phase;
    s_next_round = round;
    s_next_target = target_seq;
    s_next_valid = true;
    uint32_t slot = 0u;
    if (!tdma_maintenance_runtime_get_local_slot(&slot)) {
        return false;
    }
    tdma_ring_phase_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.version = TDMA_RING_PHASE_VERSION;
    msg.phase = phase;
    msg.round = round;
    msg.source_slot = (uint8_t)slot;
    msg.target_seq = target_seq;
    uint8_t payload[TDMA_RING_PHASE_MSG_SIZE];
    if (tdma_ring_phase_encode(&msg, payload) != TDMA_RING_PHASE_MSG_SIZE) {
        return false;
    }
    if (!tdma_maintenance_runtime_submit(payload, sizeof(payload),
                                         TDMA_RING_PHASE_TYPE, s_generation++,
                                         0x0Fu)) {
        return false;
    }
    s_publish_count++;
    return true;
}

bool tdma_ring_phase_publish(uint8_t phase, uint16_t round)
{
    return tdma_ring_phase_publish_at(phase, round, 0u);
}

bool tdma_ring_phase_next(uint8_t *phase, uint16_t *round, uint16_t *target_seq)
{
    if (!s_next_valid) {
        return false;
    }
    if (phase != NULL) {
        *phase = s_next_phase;
    }
    if (round != NULL) {
        *round = s_next_round;
    }
    if (target_seq != NULL) {
        *target_seq = s_next_target;
    }
    return true;
}

void tdma_ring_phase_service(uint32_t ring_seq)
{
    if (!s_next_valid) {
        return;
    }
    /* Signed difference: the ring sequence wraps. */
    if (s_next_target != 0u &&
        (int32_t)(ring_seq - (uint32_t)s_next_target) < 0) {
        return; /* the shared instant has not arrived yet */
    }
    s_local_phase = s_next_phase;
    s_round = s_next_round;
    s_next_valid = false;
    s_commit_count++;
}

bool tdma_ring_phase_accept(const uint8_t *payload, size_t size)
{
    tdma_ring_phase_msg_t msg;
    if (!tdma_ring_phase_decode(payload, size, &msg)) {
        s_reject_count++;
        return false;
    }
    s_peers[msg.source_slot].phase = msg.phase;
    s_peers[msg.source_slot].round = msg.round;
    s_peers[msg.source_slot].valid = true;
    s_peer_seen_ms[msg.source_slot] = osal_tick_ms();
    s_receive_count++;
    /* The maintenance plane is origin-broadcast: only the origin's frames
     * reach the ring, so a message is the ring's own phase, not just its
     * sender's.  The announcement is applied by tdma_ring_phase_service() when
     * the local ring reaches its target sequence, which is what makes every
     * board cross on the same sequence rather than on its own receipt. */
    uint32_t local_slot = 0u;
    if (tdma_maintenance_runtime_get_local_slot(&local_slot) &&
        msg.source_slot != local_slot) {
        s_next_phase = msg.phase;
        s_next_round = msg.round;
        s_next_target = msg.target_seq;
        s_next_valid = true;
    }
    return true;
}

uint8_t tdma_ring_phase_local(void)
{
    return s_local_phase;
}

bool tdma_ring_phase_ready(uint8_t target, uint16_t round, uint32_t node_count,
                           uint32_t stale_ms)
{
    if (node_count == 0u || node_count > TDMA_RING_PHASE_NODE_MAX) {
        return false;
    }
    uint32_t local_slot = 0u;
    if (!tdma_maintenance_runtime_get_local_slot(&local_slot)) {
        return false;
    }
    const uint32_t now = osal_tick_ms();
    for (uint32_t slot = 0u; slot < node_count; slot++) {
        uint8_t phase = 0u;
        uint16_t peer_round = 0u;
        bool valid = false;
        if (slot == local_slot) {
            phase = s_local_phase;
            peer_round = s_round;
            valid = true;
        } else {
            phase = s_peers[slot].phase;
            peer_round = s_peers[slot].round;
            /* A node that stopped advertising would otherwise satisfy the
             * barrier for ever with a stale value. */
            valid = s_peers[slot].valid &&
                    (uint32_t)(now - s_peer_seen_ms[slot]) <= stale_ms;
        }
        if (!valid || peer_round != round || phase < target) {
            return false;
        }
    }
    return true;
}

bool tdma_ring_phase_get_peer(uint32_t slot, tdma_ring_phase_peer_t *peer)
{
    if (peer == NULL || slot >= TDMA_RING_PHASE_NODE_MAX) {
        return false;
    }
    *peer = s_peers[slot];
    return true;
}

void tdma_ring_phase_get_diag(tdma_ring_phase_diag_t *diag)
{
    if (diag == NULL) {
        return;
    }
    diag->local_phase = s_local_phase;
    diag->round = s_round;
    diag->publish_count = s_publish_count;
    diag->receive_count = s_receive_count;
    diag->reject_count = s_reject_count;
    diag->ready_fail_count = s_ready_fail_count;
}
