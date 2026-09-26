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


def test_refmem_owner_uses_mailboxes_on_both_boundaries():
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(
        encoding="utf-8")
    submit = source.split("bool distributed_refmem_stage_position_descriptor(", 1)[1].split(
        "void distributed_refmem_request_position_cancel", 1)[0]
    assert "trigger_position_mailbox_stage(&s_position_tx_mailbox" in submit
    assert "trigger_position_descriptor_encode" not in submit
    prepare = source.split("static void distributed_refmem_position_tx_prepare(void)", 1)[1].split(
        "static void distributed_refmem_tdma_flight_sync_publish", 1)[0]
    assert "trigger_position_mailbox_take(&s_position_tx_mailbox" in prepare
    assert "trigger_position_descriptor_encode" in prepare
    receive = source.split("static void distributed_refmem_tdma_parse_position_fragment(", 1)[1].split(
        "static void distributed_refmem_tdma_flight_parse_mailbox", 1)[0]
    assert "trigger_position_mailbox_stage(" in receive
    assert "target_mask & (1u << s_tdma_flight_sync.local_slot)" in receive


def test_production_position_submit_prepare_and_cancel(tmp_path):
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(
        encoding="utf-8")
    functions = (
        ("bool", "distributed_refmem_stage_position_descriptor",
         "const trigger_position_descriptor_t *descriptor, uint32_t target_mask"),
        ("void", "distributed_refmem_request_position_cancel", "void"),
        ("void", "distributed_refmem_position_cancel_service", "void"),
        ("bool", "distributed_refmem_position_descriptor_pending", "void"),
        ("void", "distributed_refmem_position_tx_prepare", "void"),
    )
    bodies = "\n".join(
        f"{result} {name}({arguments}) {{" + c_definition_body(source, name) + "}\n"
        for result, name, arguments in functions
    )
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_position_mailbox.h"
typedef struct {
    uint32_t position_tx_active, position_tx_target_mask;
    uint32_t position_tx_fragment_index, position_tx_sequence;
    uint32_t position_tx_reject_count, position_tx_position_id, next_seq32;
    uint8_t position_tx_wire[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE];
} tx_owner_t;
static tx_owner_t s_tdma_flight_sync;
static trigger_position_mailbox_t s_position_tx_mailbox;
static volatile uint32_t s_position_cancel_requested;
''' + bodies + r'''
int main(void) {
    trigger_position_descriptor_t value={
        .schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,.run_id=1,.sync_epoch=2,
        .position_id=3,.count_epoch=4,.prediction_version=5,
        .grid_generation=6,.deployment_generation=7,.deployment_crc=8,
        .valid_from_sync=9,.expiry_sync=10};
    s_tdma_flight_sync.next_seq32=42;
    assert(distributed_refmem_stage_position_descriptor(&value,3));
    assert(distributed_refmem_position_descriptor_pending());
    assert(!distributed_refmem_stage_position_descriptor(&value,5));
    distributed_refmem_position_tx_prepare();
    assert(s_tdma_flight_sync.position_tx_active &&
        s_tdma_flight_sync.position_tx_target_mask==3 &&
        s_tdma_flight_sync.position_tx_sequence==42 &&
        s_tdma_flight_sync.position_tx_position_id==3);
    trigger_position_descriptor_t decoded;
    assert(trigger_position_descriptor_decode(s_tdma_flight_sync.position_tx_wire,&decoded));
    assert(decoded.position_id==3 && decoded.descriptor_crc32);
    assert(!distributed_refmem_stage_position_descriptor(&value,5));
    distributed_refmem_request_position_cancel();
    assert(!distributed_refmem_stage_position_descriptor(&value,5));
    distributed_refmem_position_cancel_service();
    assert(!distributed_refmem_position_descriptor_pending());
    assert(distributed_refmem_stage_position_descriptor(&value,5));
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
