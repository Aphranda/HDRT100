/* TDMA-SEAT-01 first-class receive and T2 freeze completion.
 *
 * Followers take the origin's first-class records from the priority lane: one
 * Core1 capture per ring frame, instead of the ordinary flight station that
 * samples one frame per several ring frames. A follower then reports the
 * positions it has frozen as a state in the control bytes of every image it
 * sends; one ring loop carries that state back to the origin, which keeps
 * resending a FREEZE until every target reports it. */
#include "tdma_priority_rx.h"

/* Bounded service for a held first-class record: consecutive drain attempts
 * before it is retired and counted as expired. Declared here (guarded) so every
 * translation unit that compiles this receiver — including host harnesses that
 * do not include distributed_refmem.h — shares one budget. */
#ifndef DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS
#define DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS 4u
#endif

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
/* Slice decode failures: the lane validated the frame but the first-class slice
 * itself is not decodable, so the record is lost (unrecoverable). */
static uint32_t s_position_priority_decode_reject_count;
/* TDMA-SEAT-03 bounded receive preservation: PREVIEWs refused because the
 * single-depth mailbox is still unconsumed are queued per source (append-only
 * SPSC ring, producer = capture IRQ, consumer = T2 phase on the same core) and
 * retried by the T2 phase on following slices. Recovery therefore never depends
 * on the origin repeating the record on the wire. The ring never overwrites a
 * live entry: a full ring drops the newest arrival and counts it explicitly.
 * An entry already delivered by a wire repeat is marked consumed, not removed. */
#ifndef DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH
#define DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH 4u
#endif
typedef struct {
    trigger_position_slice_t slice; /* prediction_version 0 marks consumed */
    uint32_t target_mask;
    uint32_t attempts; /* consecutive bounded drain attempts for this entry */
} distributed_refmem_position_hold_entry_t;
static distributed_refmem_position_hold_entry_t
    s_position_priority_hold[DISTRIBUTED_REFMEM_NODE_COUNT]
                            [DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH];
static volatile uint32_t s_position_priority_hold_head[DISTRIBUTED_REFMEM_NODE_COUNT];
static volatile uint32_t s_position_priority_hold_tail[DISTRIBUTED_REFMEM_NODE_COUNT];
static uint32_t s_position_priority_hold_count, s_position_priority_hold_dropped_count;
/* Queued copies retired because the very record reached the consumer by the
 * wire first (the lane marks the queued copy with prediction_version 0). The
 * record itself is not lost, but the queue must still account for the exit:
 * without this counter hold_count could not be reconciled with what is queued. */
static uint32_t s_position_priority_hold_delivered_count;
/* FREEZE never queues (it goes to the per-position latch), so the PREVIEW-oriented lane
 * counters cannot say whether a FREEZE reached a follower. These three can: latched by the
 * lane, kept-first (a FREEZE for an id whose previous latch was not yet taken is NOT stored),
 * and taken by the T2 consumer. latched == taken + still pending; kept is the silent loss. */
static uint32_t s_position_freeze_latched_count;
static uint32_t s_position_freeze_kept_count;
static uint32_t s_position_freeze_taken_count;
static inline void distributed_refmem_position_note_freeze_taken(void)
{
    ++s_position_freeze_taken_count;
}
static uint32_t s_position_priority_drain_count, s_position_priority_drain_fail_count;
static uint32_t s_position_priority_hold_expired_count;
/* Diagnostic patches that lost the control bytes to the first-class fact. */
static uint32_t s_position_ack_override_count;
/* Diagnostic control writes refused up front because the first-class fact owns
 * the shared control bytes for this image. The fast channel is shared by both
 * seats (owner ruling: one channel, time-multiplexed), so this admission is the
 * permanent mechanism, not a transition. */
static uint32_t s_position_diag_refused_count;

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

/* Why completion has not happened, per target slot. The origin only reports how
 * many times it resent, which cannot distinguish "the follower never got the
 * FREEZE" from "the follower froze it but its report does not match this run".
 * stale = the slot's reported opcode is not this run's (another run id, or the
 * slot never reported at all); incomplete = the opcode matches but this run's
 * frozen mask does not carry the position. Both are per-slot bits in
 * target_mask coordinates; a zero pair means everything needed has reported. */
