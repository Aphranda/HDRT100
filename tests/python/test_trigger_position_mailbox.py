"""Bounded cross-core handoff for T2 position descriptors."""
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_position_mailbox.h"

int main(void) {
    trigger_position_mailbox_t mailbox={0};
    trigger_position_descriptor_t value={
        .schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,.run_id=1,.sync_epoch=2,
        .position_id=3,.count_epoch=4,.prediction_version=5,
        .grid_generation=6,.deployment_generation=7,.deployment_crc=8,
        .valid_from_sync=9,.expiry_sync=10};
    trigger_position_descriptor_t received={0};
    uint32_t mask=0;
    assert(!trigger_position_mailbox_pending(&mailbox));
    assert(!trigger_position_mailbox_take(&mailbox,&received,&mask));
    assert(trigger_position_mailbox_stage(&mailbox,&value,3));
    assert(trigger_position_mailbox_pending(&mailbox));
    value.position_id=4;
    assert(!trigger_position_mailbox_stage(&mailbox,&value,3));
    assert(trigger_position_mailbox_take(&mailbox,&received,&mask));
    assert(received.position_id==3 && mask==3);
    assert(!trigger_position_mailbox_pending(&mailbox));
    assert(!trigger_position_mailbox_take(&mailbox,&received,&mask));
    assert(trigger_position_mailbox_stage(&mailbox,&value,5));
    assert(trigger_position_mailbox_take(&mailbox,&received,&mask));
    assert(received.position_id==4 && mask==5);
    value.schema=0;
    assert(!trigger_position_mailbox_stage(&mailbox,&value,1));
    return 0;
}
'''


def test_position_mailbox_single_producer_consumer(tmp_path):
    executable = compile_grid(tmp_path, "position-mailbox", HARNESS)
    result = subprocess.run([str(executable)], capture_output=True,
                            text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_refmem_owner_pulls_from_a_latest_value_table():
    """The TX boundary is no longer a mailbox: the producer writes the newest value for a
    position and the transmit side TAKES it only when the single-depth seat is free."""
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(
        encoding="utf-8")
    submit = source.split("bool distributed_refmem_stage_position_descriptor(", 1)[1].split(
        "void distributed_refmem_request_position_cancel", 1)[0]
    # Producing never hands a record over, and never depends on the wire being idle.
    assert "trigger_position_mailbox_stage(&s_position_tx_mailbox" not in submit
    assert "s_position_pending[" in submit
    assert "trigger_position_slice_encode" in submit
    assert "position_tx_active" not in submit
    assert "trigger_position_descriptor_encode" not in submit
    prepare = source.split("static bool distributed_refmem_position_tx_prepare(", 1)[1].split(
        "static void distributed_refmem_tdma_flight_sync_publish", 1)[0]
    # The transmit side takes only what it can board right now.
    assert "tdma_service_first_class_free(owner)" in prepare
    assert "trigger_position_mailbox_take(&s_position_tx_mailbox" not in prepare
    assert "s_position_pending[" in prepare
    assert "trigger_position_slice_encode" in prepare
    assert "DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_DRAIN_PACE" in prepare
    # FREEZE outranks PREVIEW, and one record carries the next position too.
    assert "TRIGGER_POSITION_SLICE_FREEZE" in prepare
    assert "next_position_id" in prepare
    assert "next_target_ms_index" in prepare
    # Receiving still crosses the bounded mailbox boundary.
    receive = source.split("static void distributed_refmem_tdma_parse_position_fragment(", 1)[1].split(
        "static void distributed_refmem_tdma_flight_parse_mailbox", 1)[0]
    assert "trigger_position_mailbox_stage(" in receive
    assert "target_mask & (1u << s_tdma_flight_sync.local_slot)" in receive


def test_production_position_merge_priority_and_drain(tmp_path):
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(
        encoding="utf-8")
    functions = (
        ("bool", "distributed_refmem_stage_position_descriptor",
         "const trigger_position_descriptor_t *descriptor, uint32_t target_mask"),
        ("void", "distributed_refmem_request_position_cancel", "void"),
        ("void", "distributed_refmem_position_cancel_service", "void"),
        ("bool", "distributed_refmem_position_descriptor_pending", "void"),
        ("bool", "distributed_refmem_position_tx_prepare", "tdma_service_service_t *owner"),
    )
    bodies = "\n".join(
        f"{result} {name}({arguments}) {{" + c_definition_body(source, name) + "}\n"
        for result, name, arguments in functions
    )
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "trigger_position_descriptor.h"
#include "trigger_position_slice.h"

/* The single-depth first-class seat, reduced to the one fact the taker asks about. */
typedef struct tdma_service_service { int unused; } tdma_service_service_t;
static bool s_seat_free = true;
bool tdma_service_first_class_free(const tdma_service_service_t *service)
{ (void)service; return s_seat_free; }
static uint32_t s_now_ms;
uint32_t osal_tick_ms(void) { return s_now_ms; }

#define DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS 3u
enum {
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_IDLE = 0u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_STAGED = 1u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_RESOURCE = 6u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE = 0u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_SEAT_BUSY = 8u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_DRAIN_PACE = 9u,
};
typedef struct {
    uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE];
    uint8_t target_mask, stage, position_id, pending;
} distributed_refmem_position_pending_t;
static distributed_refmem_position_pending_t
    s_position_pending[DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS];
static uint32_t s_position_drain_ms = 6u, s_position_last_drain_ms;
static uint32_t s_position_coalesced_count;
static uint8_t s_position_drain_cursor, s_position_wait_counted;
static volatile uint32_t s_position_cancel_requested;
typedef struct {
    uint32_t position_tx_active, position_tx_target_mask;
    uint32_t position_tx_fragment_index, position_tx_sequence;
    uint32_t position_tx_reject_count, position_tx_position_id, next_seq32;
    uint32_t position_tx_phase, position_tx_wait_reason, position_tx_wait_count;
    uint8_t position_tx_wire[TRIGGER_POSITION_SLICE_WIRE_SIZE];
} tx_owner_t;
static tx_owner_t s_tdma_flight_sync;
''' + bodies + r'''
static trigger_position_descriptor_t preview(uint32_t position, uint64_t target,
                                             uint32_t version)
{
    return (trigger_position_descriptor_t){
        .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA, .run_id = 1, .sync_epoch = 2,
        .position_id = position, .count_epoch = 4, .prediction_version = version,
        .grid_generation = 6, .deployment_generation = 7, .deployment_crc = 8,
        .valid_from_sync = 9, .expiry_sync = 10, .target_ms_index = target,
        .target_fine_slice_index = 1, .stage = TRIGGER_POSITION_STAGE_PREVIEW};
}
static trigger_position_descriptor_t freeze_of(uint32_t position, uint64_t target,
                                               uint32_t version)
{
    trigger_position_descriptor_t d = preview(position, target, version);
    d.stage = TRIGGER_POSITION_STAGE_FREEZE;
    return d;
}
static tdma_service_service_t s_owner;
static bool take(trigger_position_slice_t *slice)
{
    if (!distributed_refmem_position_tx_prepare(&s_owner)) return false;
    assert(trigger_position_slice_decode(s_tdma_flight_sync.position_tx_wire, slice));
    return true;
}
static void release(void) { s_tdma_flight_sync.position_tx_active = 0u; }

int main(void)
{
    s_tdma_flight_sync.next_seq32 = 42u;
    /* 1. Producing always succeeds and a newer value for the same position REPLACES the
     *    waiting one: producing is decoupled from transmitting. */
    trigger_position_descriptor_t d = preview(1u, 100u, 1u);
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    s_tdma_flight_sync.position_tx_active = 1u;                  /* seat busy */
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    s_tdma_flight_sync.position_tx_active = 0u;
    d = preview(1u, 200u, 2u);
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    assert(s_position_coalesced_count == 2u);                    /* merged, not lost */
    /* 2. The taker pulls nothing while the seat is still occupied ... */
    s_seat_free = false;
    trigger_position_slice_t slice;
    assert(!take(&slice));
    assert(!s_tdma_flight_sync.position_tx_active);
    assert(s_tdma_flight_sync.position_tx_wait_reason ==
           DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_SEAT_BUSY);
    const uint32_t waits = s_tdma_flight_sync.position_tx_wait_count;
    assert(!take(&slice));
    assert(s_tdma_flight_sync.position_tx_wait_count == waits);  /* one fact, not per pass */
    /* ... and takes the NEWEST value once it is free. */
    s_seat_free = true;
    assert(take(&slice));
    assert(slice.position_id == 1u && slice.target_ms_index == 200u &&
           slice.stage == TRIGGER_POSITION_SLICE_PREVIEW);
    assert(s_tdma_flight_sync.position_tx_sequence == 42u);
    release();
    /* 3. Fixed draining period: one PREVIEW per T_drain, the value waits instead of being
     *    refused. A FREEZE is never paced. */
    s_tdma_flight_sync.position_tx_active = 0u;
    s_now_ms = 1000u;
    s_position_last_drain_ms = 1000u;
    d = preview(2u, 300u, 3u);
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    assert(!take(&slice));
    assert(s_tdma_flight_sync.position_tx_wait_reason ==
           DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_DRAIN_PACE);
    s_now_ms += 6u;
    assert(take(&slice));
    assert(slice.target_ms_index == 300u);
    release();
    /* 4. A waiting FREEZE outranks a PREVIEW, whichever position it belongs to. */
    s_now_ms += 100u;
    d = preview(2u, 400u, 4u);
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    d = freeze_of(1u, 500u, 5u);
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    assert(take(&slice));
    assert(slice.stage == TRIGGER_POSITION_SLICE_FREEZE && slice.position_id == 1u &&
           slice.target_ms_index == 500u);
    release();
    /* ... and the PREVIEW that was overtaken is still there, not discarded. */
    s_now_ms += 100u;
    assert(take(&slice));
    assert(slice.stage == TRIGGER_POSITION_SLICE_PREVIEW && slice.position_id == 2u &&
           slice.target_ms_index == 400u);
    release();
    /* 5. ONE record carries TWO predicted positions: the second pending position rides in
     *    bytes 10..15 of the same slice. */
    s_now_ms += 100u;
    d = preview(1u, 600u, 6u);
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    d = preview(2u, 700u, 7u);
    assert(distributed_refmem_stage_position_descriptor(&d, 3u));
    assert(take(&slice));
    assert(slice.position_id == 1u && slice.target_ms_index == 600u);
    assert(slice.next_position_id == 2u && slice.next_target_ms_index == 700u);
    release();
    /* 6. An invalid descriptor is the only failure left, and it leaves no pending value. */
    d = preview(1u, 800u, 8u);
    d.schema = 0u;
    assert(!distributed_refmem_stage_position_descriptor(&d, 3u));
    distributed_refmem_request_position_cancel();
    assert(!distributed_refmem_stage_position_descriptor(&d, 3u));
    distributed_refmem_position_cancel_service();
    assert(!distributed_refmem_position_descriptor_pending());
    d = preview(1u, 900u, 9u);
    assert(distributed_refmem_stage_position_descriptor(&d, 9u));
    assert(distributed_refmem_position_descriptor_pending());
    distributed_refmem_request_position_cancel();
    distributed_refmem_position_cancel_service();
    assert(!distributed_refmem_position_descriptor_pending());
    return 0;
}
'''
    executable = compile_grid(tmp_path, "position-owner", harness)
    result = subprocess.run([str(executable)], capture_output=True,
                            text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
