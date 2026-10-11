"""TDMA-SEAT-01: the origin fast path rotates priority and first class per
publication opportunity, never wasting one and never reserving on EMPTY."""
import os
import shutil
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ORIGIN = ROOT / "components/tdma/src/tdma_pio_spi_ring_origin.inc"
FUNCTIONS = (
    ("static bool", "tdma_pio_spi_ring_origin_mailbox_valid",
     "const uint8_t *mailbox, uint32_t slot, uint32_t mask"),
    ("static void", "tdma_pio_spi_ring_origin_commit_owner",
     "tdma_pio_spi_ring_adapter_t *adapter, const tdma_flight_tx_view_t *tx, "
     "const uint8_t *mailbox, uint32_t generation"),
    ("static bool", "tdma_pio_spi_ring_origin_board",
     "tdma_pio_spi_ring_adapter_t *adapter, "
     "const uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE], bool *boarded"),
    ("static bool", "tdma_pio_spi_ring_origin_first_class_valid",
     "const tdma_pio_spi_ring_adapter_t *adapter, const uint8_t *mailbox"),
    ("static bool", "tdma_pio_spi_ring_origin_publish_priority",
     "tdma_pio_spi_ring_adapter_t *adapter, bool *handled"),
)

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_process_image_layout.h"

/* Physical stubs: one pending replacement, retired by the next boundary. */
static tdma_priority_tx_result_t s_offer;
static uint8_t s_priority[TDMA_FLIGHT_SHORT_SLOT_SIZE];
static bool s_pending, s_publish_ok = true;
static uint32_t s_generation, s_publish_calls;
static uint8_t s_wire[TDMA_FLIGHT_SHORT_SLOT_SIZE];
static tdma_priority_tx_result_t provider(const tdma_ring_runtime_config_t *c, uint8_t *m)
{ (void)c; if (s_offer == TDMA_PRIORITY_TX_READY) memcpy(m, s_priority, sizeof(s_priority)); return s_offer; }
static bool ready(void *c) { (void)c; return !s_pending; }
static bool publish(void *c, const uint8_t *m, uint32_t *g)
{ (void)c; if (!s_publish_ok || s_pending) return false; memcpy(s_wire, m, sizeof(s_wire));
  s_pending = true; ++s_publish_calls; *g = ++s_generation; return true; }
static void boundary(void) { s_pending = false; }
uint32_t tdma_overlay_prepare_state(const tdma_overlay_prepare_t *j) { (void)j; return TDMA_OVERLAY_PREPARE_IDLE; }
bool tdma_overlay_prepare_cancel(tdma_overlay_prepare_t *j) { (void)j; return true; }
bool tdma_flight_engine_copy_tx_layout(const tdma_flight_engine_t *e, tdma_flight_tx_layout_t *l)
{ (void)e; memset(l, 0, sizeof(*l)); l->local_slot_id = 0u; l->output_segment_mask = 1u; return true; }
bool tdma_flight_engine_accept_tx(tdma_flight_engine_t *e, const tdma_flight_tx_layout_t *l,
    const tdma_flight_engine_apply_t *a, bool r) { (void)e; (void)l; (void)a; (void)r; return true; }
''' + "\n".join(
    f"{kind} {name}({arguments});" for kind, name, arguments in FUNCTIONS) + r'''