void DISTRIBUTED_REFMEM_TIME_CRITICAL(distributed_refmem_position_ack_pending)(
    uint32_t run_id, uint32_t target_mask, uint32_t *stale_mask, uint32_t *incomplete_mask)
{
    const uint32_t opcode = position_ack_opcode(run_id);
    uint32_t stale = 0u, all_frozen = 0xFFu;
    for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_NODE_COUNT; ++slot) {
        if ((target_mask & (1u << slot)) == 0u) continue;
        const uint32_t state = __atomic_load_n(&s_position_freeze_ack[slot], __ATOMIC_ACQUIRE);
        if ((state >> 8u) != opcode)
            stale |= 1u << slot;                 /* wrong run / never reported */
        else
            all_frozen &= state & 0xFFu;         /* positions this run已冻结 */
    }
    if (stale_mask) *stale_mask = stale;
    /* Position bits no reporting board has frozen. While `stale` is non-zero this
     * only covers the boards that do speak this run; the stale set is the first
     * thing to fix. */
    if (incomplete_mask) *incomplete_mask = (uint32_t)(~all_frozen) & 0xFFu;
}

static bool distributed_refmem_position_priority_owned(void)
{
    return __atomic_load_n(&s_position_priority_owned, __ATOMIC_ACQUIRE) != 0u;
}

/* Core0 image build admission (TDMA-SEAT-03): the fast channel is shared by
 * both seats, so admission is permanent. While this node must
 * publish a first-class frozen state, the two shared control bytes belong to that
 * fact, so the action diagnostic may not claim them at all. Payload fragments are
 * unaffected by this decision. A refusal is counted here, so a suppressed
 * diagnostic control byte is an explicit fact; because the diagnostic never wins
 * the bytes, an accepted image no longer increments the override counter. */
static bool distributed_refmem_position_ack_control_available(uint8_t mailbox_class)
{
    if (__atomic_load_n(&s_position_local_frozen, __ATOMIC_ACQUIRE) == 0u ||
        !tdma_process_image_transport_class_valid(mailbox_class)) return true;
    ++s_position_diag_refused_count;
    return false;
}

/* Core0 image build: the follower's frozen state rides every ordinary image.
 * It is written after any diagnostic patch, so a competing opcode is replaced
 * rather than silently swallowing the first-class fact; each replacement is
 * counted. */
static void distributed_refmem_position_ack_patch(uint8_t *mailbox)
{
    const uint32_t state = __atomic_load_n(&s_position_local_frozen, __ATOMIC_ACQUIRE);
    if (!state || !tdma_process_image_transport_class_valid(mailbox[3])) return;
    const uint8_t previous = mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET];
    if (previous != TDMA_PROCESS_IMAGE_CONTROL_OPCODE_NONE &&
        (previous & TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN_MASK) !=
            TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN)
        ++s_position_ack_override_count;
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
    memset(s_position_priority_hold, 0, sizeof(s_position_priority_hold));
    for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_NODE_COUNT; ++slot) {
        __atomic_store_n(&s_position_priority_hold_head[slot], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&s_position_priority_hold_tail[slot], 0u, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&s_position_priority_owned, 0u, __ATOMIC_RELEASE);
}

