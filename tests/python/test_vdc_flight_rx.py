"""Run the production ordinary-mailbox retention over the real RX FIFO.

Ring/profile discovery and the existing independent RefMem DELTA parser are
controlled. Mailbox admission, CRC, serial ordering, FIFO epochs and guarded
history reads execute production C. No DCO command is applied by this slice.
"""
import re
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def flight_rx_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-flight-rx")
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    service = (ROOT / "components/tdma/src/tdma_service.c").read_text(encoding="utf-8")
    binding = re.search(r"typedef struct \{[^}]*\} distributed_refmem_vdc_flight_rx_binding_t;", source, re.S)
    state = re.search(r"typedef struct \{[^}]*\} distributed_refmem_tdma_flight_sync_t;", source, re.S)
    assert binding and state
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "distributed_refmem.h"
#include "vdc_dpll_manager.h"
#include "tdma_process_image_layout.h"
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE TDMA_FLIGHT_SHORT_SLOT_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE TDMA_FLIGHT_SHORT_PAYLOAD_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT TDMA_FLIGHT_SHORT_SLOT_COUNT
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC TDMA_FLIGHT_MAILBOX_MAGIC
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION TDMA_FLIGHT_MAILBOX_VERSION
static tdma_service_service_t owner;
static tdma_ring_clock_snapshot_t clock_ring;
static vdc_dpll_manager_refmem_snapshot_t profile;
static bool clock_available = true, profile_available = true;
static uint32_t ordinary_parse_count, copy_mode;
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{ *out = clock_ring; return clock_available; }
bool vdc_dpll_manager_get_refmem_snapshot(vdc_dpll_manager_refmem_snapshot_t *out)
{ *out = profile; return profile_available; }
static void distributed_refmem_tdma_flight_parse_mailbox(const uint8_t *data, size_t size)
{ assert(data && size == TDMA_FLIGHT_SHORT_SLOT_SIZE); ++ordinary_parse_count; }
/* Raw feedback has separate production/FIFO integration coverage. */
static void distributed_refmem_feedback_receive(tdma_service_service_t *service, uint32_t slot,
    const uint8_t *mailbox, const tdma_flight_rx_view_t *view)
{ (void)service; (void)slot; (void)mailbox; (void)view; }
''' + binding.group(0) + "\n" + state.group(0) + r'''
static distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;
static volatile uint32_t s_vdc_flight_rx_guard;
static distributed_refmem_vdc_flight_rx_binding_t s_vdc_flight_rx_binding;
static distributed_refmem_vdc_flight_rx_snapshot_t s_vdc_flight_rx;
static bool s_vdc_flight_rx_admission_ready;
static void *controlled_copy(void *to, const void *from, size_t size)
{
    if (copy_mode && from == &s_vdc_flight_rx) {
        memcpy(to, from, size / 2);
        s_vdc_flight_rx_guard += 2;
        memcpy((uint8_t *)to + size / 2, (const uint8_t *)from + size / 2, size - size / 2);
        return to;
    }
    return memcpy(to, from, size);
}
'''
    harness += "\n".join(ingress_definition(service, name) for name in (
        "tdma_service_core0_advance_flight_rx_admission_epoch",
        "tdma_service_acquire_flight_rx", "tdma_service_release_flight_rx"))
    harness += "\n#define memcpy controlled_copy\n"
    harness += "\n".join(ingress_definition(source, name) for name in (
        "distributed_refmem_get_le16", "distributed_refmem_get_i16",
        "distributed_refmem_get_le32", "distributed_refmem_flight_input_offset_for_slot",
        "distributed_refmem_vdc_flight_rx_read_binding",
        "distributed_refmem_vdc_flight_rx_same_binding",
        "distributed_refmem_vdc_flight_rx_refresh",
        "distributed_refmem_vdc_flight_rx_accept",
        "distributed_refmem_get_vdc_flight_rx",
        "distributed_refmem_tdma_flight_sync_receive"))
    harness += r'''