static void mailbox(uint8_t *m, uint8_t cls, uint8_t tag)
{
    memset(m, 0, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    m[0] = (uint8_t)TDMA_FLIGHT_MAILBOX_MAGIC; m[1] = (uint8_t)(TDMA_FLIGHT_MAILBOX_MAGIC >> 8u);
    m[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] = TDMA_FLIGHT_MAILBOX_VERSION; m[3] = cls;
    m[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] = 0u; m[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 0x0eu;
    m[10] = tag;
    const uint16_t crc = tdma_process_image_crc16_ccitt(m, TDMA_PROCESS_IMAGE_CRC_OFFSET);
    m[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc; m[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
}
static tdma_pio_spi_ring_adapter_t a;
static tdma_first_class_t first;
static tdma_overlay_prepare_t station;
static bool step(void)
{ bool handled = false; const bool ok = tdma_pio_spi_ring_origin_publish_priority(&a, &handled);
  assert(handled); boundary(); return ok; }
static void priority_event(uint8_t tag)
{ s_offer = TDMA_PRIORITY_TX_READY; mailbox(s_priority, TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS, tag); }
static void stage_first(uint8_t tag)
{ uint8_t m[TDMA_FLIGHT_SHORT_SLOT_SIZE]; mailbox(m, TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, tag);
  assert(tdma_first_class_stage(&first, m)); }

int main(void)
{
    a.config.local_slot_id = 0u; a.config.node_count = 4u;
    a.priority_tx_provider = provider;
    a.phys_origin.ready = ready; a.phys_origin.publish = publish;
    a.first_class = &first; a.overlay_preparation = &station;

    /* Provider enabled: the fast path becomes the first-class route. */
    s_offer = TDMA_PRIORITY_TX_EMPTY;
    assert(step() && tdma_first_class_route(&first) && !s_publish_calls);

    /* EMPTY reserves nothing: a waiting first-class record boards. */
    stage_first(1u);
    assert(step() && s_publish_calls == 1u && s_wire[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);
    assert(!tdma_first_class_pending(&first) && a.origin.seat_first_class_count == 1u);

    /* Priority alone uses every opportunity. */
    priority_event(10u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS && s_wire[10] == 10u);
    priority_event(11u);
    assert(step() && s_wire[10] == 11u && a.origin.seat_priority_count == 2u);

    /* A first-class boarding must not make the old priority event look new. */
    stage_first(2u);
    const uint32_t before = s_publish_calls;
    /* Both pending: rotation picks one leader per opportunity, 1:1. */
    priority_event(12u);
    assert(step());
    const uint8_t lead = s_wire[3];
    assert(step());
    assert(s_wire[3] != lead && s_publish_calls == before + 2u);
    assert(a.origin.seat_first_class_count == 2u);
    /* Same priority event again with no first-class record: nothing new, no publish. */
    const uint32_t settled = s_publish_calls;
    assert(step() && s_publish_calls == settled);

    /* A waiting record is never overwritten, and the refusal is counted as an
     * explicit first-class backpressure fact. */
    stage_first(3u);
    uint8_t other[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    mailbox(other, TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 4u);
    assert(!tdma_first_class_stage(&first, other));
    assert(first.stage_refused_count == 1u);

    /* A deferred hardware publish keeps the record for the next opportunity. */
    s_offer = TDMA_PRIORITY_TX_EMPTY; s_publish_ok = false;
    assert(step() && tdma_first_class_pending(&first));
    assert(a.origin.seat_first_class_deferred_count == 1u);
    s_publish_ok = true;
    assert(step() && !tdma_first_class_pending(&first) && s_wire[10] == 3u);

    /* Repeated deferrals are waiting opportunities, not an invisible stall: the
     * declared budget is reached, counted once, and the maximum is published. */
    stage_first(4u);
    const uint32_t defer_violations = a.origin.seat_first_class_wait_violation_count;
    a.origin.seat_first_class_wait = 0u;
    a.origin.seat_first_class_wait_max = 0u;
    s_offer = TDMA_PRIORITY_TX_EMPTY; s_publish_ok = false;
    for (uint32_t i = 0u; i < TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES; ++i)
        assert(step() && tdma_first_class_pending(&first));
    assert(a.origin.seat_first_class_wait == TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
    assert(a.origin.seat_first_class_wait_violation_count == defer_violations + 1u);
    assert(a.origin.seat_first_class_deferred_count ==
           1u + TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
    s_publish_ok = true;
    assert(step() && !tdma_first_class_pending(&first));
    assert(a.origin.seat_first_class_wait_max == TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
    assert(a.origin.seat_first_class_wait == 0u);

    /* A malformed record is retired, never retried. */
    uint8_t bad[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    mailbox(bad, TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 5u);
    bad[TDMA_PROCESS_IMAGE_CRC_OFFSET] ^= 0xffu;
    assert(tdma_first_class_stage(&first, bad));
    const uint32_t calls = s_publish_calls;
    assert(step() && !tdma_first_class_pending(&first) && s_publish_calls == calls);
    assert(a.origin.seat_first_class_reject_count == 1u);

    /* STOP retires a waiting record and the route; the loss is counted. */
    stage_first(6u);
    tdma_first_class_retire(&first, false);
    assert(!tdma_first_class_pending(&first) && !tdma_first_class_route(&first));
    assert(first.retired_waiting_count == 1u);

    /* A record waiting across route loss publishes its wait before retirement,
     * so a starved first-class record is never a silent loss. */
    a.origin.seat_first_class_wait = 5u;
    a.origin.seat_first_class_wait_max = 0u;
    stage_first(7u);
    priority_event(80u);
    assert(step());
    assert(!tdma_first_class_pending(&first));
    assert(a.origin.seat_first_class_wait_max == 5u);
    assert(first.retired_waiting_count == 2u);

    /* M:N = 2:1: priority leads twice, first class once, per three opportunities.
     * Route start re-arms the seat after the STOP case above: the fast path
     * retires a record staged while the route is down. */
    tdma_first_class_retire(&first, true);
    a.seat_rotation_priority_share = 2u;
    a.seat_rotation_first_class_share = 1u;
    a.origin.seat_opportunity_count = 0u;
    stage_first(20u);
    priority_event(30u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS);
    priority_event(31u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS);
    assert(tdma_first_class_pending(&first));
    priority_event(32u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);
    assert(!tdma_first_class_pending(&first));
    /* The wait counter is published when the waiting record finally boards. */
    assert(a.origin.seat_first_class_wait_max >= 2u);

    /* M:N = 1:2 leads with the first class twice per three opportunities. */
    a.seat_rotation_priority_share = 1u;
    a.seat_rotation_first_class_share = 2u;
    a.origin.seat_opportunity_count = 0u;
    stage_first(40u);
    priority_event(50u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS);
    priority_event(51u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);
    priority_event(52u);
    stage_first(41u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);

    /* An unset (zero) ratio falls back to the compiled 1:1 default. */
    a.seat_rotation_priority_share = 0u;
    a.seat_rotation_first_class_share = 0u;
    a.origin.seat_opportunity_count = 0u;
    stage_first(60u);
    priority_event(70u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS);
    priority_event(71u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);

    /* The declared wait budget is reported as a crossing, not only a maximum:
     * with M=8:N=1 a first-class record waits the full budget before boarding. */
    tdma_first_class_retire(&first, true);
    a.seat_rotation_priority_share = 8u;
    a.seat_rotation_first_class_share = 1u;
    a.origin.seat_opportunity_count = 0u;
    a.origin.seat_first_class_wait = 0u;
    const uint32_t violations = a.origin.seat_first_class_wait_violation_count;
    stage_first(90u);
    for (uint8_t i = 0u; i < 8u; ++i) {
        priority_event((uint8_t)(91u + i));
        assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS);
    }
    assert(tdma_first_class_pending(&first));
    assert(a.origin.seat_first_class_wait ==
           TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
    assert(a.origin.seat_first_class_wait_violation_count == violations + 1u);
    /* The ninth opportunity belongs to the first class: the budget is bounded,
     * not a starvation. */
    priority_event(99u);
    assert(step() && s_wire[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS);
    assert(!tdma_first_class_pending(&first));
    assert(a.origin.seat_first_class_wait == 0u);

    /* Validation boundary: both shares and the M+N cycle are bounded. */
    assert(tdma_seat_rotation_valid(1u, 1u));
    assert(tdma_seat_rotation_valid(TDMA_SEAT_ROTATION_SHARE_MAX, 4u));
    assert(!tdma_seat_rotation_valid(0u, 1u));
    assert(!tdma_seat_rotation_valid(1u, 0u));
    assert(!tdma_seat_rotation_valid(TDMA_SEAT_ROTATION_SHARE_MAX + 1u, 1u));
    assert(!tdma_seat_rotation_valid(TDMA_SEAT_ROTATION_SHARE_MAX,
                                     TDMA_SEAT_ROTATION_SHARE_MAX));
    assert(tdma_seat_rotation_priority(0u) ==
           TDMA_SEAT_ROTATION_DEFAULT_PRIORITY_SHARE);
    assert(tdma_seat_rotation_first_class(0u) ==
           TDMA_SEAT_ROTATION_DEFAULT_FIRST_CLASS_SHARE);
    return 0;
}
'''


def test_seat_rotation_on_origin_fast_path(tmp_path):
    source = ORIGIN.read_text(encoding="utf-8")
    bodies = "\n".join(
        f"{kind} {name}({arguments}) {{" + c_definition_body(source, name) + "}\n"
        for kind, name, arguments in FUNCTIONS)
    executable = compile_grid(tmp_path, "seat-rotation", HARNESS + bodies)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_seat_window_gate_still_builds_when_enabled(tmp_path):
    """The origin's seat-window gate is compiled only under
    TDMA_LOOP_SCHEDULE_ENABLED, which is 0 by default. A dormant path rots, so
    build the real adapter translation unit with the gate enabled: it must still
    compile."""
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
               "-DTDMA_LOOP_SCHEDULE_ENABLED=1",
               "-I" + str(ROOT / "components/tdma/inc"),
               str(ROOT / "components/tdma/src/tdma_pio_spi_ring_adapter.c")]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr


GATE_PREFIX = r'''
#include <assert.h>
#include <string.h>
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_seat_admission.h"

'''

GATE_MAIN = r'''
/* The gate only reads adapter->config.cycle_period_ns and writes the two
 * starvation counters, so a bare adapter is a faithful fixture. */
int main(void)
{
    tdma_pio_spi_ring_adapter_t adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.config.cycle_period_ns = 1000000u;  /* 1 ms slots inside a 4 ms loop */

    /* Unmounted load: unconstrained, no counters. */
    assert(tdma_pio_spi_ring_origin_seat_gate(&adapter, TDMA_LOOP_SLOT_NONE, 0u));
    assert(adapter.origin.seat_starved_count == 0u);

    /* Priority seat, boundary 0 (0 ms) is its window. */
    assert(tdma_pio_spi_ring_origin_seat_gate(&adapter, TDMA_LOOP_SLOT_PRIORITY, 0u));
    /* First class on boundary 0 is the correct seat but the wrong boundary. */
    assert(!tdma_pio_spi_ring_origin_seat_gate(&adapter, TDMA_LOOP_SLOT_FIRST_CLASS, 0u));
    assert(adapter.origin.seat_first_class_starved_count == 0u);
    /* Boundary 2 (2 ms) is inside the first-class window [1.5 ms, 3.0 ms). */
    assert(tdma_pio_spi_ring_origin_seat_gate(&adapter, TDMA_LOOP_SLOT_FIRST_CLASS, 2u));
    assert(adapter.origin.seat_first_class_starved_count == 0u);
    /* Boundary 3 (3 ms) starts the guard interval: defer, still no starvation. */
    assert(!tdma_pio_spi_ring_origin_seat_gate(&adapter, TDMA_LOOP_SLOT_FIRST_CLASS, 3u));
    assert(adapter.origin.seat_first_class_starved_count == 0u);

    /* The LOAD seat is deliberately disabled (TDMA_LOOP_PLAN_LOAD_ENABLED == 0),
     * so it is never in the plan: mounting a load there must be counted as
     * starvation, never deferred silently forever. */
    assert(!tdma_pio_spi_ring_origin_seat_gate(&adapter, TDMA_LOOP_SLOT_LOAD, 0u));
    assert(adapter.origin.seat_starved_count == 1u);
    assert(adapter.origin.seat_first_class_starved_count == 0u);
    assert(!tdma_pio_spi_ring_origin_seat_gate(&adapter, TDMA_LOOP_SLOT_LOAD, 2u));
    assert(adapter.origin.seat_starved_count == 2u);
    return 0;
}
'''


def test_seat_window_gate_counts_starvation_and_defers(tmp_path):
    source = ORIGIN.read_text(encoding="utf-8")
    body = c_definition_body(source, "tdma_pio_spi_ring_origin_seat_gate")
    definition = ("static bool tdma_pio_spi_ring_origin_seat_gate("
        "tdma_pio_spi_ring_adapter_t *adapter, tdma_loop_slot_class_t load_seat, "
        "uint64_t boundary_sequence) {" + body + "}\n")
    executable = compile_grid(tmp_path, "seat-window", GATE_PREFIX + definition + GATE_MAIN,
                             [ROOT / "components/tdma/src/tdma_seat_admission.c"])
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
