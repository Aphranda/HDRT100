"""TDMA-SEAT-01 first-class receive and T2 freeze completion.

The follower's priority-lane receiver stages every distinct origin T2 record,
treats wire repeats as one record and retries an unstaged one; frozen state
rides the follower's images and the origin sees completion per run.
"""
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid
from refmem_units import body as refmem_unit_body

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "tdma_flight_engine.h"
#include "tdma_priority_rx.h"
#include "tdma_process_image_layout.h"
#include "trigger_position_mailbox.h"
#include "trigger_position_slice.h"
#define DISTRIBUTED_REFMEM_NODE_COUNT 8u
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
static trigger_position_mailbox_t s_position_rx_mailboxes[DISTRIBUTED_REFMEM_NODE_COUNT];
static trigger_position_freeze_latch_t s_position_freeze_latches[DISTRIBUTED_REFMEM_NODE_COUNT];
static uint16_t distributed_refmem_get_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
void distributed_refmem_note_position_frozen(uint32_t run_id, bool active, uint32_t mask);
uint32_t distributed_refmem_position_freeze_acked(uint32_t run_id, uint32_t targets);
void distributed_refmem_position_priority_rx(const tdma_priority_rx_record_t *record);
#include "INC"

static void seal(uint8_t *m)
{
    const uint16_t crc = tdma_process_image_crc16_ccitt(m, TDMA_PROCESS_IMAGE_CRC_OFFSET);
    m[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
    m[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
}
static tdma_priority_rx_record_t record(uint8_t cls, uint32_t position, uint32_t version,
                                        bool freeze, uint32_t target)
{
    tdma_priority_rx_record_t r = {.epoch = 1u};
    uint8_t *m = r.mailbox;
    m[0] = (uint8_t)TDMA_FLIGHT_MAILBOX_MAGIC; m[1] = (uint8_t)(TDMA_FLIGHT_MAILBOX_MAGIC >> 8u);
    m[2] = TDMA_FLIGHT_MAILBOX_VERSION; m[3] = cls;
    m[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] = 0u;
    m[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 0x0eu;
    const trigger_position_slice_t slice = {.position_id = (uint8_t)position,
        .prediction_version = (uint16_t)version, .target_ms_index = target,
        .stage = freeze ? TRIGGER_POSITION_SLICE_FREEZE : TRIGGER_POSITION_SLICE_PREVIEW};
    assert(trigger_position_slice_encode(&slice, m + TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET));
    seal(m);
    return r;
}
static bool take(trigger_position_descriptor_t *d)
{
    uint32_t mask;
    return trigger_position_freeze_latch_take(&s_position_freeze_latches[0], d) ||
        trigger_position_mailbox_take(&s_position_rx_mailboxes[0], d, &mask);
}

int main(void)
{
    trigger_position_descriptor_t d;
    /* Another class on the lane is not ours and does not claim T2. */
    tdma_priority_rx_record_t vdc = record(TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS, 1, 1, false, 5);
    distributed_refmem_position_priority_rx(&vdc);
    assert(!distributed_refmem_position_priority_owned() && !take(&d));

    /* A PREVIEW is staged once; its wire repeats on later frames are one record. */
    tdma_priority_rx_record_t p1 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 1, 3, false, 40);
    distributed_refmem_position_priority_rx(&p1);
    distributed_refmem_position_priority_rx(&p1);
    assert(distributed_refmem_position_priority_owned());
    assert(s_position_priority_accept_count == 1u && s_position_priority_duplicate_count == 1u);

    /* Mailbox still busy: the next PREVIEW is refused (and now held). */
    tdma_priority_rx_record_t p2 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 2, 4, false, 60);
    distributed_refmem_position_priority_rx(&p2);
    assert(s_position_priority_reject_count == 1u);
    const uint32_t holds = s_position_priority_hold_count;
    assert(holds == 1u);
    /* A FREEZE never waits behind it, and overtakes it. */
    tdma_priority_rx_record_t f1 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 1, 3, true, 0);
    distributed_refmem_position_priority_rx(&f1);
    distributed_refmem_position_priority_rx(&f1);
    assert(take(&d) && d.stage == TRIGGER_POSITION_STAGE_FREEZE && d.position_id == 1u && d.prediction_version == 3u);
    assert(take(&d) && d.stage == TRIGGER_POSITION_STAGE_PREVIEW && d.position_id == 1u && d.target_ms_index == 40u);
    /* The wire repeat still works, and delivering the same record retires the
     * held copy so nothing stale is re-delivered later. */
    distributed_refmem_position_priority_rx(&p2);
    assert(take(&d) && d.position_id == 2u && d.prediction_version == 4u && !take(&d));
    assert(!distributed_refmem_position_priority_drain(0u)); /* hold retired with it */

    /* Bounded receive preservation: refused PREVIEWs are queued per source and
     * delivered by the T2 phase on following slices, in arrival order, even when
     * the origin never repeats them. */
    tdma_priority_rx_record_t a1 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 3, 10, false, 100);
    distributed_refmem_position_priority_rx(&a1);
    tdma_priority_rx_record_t a2 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 4, 11, false, 120);
    distributed_refmem_position_priority_rx(&a2);
    assert(s_position_priority_hold_count == 2u);
    tdma_priority_rx_record_t a3 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 4, 12, false, 130);
    distributed_refmem_position_priority_rx(&a3);
    assert(s_position_priority_hold_count == 3u);
    /* While the mailbox is still full the drain fails and keeps both queued. */
    assert(!distributed_refmem_position_priority_drain(0u) &&
           s_position_priority_drain_fail_count == 1u);
    assert(take(&d) && d.position_id == 3u && d.prediction_version == 10u);
    assert(distributed_refmem_position_priority_drain(0u) &&
           s_position_priority_drain_count == 1u);
    assert(take(&d) && d.position_id == 4u && d.prediction_version == 11u &&
           d.target_ms_index == 120u);   /* arrival order, not newest-first */
    assert(distributed_refmem_position_priority_drain(0u) &&
           s_position_priority_drain_count == 2u);
    assert(take(&d) && d.position_id == 4u && d.prediction_version == 12u &&
           d.target_ms_index == 130u);
    assert(!take(&d) && !distributed_refmem_position_priority_drain(0u));

    /* A queued copy delivered by a wire repeat is marked consumed, so the drain
     * does not stage the same record twice. */
    tdma_priority_rx_record_t b1 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 7, 20, false, 200);
    distributed_refmem_position_priority_rx(&b1);          /* staged */
    tdma_priority_rx_record_t b2 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 8, 21, false, 210);
    distributed_refmem_position_priority_rx(&b2);          /* queued */
    assert(take(&d) && d.position_id == 7u);
    distributed_refmem_position_priority_rx(&b2);          /* wire repeat: staged */
    assert(s_position_priority_accept_count >= 5u);
    assert(take(&d) && d.position_id == 8u);
    /* The wire repeat delivered the record the queue was still holding. Retiring
     * that copy stages nothing (drain() reports what it staged, not what it
     * retired), but the exit is counted, so the queue identity closes. */
    const uint32_t delivered_before = s_position_priority_hold_delivered_count;
    assert(!distributed_refmem_position_priority_drain(0u));
    assert(s_position_priority_hold_delivered_count == delivered_before + 1u);
    assert(s_position_priority_hold_count == s_position_priority_drain_count +
           s_position_priority_hold_expired_count +
           s_position_priority_hold_delivered_count);   /* nothing left queued */
    assert(!take(&d));

    /* The queue is bounded: a full queue drops the newest refusal explicitly
     * instead of overwriting a live entry. Occupy the mailbox first so every
     * arrival below is a refusal. */
    tdma_priority_rx_record_t busy = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 6, 19, false, 190);
    distributed_refmem_position_priority_rx(&busy);
    const uint32_t dropped = s_position_priority_hold_dropped_count;
    for (uint32_t i = 0u; i <= DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH; ++i) {
        tdma_priority_rx_record_t full = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS,
            (uint8_t)(9u + i), (uint16_t)(30u + i), false, 300u + i);
        distributed_refmem_position_priority_rx(&full);
    }
    assert(s_position_priority_hold_dropped_count == dropped + 1u);

    /* The retry is bounded: an entry the consumer never takes is retired at the
     * declared attempt budget and counted, never retried forever. */
    const uint32_t expired = s_position_priority_hold_expired_count;
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS; ++i)
        assert(!distributed_refmem_position_priority_drain(0u));
    assert(s_position_priority_hold_expired_count >= expired + 1u);

    /* Release the occupied slot and clear the queue, so the STOP case below
     * starts from a clean receiver instead of inheriting this block's state. */
    assert(take(&d) && d.position_id == 6u && d.prediction_version == 19u);
    while (take(&d)) { }
    distributed_refmem_position_ack_reset();
    assert(!distributed_refmem_position_priority_drain(0u));

    /* Follower state rides every ordinary image; the origin sees it per run. */
    uint8_t image[TDMA_FLIGHT_SHORT_SLOT_SIZE] = {0};
    image[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    distributed_refmem_position_ack_patch(image);
    assert(image[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] == TDMA_PROCESS_IMAGE_CONTROL_OPCODE_NONE);
    distributed_refmem_note_position_frozen(0x1234u, true, 0x5u);
    distributed_refmem_position_ack_patch(image);
    seal(image);
    distributed_refmem_position_ack_receive(1u, image);
    distributed_refmem_position_ack_receive(2u, image);
    assert(distributed_refmem_position_freeze_acked(0x1234u, 0x6u) == 0x5u);
    assert(!distributed_refmem_position_freeze_acked(0x1234u, 0xeu)); /* slot 3 not reported */
    assert(!distributed_refmem_position_freeze_acked(0x1235u, 0x6u)); /* another run */
    /* A corrupt image never moves the state; absence keeps the last report. */
    distributed_refmem_note_position_frozen(0x1234u, true, 0x7u);
    distributed_refmem_position_ack_patch(image);
    distributed_refmem_position_ack_receive(1u, image);
    assert(distributed_refmem_position_freeze_acked(0x1234u, 0x2u) == 0x5u);
    /* The T2 frozen class and priority images carry no follower state. */
    uint8_t t2[TDMA_FLIGHT_SHORT_SLOT_SIZE] = {0};
    t2[3] = TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS;
    distributed_refmem_position_ack_patch(t2);
    assert(!t2[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET]);

    /* A diagnostic may claim the control bytes first; the delivery-critical
     * first-class fact is written after it and the override is counted. */
    distributed_refmem_note_position_frozen(0x1234u, true, 0x5u);
    uint8_t contested[TDMA_FLIGHT_SHORT_SLOT_SIZE] = {0};
    contested[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    contested[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] = 0xD1u; /* probe request */
    contested[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET] = 7u;
    const uint32_t overrides = s_position_ack_override_count;
    distributed_refmem_position_ack_patch(contested);
    assert(contested[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] ==
           (uint8_t)((0xE0u | (0x1234u & 0x0Fu))));
    assert(contested[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET] == 0x5u);
    assert(s_position_ack_override_count == overrides + 1u);
    /* Re-asserting our own opcode is not an override. */
    distributed_refmem_position_ack_patch(contested);
    assert(s_position_ack_override_count == overrides + 1u);

    /* Admission (TDMA-SEAT-03 A' transition): while a first-class fact must ride
     * this image, the shared control bytes are reserved for it, so the diagnostic
     * is refused up front and counted; because it never claims the bytes, the
     * first-class patch performs no override at all. */
    const uint32_t refused = s_position_diag_refused_count;
    assert(!distributed_refmem_position_ack_control_available(TDMA_PROCESS_IMAGE_MESSAGE_CLASS));
    assert(s_position_diag_refused_count == refused + 1u);
    uint8_t reserved[TDMA_FLIGHT_SHORT_SLOT_SIZE] = {0};
    reserved[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    distributed_refmem_position_ack_patch(reserved);
    assert(reserved[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] ==
           (uint8_t)(0xE0u | (0x1234u & 0x0Fu)));
    assert(s_position_ack_override_count == overrides + 1u); /* no new override */
    /* Without a first-class fact and on classes that carry none, the diagnostic
     * is admitted: the admission query never refuses a call it cannot protect. */
    distributed_refmem_note_position_frozen(0u, false, 0u);
    assert(distributed_refmem_position_ack_control_available(TDMA_PROCESS_IMAGE_MESSAGE_CLASS));
    assert(distributed_refmem_position_ack_control_available(
        TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS));
    distributed_refmem_note_position_frozen(0x1234u, true, 0x5u);
    assert(distributed_refmem_position_ack_control_available(
        TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS));
    assert(s_position_diag_refused_count == refused + 1u);

    /* WHY a FREEZE has not completed: the origin can only see a resend count, so
     * the laggards are reported per target slot. A board that never reported (or
     * that speaks another run id) is `stale`; with every board speaking this run,
     * `incomplete` carries the position bits nobody has frozen. */
    {
        uint8_t ack[TDMA_FLIGHT_SHORT_SLOT_SIZE] = {0};
        const uint32_t run = 0x1234u;
        /* Known starting state: this case asserts absolute slot sets. */
        distributed_refmem_position_ack_reset();
        const uint32_t opcode = TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN | (run & 0x0Fu);
        uint32_t stale = 0xFFFFFFFFu, incomplete = 0xFFFFFFFFu;
        /* Nothing has reported: every targeted slot is stale, and no position is
         * claimed missing - with no reporting board the missing set is unknown,
         * which is exactly why the stale mask is the actionable fact. */
        distributed_refmem_position_ack_pending(run, 0x6u, &stale, &incomplete);
        assert(stale == 0x6u && incomplete == 0u);
        /* Slot 1 reports this run and positions 1+3 frozen. */
        ack[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] = (uint8_t)opcode;
        ack[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET] = 0x5u;
        const uint16_t crc = tdma_process_image_crc16_ccitt(ack,
            TDMA_PROCESS_IMAGE_CRC_OFFSET);
        ack[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
        ack[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
        distributed_refmem_position_ack_receive(1u, ack);
        distributed_refmem_position_ack_pending(run, 0x6u, &stale, &incomplete);
        assert(stale == 0x4u);              /* slot 2 still silent */
        assert(incomplete == ((uint32_t)(~0x5u) & 0xFFu));   /* positions 2 and 4 */
        /* Slot 2 answers with ANOTHER run's opcode: it stays stale, and the
         * incomplete set only covers the boards that do speak this run. */
        ack[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] =
            (uint8_t)(TDMA_PROCESS_IMAGE_CONTROL_OPCODE_T2_FROZEN | 0x2u);
        const uint16_t crc2 = tdma_process_image_crc16_ccitt(ack,
            TDMA_PROCESS_IMAGE_CRC_OFFSET);
        ack[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc2;
        ack[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc2 >> 8u);
        distributed_refmem_position_ack_receive(2u, ack);
        distributed_refmem_position_ack_pending(run, 0x6u, &stale, &incomplete);
        assert(stale == 0x4u);
        /* And the completion predicate agrees that nothing is complete. */
        assert(distributed_refmem_position_freeze_acked(run, 0x6u) == 0u);
    }

    /* A frame the lane validated whose compact slice does not decode is an
     * unrecoverable first-class loss: counted on its own, never mixed into the
     * recoverable backpressure count, and never queued. */
    const uint32_t decode_rejects = s_position_priority_decode_reject_count;
    const uint32_t backpressure_rejects = s_position_priority_reject_count;
    const uint32_t held_before = s_position_priority_hold_count;
    const uint32_t accepted_before = s_position_priority_accept_count;
    tdma_priority_rx_record_t undecodable =
        record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 5u, 40u, false, 400u);
    /* Clear the slice tag bits: the frame itself is well formed, but the compact
     * slice cannot decode. */
    undecodable.mailbox[TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET] = 0x1Fu;
    distributed_refmem_position_priority_rx(&undecodable);
    assert(s_position_priority_decode_reject_count == decode_rejects + 1u);
    assert(s_position_priority_reject_count == backpressure_rejects);
    assert(s_position_priority_hold_count == held_before);
    assert(s_position_priority_accept_count == accepted_before); /* nothing accepted */
    assert(!take(&d));

    /* STOP: no stale completion, lane ownership or repeat memory survives. */
    distributed_refmem_note_position_frozen(0u, false, 0u);
    distributed_refmem_position_ack_reset();
    assert(!distributed_refmem_position_freeze_acked(0x1234u, 0x2u));
    assert(!distributed_refmem_position_priority_owned());
    distributed_refmem_position_priority_rx(&p1);
    assert(take(&d) && d.position_id == 1u);
    distributed_refmem_position_priority_rx(NULL);
    assert(!distributed_refmem_position_priority_owned());
    return 0;
}
'''


def test_first_class_lane_receive_and_freeze_completion(tmp_path):
    executable = compile_grid(tmp_path, "t2-freeze-completion",
                              HARNESS.replace('#include "INC"',
                                              refmem_unit_body("distributed_refmem_position_ack")))
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