#undef memcpy
static uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
static void put16(uint8_t *p, uint16_t x) { p[0] = x; p[1] = x >> 8; }
static void put32(uint8_t *p, uint32_t x)
{ for (uint32_t i = 0; i < 4; ++i) p[i] = x >> (i * 8); }
static uint8_t *mailbox(uint32_t source, uint32_t sequence)
{
    uint8_t *m = image + source * TDMA_FLIGHT_SHORT_SLOT_SIZE;
    memset(m, 0, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    put16(m, TDMA_FLIGHT_MAILBOX_MAGIC);
    m[2] = TDMA_FLIGHT_MAILBOX_VERSION; m[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    m[4] = source; m[5] = 0x3f; put16(m + 6, (uint16_t)sequence);
    put16(m + TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET, (uint16_t)(int16_t)-11);
    put16(m + TDMA_PROCESS_IMAGE_VDC_RATE_OFFSET, 57);
    m[TDMA_PROCESS_IMAGE_VDC_LOCK_OFFSET] = 2;
    m[TDMA_PROCESS_IMAGE_VDC_QUALITY_OFFSET] = 0xa1;
    put32(m + TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET, sequence);
    put16(m + TDMA_PROCESS_IMAGE_CRC_OFFSET, tdma_process_image_crc16_ccitt(m, TDMA_PROCESS_IMAGE_CRC_OFFSET));
    return m;
}
static void crc(uint8_t *m)
{ put16(m + TDMA_PROCESS_IMAGE_CRC_OFFSET, tdma_process_image_crc16_ccitt(m, TDMA_PROCESS_IMAGE_CRC_OFFSET)); }
static void enqueue(uint32_t mask, uint32_t transport)
{
    assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo, image,
        tdma_flight_payload_size(clock_ring.node_count), transport, transport, mask, 0, 0));
}
static void receive(void)
{
    const tdma_ring_runtime_snapshot_t ring = {.local_slot_id = clock_ring.local_slot_id,
        .node_count = clock_ring.node_count};
    distributed_refmem_tdma_flight_sync_receive(&owner, &ring);
}
static void send(uint32_t source, uint32_t sequence)
{ mailbox(source, sequence); enqueue(1u << source, 1000 + sequence); receive(); }
static void setup(void)
{
    assert(tdma_flight_fifo_init(&owner.flight_fifo));
    clock_ring = (tdma_ring_clock_snapshot_t){.enabled = 1, .adapter_started = 1,
        .config_seq = 7, .applied_config_seq = 7, .node_count = 6,
        .local_slot_id = 2, .schedule_crc32 = 0xaabb};
    profile.control_profile = (vdc_dpll_control_profile_t){.valid = 1,
        .mode = VDC_DPLL_CONTROL_MODE_FOLLOWER, .generation = 9, .follow_master_slot_id = 0};
    profile.schedule.local_slot_id = 2; profile.schedule.schedule_crc32 = 0xaabb;
    profile.clock_epoch_id = 3; profile.clock_run_id = 4;
    s_tdma_flight_sync.node_count = 6; s_tdma_flight_sync.local_slot = 2;
    s_tdma_flight_sync.active_mask = 0x3f;
    distributed_refmem_vdc_flight_rx_refresh(&owner);
    assert(s_vdc_flight_rx_binding.rx_admission_epoch != 0);
    send(0, 10);
    assert(s_vdc_flight_rx.retained && s_vdc_flight_rx.receive_count == 1);
    assert(s_vdc_flight_rx.phase_offset_ns == -44 && s_vdc_flight_rx.period_adjust_ppb == 114);
    assert(s_vdc_flight_rx.source_slot == 0 && s_vdc_flight_rx.mailbox_seq == 10);
    assert(s_vdc_flight_rx.transport_sequence == 1010);
}
int main(int argc, char **argv)
{
    assert(argc == 2); setup();
    const distributed_refmem_vdc_flight_rx_snapshot_t before = s_vdc_flight_rx;
    distributed_refmem_vdc_flight_rx_snapshot_t out;
    uint8_t *m = mailbox(0, 11);
    if (!strcmp(argv[1], "new")) {
        send(0, 11); assert(distributed_refmem_get_vdc_flight_rx(&out));
        assert(out.retained && out.active && out.receive_count == 2 && out.mailbox_seq == 11);
    } else if (!strcmp(argv[1], "bad_crc")) { m[8] ^= 1; enqueue(1, 1011); receive(); }
    else if (!strcmp(argv[1], "other_source")) { send(1, 11); }
    else if (!strcmp(argv[1], "slot_mismatch")) { m[4] = 1; crc(m); enqueue(1, 1011); receive(); }
    else if (!strcmp(argv[1], "wrong_target")) { m[5] = 1; crc(m); enqueue(1, 1011); receive(); }
    else if (!strcmp(argv[1], "not_ready")) { m[13] &= 0x7f; crc(m); enqueue(1, 1011); receive(); }
    else if (!strcmp(argv[1], "duplicate")) { send(0, 10); }
    else if (!strcmp(argv[1], "stale")) { send(0, 9); }
    else if (!strcmp(argv[1], "half_range")) { send(0, 10u + 0x80000000u); }
    else if (!strcmp(argv[1], "sequence_mismatch")) { m[6] ^= 1; crc(m); enqueue(1, 1011); receive(); }
    else if (!strcmp(argv[1], "zero_sequence")) { send(0, 0); }
    else if (!strcmp(argv[1], "wrong_class")) { m[3] = 0x11; crc(m); enqueue(1, 1011); receive(); }
    else if (!strcmp(argv[1], "stop")) {
        clock_ring.enabled = 0;
        assert(distributed_refmem_get_vdc_flight_rx(&out)); assert(out.retained && !out.active);
        distributed_refmem_vdc_flight_rx_refresh(&owner); send(0, 11);
        assert(distributed_refmem_get_vdc_flight_rx(&out)); assert(out.retained && !out.active);
    } else if (!strcmp(argv[1], "role_roundtrip") || !strcmp(argv[1], "ring_change")) {
        enqueue(1, 1011); // Queued before the new binding must be skipped.
        if (!strcmp(argv[1], "role_roundtrip")) {
            profile.control_profile.mode = VDC_DPLL_CONTROL_MODE_MASTER;
            profile.control_profile.generation++;
            distributed_refmem_vdc_flight_rx_refresh(&owner);
            assert(distributed_refmem_get_vdc_flight_rx(&out)); assert(out.retained && !out.active);
            profile.control_profile.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
            profile.control_profile.generation++;
        } else { clock_ring.config_seq++; clock_ring.applied_config_seq++; }
        distributed_refmem_vdc_flight_rx_refresh(&owner);
        assert(distributed_refmem_get_vdc_flight_rx(&out)); assert(out.retained && !out.active);
        receive(); assert(!memcmp(&before, &s_vdc_flight_rx, sizeof(before)));
        send(0, 1); assert(distributed_refmem_get_vdc_flight_rx(&out));
        assert(out.active && out.mailbox_seq == 1 && out.receive_count == 2);
    } else if (!strcmp(argv[1], "snapshot_failure")) {
        profile_available = false; distributed_refmem_vdc_flight_rx_refresh(&owner);
        send(0, 11); assert(distributed_refmem_get_vdc_flight_rx(&out)); assert(out.retained && !out.active);
        profile_available = true; distributed_refmem_vdc_flight_rx_refresh(&owner);
        send(0, 12); assert(s_vdc_flight_rx.mailbox_seq == 12);
    } else if (!strcmp(argv[1], "unacked_config")) {
        clock_ring.config_seq++; distributed_refmem_vdc_flight_rx_refresh(&owner);
        send(0, 11); assert(distributed_refmem_get_vdc_flight_rx(&out)); assert(out.retained && !out.active);
    } else if (!strcmp(argv[1], "copy_race")) {
        copy_mode = 1; assert(!distributed_refmem_get_vdc_flight_rx(&out));
        copy_mode = 0; s_vdc_flight_rx_guard = 1; assert(!distributed_refmem_get_vdc_flight_rx(&out));
        s_vdc_flight_rx_guard = UINT32_MAX - 1; copy_mode = 1;
        assert(!distributed_refmem_get_vdc_flight_rx(&out));
    } else if (!strcmp(argv[1], "saturate")) {
        s_vdc_flight_rx.receive_count = UINT32_MAX; send(0, 11);
        assert(s_vdc_flight_rx.receive_count == UINT32_MAX && s_vdc_flight_rx.mailbox_seq == 11);
    } else if (!strcmp(argv[1], "wrap")) {
        s_vdc_flight_rx.mailbox_seq = UINT32_MAX; send(0, 1);
        assert(s_vdc_flight_rx.mailbox_seq == 1 && s_vdc_flight_rx.receive_count == 2);
    } else { assert(!"unknown case"); }
    if (strcmp(argv[1], "new") && strcmp(argv[1], "role_roundtrip") &&
        strcmp(argv[1], "ring_change") && strcmp(argv[1], "snapshot_failure") &&
        strcmp(argv[1], "saturate") && strcmp(argv[1], "wrap")) {
        assert(!memcmp(&before, &s_vdc_flight_rx, sizeof(before)));
    }
    assert(ordinary_parse_count > 0);
    printf("ordinary flight RX %s passed\n", argv[1]);
    return 0;
}
'''
    return compile_executable(directory, "flight_rx", harness,
                              [ROOT / "components/tdma/src/tdma_flight_fifo.c"])


@pytest.mark.parametrize("case", [
    "new", "bad_crc", "other_source", "slot_mismatch", "wrong_target",
    "not_ready", "duplicate", "stale", "half_range", "sequence_mismatch",
    "zero_sequence", "wrong_class", "stop", "role_roundtrip", "ring_change",
    "snapshot_failure", "unacked_config", "copy_race", "saturate", "wrap",
])
def test_ordinary_flight_rx(flight_rx_executable, case):
    result = subprocess.run([str(flight_rx_executable), case],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