/* Core1 priority-lane receiver, inside the capture IRQ. The lane already
 * validated the header, mailbox CRC, the reference source and our target bit.
 * The origin keeps one record on the wire for consecutive frames: repeat wire
 * bytes are one record. A PREVIEW refused by the single-depth mailbox is held
 * for the next T2 slice instead of being dropped; FREEZE goes to the latch. */
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
    const uint32_t target_mask = mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET];
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
    const bool decoded = trigger_position_slice_decode(wire, &slice);
    bool staged = false;
    if (decoded) {
        if (slice.stage == TRIGGER_POSITION_SLICE_FREEZE) {
            const bool was_latched = slice.position_id >= 1u &&
                slice.position_id <= TRIGGER_POSITION_FREEZE_LATCH_IDS &&
                (__atomic_load_n(&s_position_freeze_latches[source].mask, __ATOMIC_ACQUIRE) &
                 (1u << (slice.position_id - 1u))) != 0u;
            staged = trigger_position_freeze_latch_put(&s_position_freeze_latches[source],
                slice.position_id, slice.prediction_version, slice.target_ms_index);
            if (staged) {
                if (was_latched) ++s_position_freeze_kept_count;
                else ++s_position_freeze_latched_count;
            }
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
                &received, target_mask);
        }
    }
    if (!staged) {
        if (!decoded) {
            /* The lane validated the frame (header, identity, mailbox CRC), but
             * the compact first-class slice itself does not decode. That is an
             * unrecoverable loss and is counted on its own, never mixed into the
             * recoverable backpressure count below. */
            ++s_position_priority_decode_reject_count;
            return;
        }
        /* Decoded but refused by the single-depth mailbox: bounded backpressure,
         * queued for the following slices. */
        ++s_position_priority_reject_count;
        /* Queue a refused PREVIEW for the following slices. Append-only: a full
         * ring drops this arrival instead of overwriting a live entry. */
        if (slice.stage == TRIGGER_POSITION_SLICE_PREVIEW &&
            target_mask != 0u) {
            const uint32_t tail = __atomic_load_n(
                &s_position_priority_hold_tail[source], __ATOMIC_RELAXED);
            const uint32_t head = __atomic_load_n(
                &s_position_priority_hold_head[source], __ATOMIC_ACQUIRE);
            if (tail - head >= DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH) {
                ++s_position_priority_hold_dropped_count;
            } else {
                distributed_refmem_position_hold_entry_t *const entry =
                    &s_position_priority_hold[source]
                        [tail % DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH];
                entry->slice = slice;
                entry->target_mask = target_mask;
                entry->attempts = 0u;
                __atomic_store_n(&s_position_priority_hold_tail[source],
                                 tail + 1u, __ATOMIC_RELEASE);
                ++s_position_priority_hold_count;
            }
        }
        return;
    }
    /* Delivered: a queued copy of this very record is obsolete. Mark it consumed
     * so the drain skips it instead of delivering a duplicate later. */
    {
        const uint32_t head = __atomic_load_n(
            &s_position_priority_hold_head[source], __ATOMIC_ACQUIRE);
        const uint32_t tail = __atomic_load_n(
            &s_position_priority_hold_tail[source], __ATOMIC_ACQUIRE);
        for (uint32_t index = head; index != tail; ++index) {
            distributed_refmem_position_hold_entry_t *const entry =
                &s_position_priority_hold[source]
                    [index % DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH];
            if (entry->slice.position_id == slice.position_id &&
                entry->slice.prediction_version == slice.prediction_version) {
                entry->slice.prediction_version = 0u;
                break;
            }
        }
    }
    for (uint32_t i = 0u; i < TRIGGER_POSITION_SLICE_WIRE_SIZE; ++i) last[i] = wire[i];
    ++s_position_priority_accept_count;
}

/* Core1 T2 phase: retry queued first-class records through the normal mailbox.
 * Returns true when a record was staged, so the caller can take it in the same
 * slice. Runs outside the capture IRQ but on the same core; the ring is
 * append-only, so a live entry is never overwritten while being read. Consumed
 * entries are skipped; an entry whose bounded retry budget is exhausted is
 * retired and counted, so the seat never waits or retries without bound.
 * Followers reach it on every slice with nothing staged: keep it out of XIP. */
bool DISTRIBUTED_REFMEM_TIME_CRITICAL(distributed_refmem_position_priority_drain)(uint32_t source_slot)
{
    if (source_slot >= DISTRIBUTED_REFMEM_NODE_COUNT) return false;
    uint32_t head = __atomic_load_n(&s_position_priority_hold_head[source_slot],
                                    __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&s_position_priority_hold_tail[source_slot],
                                          __ATOMIC_ACQUIRE);
    bool advanced = false;
    while (head != tail) {
        distributed_refmem_position_hold_entry_t *const entry =
            &s_position_priority_hold[source_slot]
                [head % DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH];
        if (entry->slice.prediction_version == 0u) { /* delivered already */
            ++head;
            advanced = true;
            ++s_position_priority_hold_delivered_count;
            continue;
        }
        if (entry->slice.stage != TRIGGER_POSITION_SLICE_PREVIEW ||
            entry->target_mask == 0u) {
            ++head;
            advanced = true;
            continue;
        }
        const trigger_position_descriptor_t received = {
            .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
            .stage = TRIGGER_POSITION_STAGE_PREVIEW,
            .position_id = entry->slice.position_id,
            .prediction_version = entry->slice.prediction_version,
            .target_ms_index = entry->slice.target_ms_index,
            .target_fine_slice_index = entry->slice.target_fine_slice_index,
        };
        if (!trigger_position_mailbox_stage(&s_position_rx_mailboxes[source_slot],
                                            &received, entry->target_mask)) {
            ++s_position_priority_drain_fail_count;
            if (++entry->attempts >= DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS) {
                ++head; /* retire the stale entry and try the next one */
                advanced = true;
                ++s_position_priority_hold_expired_count;
                continue;
            }
            break; /* keep it for a later slice */
        }
        ++head;
        advanced = true;
        ++s_position_priority_drain_count;
        __atomic_store_n(&s_position_priority_hold_head[source_slot], head,
                         __ATOMIC_RELEASE);
        return true;
    }
    if (advanced)
        __atomic_store_n(&s_position_priority_hold_head[source_slot], head,
                         __ATOMIC_RELEASE);
    return false;
}
