"""TDMA-SEAT-01: the origin fast path rotates priority and first class per
publication opportunity, never wasting one and never reserving on EMPTY."""
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

    /* A waiting record is never overwritten. */
    stage_first(3u);
    uint8_t other[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    mailbox(other, TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 4u);
    assert(!tdma_first_class_stage(&first, other));

    /* A deferred hardware publish keeps the record for the next opportunity. */
    s_offer = TDMA_PRIORITY_TX_EMPTY; s_publish_ok = false;
    assert(step() && tdma_first_class_pending(&first));
    s_publish_ok = true;
    assert(step() && !tdma_first_class_pending(&first) && s_wire[10] == 3u);

    /* A malformed record is retired, never retried. */
    uint8_t bad[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    mailbox(bad, TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 5u);
    bad[TDMA_PROCESS_IMAGE_CRC_OFFSET] ^= 0xffu;
    assert(tdma_first_class_stage(&first, bad));
    const uint32_t calls = s_publish_calls;
    assert(step() && !tdma_first_class_pending(&first) && s_publish_calls == calls);
    assert(a.origin.seat_first_class_reject_count == 1u);

    /* STOP retires a waiting record and the route. */
    stage_first(6u);
    tdma_first_class_retire(&first, false);
    assert(!tdma_first_class_pending(&first) && !tdma_first_class_route(&first));
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
