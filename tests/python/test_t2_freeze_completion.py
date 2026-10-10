"""TDMA-SEAT-01 first-class receive and T2 freeze completion.

The follower's priority-lane receiver stages every distinct origin T2 record,
treats wire repeats as one record and retries an unstaged one; frozen state
rides the follower's images and the origin sees completion per run.
"""
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid

INC = ROOT / "components/distributed_refmem/src/distributed_refmem_position_ack.inc"

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
    assert(trigger_position_slice_encode(&slice, m + TDMA_PROCESS_IMAGE_REFMEM_OFFSET));
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

    /* Mailbox still busy: the next PREVIEW is not remembered and its repeat retries. */
    tdma_priority_rx_record_t p2 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 2, 4, false, 60);
    distributed_refmem_position_priority_rx(&p2);
    assert(s_position_priority_reject_count == 1u);
    /* A FREEZE never waits behind it, and overtakes it. */
    tdma_priority_rx_record_t f1 = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 1, 3, true, 0);
    distributed_refmem_position_priority_rx(&f1);
    distributed_refmem_position_priority_rx(&f1);
    assert(take(&d) && d.stage == TRIGGER_POSITION_STAGE_FREEZE && d.position_id == 1u && d.prediction_version == 3u);
    assert(take(&d) && d.stage == TRIGGER_POSITION_STAGE_PREVIEW && d.position_id == 1u && d.target_ms_index == 40u);
    distributed_refmem_position_priority_rx(&p2);
    assert(take(&d) && d.position_id == 2u && d.prediction_version == 4u && !take(&d));

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
                              HARNESS.replace('"INC"', '"' + INC.as_posix() + '"'))
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
