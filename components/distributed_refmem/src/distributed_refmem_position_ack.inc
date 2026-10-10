/* TDMA-SEAT-01 first-class receive and T2 freeze completion.
 *
 * Followers take the origin's first-class records from the priority lane: one
 * Core1 capture per ring frame, instead of the ordinary flight station that
 * samples one frame per several ring frames. A follower then reports the
 * positions it has frozen as a state in the control bytes of every image it
 * sends; one ring loop carries that state back to the origin, which keeps
 * resending a FREEZE until every target reports it. */
#include "tdma_priority_rx.h"

/* opcode << 8 | frozen mask; zero while no follower T2 run is active. */
static uint32_t s_position_local_frozen;
static uint32_t s_position_freeze_ack[DISTRIBUTED_REFMEM_NODE_COUNT];
/* Set by the first accepted lane record: Core0 then leaves T2 to the lane so
 * each RX mailbox and FREEZE latch keeps a single producer. */
static uint32_t s_position_priority_owned;
static uint8_t s_position_priority_last[DISTRIBUTED_REFMEM_NODE_COUNT]
    [TRIGGER_POSITION_SLICE_WIRE_SIZE];
static uint32_t s_position_priority_accept_count, s_position_priority_duplicate_count,
    s_position_priority_reject_count;

static inline uint32_t position_ack_opcode(uint32_t run_id)
{
    return TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN | (run_id & 0x0Fu);
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(distributed_refmem_note_position_frozen)(
    uint32_t run_id, bool active, uint32_t frozen_mask)
{
    __atomic_store_n(&s_position_local_frozen, active ?
        (position_ack_opcode(run_id) << 8u) | (frozen_mask & 0xFFu) : 0u, __ATOMIC_RELEASE);
}

uint32_t DISTRIBUTED_REFMEM_TIME_CRITICAL(distributed_refmem_position_freeze_acked)(
    uint32_t run_id, uint32_t target_mask)
{
    const uint32_t opcode = position_ack_opcode(run_id);
    uint32_t acked = target_mask ? 0xFFu : 0u;
    for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_NODE_COUNT; ++slot) {
        if ((target_mask & (1u << slot)) == 0u) continue;
        const uint32_t state = __atomic_load_n(&s_position_freeze_ack[slot], __ATOMIC_ACQUIRE);
        if ((state >> 8u) != opcode) return 0u;
        acked &= state & 0xFFu;
    }
    return acked;
}

static bool distributed_refmem_position_priority_owned(void)
{
    return __atomic_load_n(&s_position_priority_owned, __ATOMIC_ACQUIRE) != 0u;
}

/* Core0 image build: the follower's frozen state rides every ordinary image. */
static void distributed_refmem_position_ack_patch(uint8_t *mailbox)
{
    const uint32_t state = __atomic_load_n(&s_position_local_frozen, __ATOMIC_ACQUIRE);
    if (!state || !tdma_process_image_transport_class_valid(mailbox[3])) return;
    mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] = (uint8_t)(state >> 8u);
    mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET] = (uint8_t)state;
}

/* Core0 receive: latest reported state per follower; absence keeps the last. */
static void distributed_refmem_position_ack_receive(uint32_t slot, const uint8_t *mailbox)
{
    const uint8_t opcode = mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET];
    if (slot >= DISTRIBUTED_REFMEM_NODE_COUNT ||
        (opcode & TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN_MASK) !=
            TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN ||
        distributed_refmem_get_le16(&mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET]) !=
            tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET)) return;
    __atomic_store_n(&s_position_freeze_ack[slot], ((uint32_t)opcode << 8u) |
        mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET], __ATOMIC_RELEASE);
}

/* Stopped boundary: no lane, no Core0 receive and no T2 run is live. */
static void distributed_refmem_position_ack_reset(void)
{
    for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_NODE_COUNT; ++slot)
        __atomic_store_n(&s_position_freeze_ack[slot], 0u, __ATOMIC_RELEASE);
    memset(s_position_priority_last, 0, sizeof(s_position_priority_last));
    __atomic_store_n(&s_position_priority_owned, 0u, __ATOMIC_RELEASE);
}

/* Core1 priority-lane receiver, inside the capture IRQ. The lane already
 * validated the header, mailbox CRC, the reference source and our target bit.
 * The origin keeps one record on the wire for consecutive frames: repeat wire
 * bytes are one record. A record not staged (mailbox busy) is not remembered,
 * so its next repeat retries. */
void DISTRIBUTED_REFMEM_TIME_CRITICAL(distributed_refmem_position_priority_rx)(
    const tdma_priority_rx_record_t *record)
{
    if (!record) {
        __atomic_store_n(&s_position_priority_owned, 0u, __ATOMIC_RELEASE);
        return;
    }
    const uint8_t *mailbox = record->mailbox;
    const uint32_t source = mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET];
    if (mailbox[3] != TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS ||
        source >= DISTRIBUTED_REFMEM_NODE_COUNT) return;
    __atomic_store_n(&s_position_priority_owned, 1u, __ATOMIC_RELEASE);
    const uint8_t *wire = &mailbox[TDMA_PROCESS_IMAGE_REFMEM_OFFSET];
    uint8_t *last = s_position_priority_last[source];
    bool repeat = true;
    for (uint32_t i = 0u; i < TRIGGER_POSITION_SLICE_WIRE_SIZE; ++i)
        repeat = repeat && wire[i] == last[i];
    if (repeat) {
        ++s_position_priority_duplicate_count;
        return;
    }
    trigger_position_slice_t slice;
    bool staged = false;
    if (trigger_position_slice_decode(wire, &slice)) {
        if (slice.stage == TRIGGER_POSITION_SLICE_FREEZE) {
            staged = trigger_position_freeze_latch_put(&s_position_freeze_latches[source],
                slice.position_id, slice.prediction_version);
        } else {
            const trigger_position_descriptor_t received = {
                .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
                .stage = TRIGGER_POSITION_STAGE_PREVIEW,
                .position_id = slice.position_id,
                .prediction_version = slice.prediction_version,
                .target_ms_index = slice.target_ms_index,
                .target_fine_slice_index = slice.target_fine_slice_index,
            };
            staged = trigger_position_mailbox_stage(&s_position_rx_mailboxes[source],
                &received, mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET]);
        }
    }
    if (!staged) {
        ++s_position_priority_reject_count;
        return;
    }
    for (uint32_t i = 0u; i < TRIGGER_POSITION_SLICE_WIRE_SIZE; ++i) last[i] = wire[i];
    ++s_position_priority_accept_count;
}
