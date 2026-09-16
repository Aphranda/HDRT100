"""Run the real Core1 ingress against the production retained TDMA lane.

Only owner facades and the clock are mocked. No DCO, RefMem, storage, or SCPI
implementation is linked: raw mailbox consumption must not require them.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module", params=[(6, False), (6, True), (8, False), (8, True)])
def ingress_executable(request, tmp_path_factory):
    nodes, short_enums = request.param
    directory = tmp_path_factory.mktemp(f"vdc-ingress-{nodes}-{int(short_enums)}")
    source = directory / "ingress.c"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required"
    executable = directory / ("ingress.exe" if os.name == "nt" else "ingress")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={nodes}",
               *["-I" + str(ROOT / path) for path in (
                   "components/tdma/inc", "components/vdc_dpll_manager/inc",
                   "components/vdc_dpll_manager/src")],
               str(source), str(ROOT / "components/tdma/src/tdma_priority_rx.c"),
               str(ROOT / "components/vdc_dpll_manager/src/vdc_priority_codec.c"),
               str(ROOT / "components/tdma/src/tdma_transport_frame.c"), "-o", str(executable)]
    if short_enums:
        command.insert(1, "-fshort-enums")
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "build.json").write_text(json.dumps({"command": command,
        "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}, indent=2),
        encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("case", [
    "empty", "sequence_zero", "duplicates", "full_sequence", "gap_saturation",
    "order_reject", "snapshot_failure", "overwrite_retry", "stop_between_reads",
    "rebase_between_reads", "stop_and_arm", "epoch_and_wrap", "clock_failure",
    "future_irq", "clock_body_range", "clock_fail_preserves_max", "arrival_64bit", "guard_getter",
    "ordinary_and_unknown", "counter_saturation", "typed_decode_and_reject",
])
def test_real_core1_priority_ingress(ingress_executable, case):
    result = subprocess.run([str(ingress_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_priority_ingress.h"
#include "tdma_profile.h"
#include "tdma_process_image_layout.h"
#include "tdma_transport_frame.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
static tdma_priority_rx_t lane;
static tdma_priority_rx_snapshot_t stale_snapshot;
static bool use_stale_snapshot;
static unsigned snapshot_calls, copy_calls, clock_calls, interleave;
static uint64_t clock_values[2] = {1000u, 1100u};
static bool clock_valid[2] = {true, true};
static uint32_t *collision_guard;
static const uint32_t *collision_word;
static bool invalid_typed;
static void publish(uint32_t sequence, uint64_t irq_ticks, uint8_t mailbox_class);

static uint32_t read_atomic(const uint32_t *p, int order)
{
    const uint32_t value = __atomic_load_n(p, order);
    if (p == collision_word) {
        __atomic_add_fetch(collision_guard, 2u, __ATOMIC_RELEASE);
        collision_word = NULL;
    }
    return value;
}
static bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz, uint64_t *out)
{
    assert(hz == BOARD_SYS_CLOCK_HZ && clock_calls < 2u);
    const unsigned index = clock_calls++;
    if (!clock_valid[index]) return false;
    *out = clock_values[index]; return true;
}
static bool tdma_runtime_owner_get_priority_rx_snapshot(tdma_priority_rx_snapshot_t *out)
{
    ++snapshot_calls;
    if (use_stale_snapshot) { *out = stale_snapshot; return true; }
    return tdma_priority_rx_snapshot(&lane, out);
}
static bool tdma_runtime_owner_copy_priority_rx_live(uint32_t epoch, uint32_t sequence,
    tdma_priority_rx_record_t *out)
{
    ++copy_calls;
    if (interleave == 1u) publish(sequence + TDMA_PRIORITY_RX_CAPACITY, 900u, 0x10u);
    if (interleave == 2u) tdma_priority_rx_stop(&lane);
    if (interleave == 3u) assert(tdma_priority_rx_rebase(&lane));
    interleave = 0u;
    return tdma_priority_rx_copy_live(&lane, epoch, sequence, out);
}
#define __atomic_load_n read_atomic
#include "vdc_priority_ingress.inc"
#undef __atomic_load_n

static void put16(uint8_t *p, uint32_t value)
{ p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8u); }
static void publish(uint32_t sequence, uint64_t irq_ticks, uint8_t mailbox_class)
{
    uint8_t payload[4u * 32u + 4u] = {0};
    put16(payload, TDMA_FLIGHT_MAILBOX_MAGIC);
    payload[2] = TDMA_FLIGHT_MAILBOX_VERSION;
    payload[3] = mailbox_class; payload[4] = 0u; payload[5] = 15u;
    put16(payload + 6u, sequence + 40u);
    if (tdma_process_image_typed_sync_class_valid(mailbox_class)) {
        const vdc_priority_codec_record_t typed = {.binding_generation=42u,
            .event_sequence=sequence-1u, .event_time_lower=123456789u,
            .uncertainty_width=7u, .flags=0u};
        assert(vdc_priority_codec_encode(&typed, payload+8u));
        if (invalid_typed) memset(payload+8u,0,4u);
    }
    put16(payload + 30u, tdma_process_image_crc16_ccitt(payload, 30u));
    uint8_t packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE + sizeof(payload)];
    tdma_transport_frame_build_t build = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT, .origin_slot_id = 0u,
        .transport_sequence = sequence, .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
        .flags = TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE, .schedule_crc32 = 0x1234u,
        .ring_profile_crc32 = 0x5678u, .hop_limit = 4u,
        .payload = payload, .payload_size = sizeof(payload)};
    size_t size; tdma_transport_result_t result;
    assert(tdma_transport_frame_encode(&build, packet, sizeof(packet), &size, &result));
    assert(size == sizeof(packet));
    tdma_priority_rx_record_t record = {.epoch = lane.status.epoch, .sequence = sequence,
        .irq_entry_ticks = irq_ticks, .candidate_word = (uint64_t)sequence * 100u};
    memcpy(record.header, packet, sizeof(record.header));
    memcpy(record.mailbox, payload, sizeof(record.mailbox));
    const tdma_priority_rx_binding_t binding = {.packet_bytes = sizeof(packet),
        .reference_slot = 0u, .local_slot = 1u, .node_count = 4u,
        .schedule_crc32 = 0x1234u, .profile_crc32 = 0x5678u};
    const uint32_t reason = tdma_priority_rx_validate(&binding, &record);
    assert(reason == (mailbox_class == 0x7fu ? TDMA_PRIORITY_RX_MAILBOX : TDMA_PRIORITY_RX_OK));
    /* Unknown-class injection tests the consumer's lack of a control side
     * effect; production validation above correctly rejects this class. */
    assert(tdma_priority_rx_publish(&lane, &record));
}
static vdc_priority_ingress_snapshot_t sample(void)
{
    snapshot_calls = copy_calls = clock_calls = 0u;
    vdc_priority_ingress_core1();
    assert(snapshot_calls == 1u && copy_calls <= 1u && clock_calls <= 2u);
    vdc_priority_ingress_snapshot_t out;
    assert(vdc_dpll_manager_get_priority_ingress(&out));
    return out;
}
static void expect_raw(const vdc_priority_ingress_snapshot_t *out, uint32_t sequence)
{
    tdma_priority_rx_record_t raw;
    assert(out->active && out->have_record && out->record.sequence == sequence);
    assert(tdma_priority_rx_copy(&lane, out->epoch, sequence, &raw));
    assert(!memcmp(&raw, &out->record, sizeof(raw)));
    assert(out->last_class == raw.mailbox[3]);
}
int main(int argc, char **argv)
{
    assert(argc == 2); const char *mode = argv[1];
    assert(tdma_priority_rx_start(&lane));
    vdc_priority_ingress_snapshot_t out;
    if (!strcmp(mode, "typed_decode_and_reject")) {
        publish(1u,900u,0x14u); out=sample();
        assert(out.typed_decode_count==1u && out.typed_reject_count==0u);
        assert(out.typed_record.binding_generation==42u && out.typed_record.event_sequence==0u);
        assert(out.typed_record.event_time_lower==123456789u && out.typed_record.uncertainty_width==7u);
        invalid_typed=true;
        publish(2u,900u,0x14u); out=sample();
        assert(out.typed_decode_count==1u && out.typed_reject_count==1u);
        assert(out.typed_record.binding_generation==42u && out.typed_record.event_sequence==0u);
        out=sample(); assert(out.typed_reject_count==1u);
        publish(3u,900u,0x10u); out=sample();
        assert(out.typed_decode_count==1u && out.typed_reject_count==1u);
        tdma_priority_rx_stop(&lane); out=sample(); assert(!out.active);
        assert(tdma_priority_rx_start(&lane)); out=sample();
        assert(!out.typed_decode_count && !out.typed_reject_count && !out.typed_record.binding_generation);
    } else if (!strcmp(mode, "empty")) {
        out = sample(); assert(out.schema == 1u && out.active && !out.have_record);
        assert(out.service_count == 1u && out.empty_polls == 1u && copy_calls == 0u);
    } else if (!strcmp(mode, "sequence_zero")) {
        publish(0u, 900u, 0x10u); out = sample(); expect_raw(&out, 0u);
        assert(out.copied_count == 1u && copy_calls == 1u && out.body_last_cycles == 100u);
        assert(out.arrival_last_cycles == 200u && out.timing_samples == 1u);
    } else if (!strcmp(mode, "duplicates")) {
        publish(7u, 900u, 0x10u); out = sample();
        const tdma_priority_rx_record_t saved = out.record;
        out = sample(); assert(copy_calls == 0u && out.copied_count == 1u && out.duplicate_polls == 1u);
        assert(!memcmp(&saved, &out.record, sizeof(saved)));
    } else if (!strcmp(mode, "full_sequence")) {
        publish(1u, 900u, 0x10u); out = sample();
        publish(65537u, 950u, 0x10u); out = sample(); expect_raw(&out, 65537u);
        assert(out.copied_count == 2u && out.skipped_carrier_count == 65535u);
        assert(out.duplicate_polls == 0u);
    } else if (!strcmp(mode, "gap_saturation")) {
        publish(10u, 900u, 0x10u); out = sample();
        publish(14u, 950u, 0x10u); out = sample(); assert(out.skipped_carrier_count == 3u);
        s_priority_ingress.skipped_carrier_count = UINT32_MAX - 1u;
        publish(18u, 950u, 0x10u); out = sample(); assert(out.skipped_carrier_count == UINT32_MAX);
    } else if (!strcmp(mode, "order_reject")) {
        publish(2u, 900u, 0x10u); assert(tdma_priority_rx_snapshot(&lane, &stale_snapshot));
        publish(5u, 950u, 0x10u); out = sample();
        use_stale_snapshot = true; out = sample();
        assert(out.order_rejects == 1u && out.record.sequence == 5u && copy_calls == 0u);
        stale_snapshot.latest_sequence = 5u + (uint32_t)INT32_MAX + 1u;
        out = sample(); assert(out.order_rejects == 2u && copy_calls == 0u);
    } else if (!strcmp(mode, "snapshot_failure")) {
        publish(1u, 900u, 0x10u); out = sample();
        const vdc_priority_ingress_snapshot_t saved = out;
        ++lane.guard; out = sample(); --lane.guard;
        assert(!memcmp(&out, &saved, sizeof(out)) && copy_calls == 0u);
    } else if (!strcmp(mode, "overwrite_retry")) {
        publish(1u, 900u, 0x10u); interleave = 1u; out = sample();
        assert(!out.have_record && out.copy_retries == 1u && out.copied_count == 0u);
        out = sample(); expect_raw(&out, 5u); assert(out.copied_count == 1u);
    } else if (!strcmp(mode, "stop_between_reads")) {
        publish(1u, 900u, 0x10u); interleave = 2u; out = sample();
        assert(!out.have_record && out.copy_retries == 1u);
        out = sample(); assert(!out.active && !out.have_record && copy_calls == 0u);
    } else if (!strcmp(mode, "rebase_between_reads")) {
        publish(1u, 900u, 0x10u); const uint32_t old = lane.status.epoch;
        interleave = 3u; out = sample(); assert(!out.have_record && out.copy_retries == 1u);
        publish(1u, 950u, 0x10u); out = sample(); expect_raw(&out, 1u);
        assert(out.epoch == old + 1u && out.copy_retries == 0u && out.copied_count == 1u);
    } else if (!strcmp(mode, "stop_and_arm")) {
        publish(3u, 900u, 0x10u); out = sample(); const uint32_t old = out.epoch;
        const tdma_priority_rx_record_t saved = out.record;
        tdma_priority_rx_stop(&lane); out = sample();
        assert(!out.active && out.have_record && copy_calls == 0u);
        assert(!memcmp(&saved, &out.record, sizeof(saved)));
        assert(tdma_priority_rx_start(&lane)); out = sample();
        assert(out.active && out.epoch == old + 1u && !out.have_record && out.copied_count == 0u);
        publish(3u, 900u, 0x10u); out = sample(); expect_raw(&out, 3u);
    } else if (!strcmp(mode, "epoch_and_wrap")) {
        publish(UINT32_MAX, 900u, 0x10u); out = sample(); const uint32_t old = out.epoch;
        publish(0u, 950u, 0x10u); out = sample(); expect_raw(&out, 0u);
        assert(out.epoch == old + 1u && out.copied_count == 1u && out.order_rejects == 0u);
        assert(tdma_priority_rx_rebase(&lane)); out = sample();
        assert(out.epoch == old + 2u && !out.have_record && out.empty_polls == 1u);
    } else if (!strcmp(mode, "clock_failure")) {
        publish(1u, 900u, 0x10u); clock_valid[0] = false; out = sample();
        expect_raw(&out, 1u); assert(out.clock_failures == 1u && !out.timing_samples);
        assert(out.body_last_cycles == UINT32_MAX && out.arrival_last_cycles == UINT64_MAX);
        clock_valid[0] = true; clock_valid[1] = false; publish(2u, 900u, 0x10u); out = sample();
        assert(out.clock_failures == 2u && out.copied_count == 2u && !out.timing_samples);
        assert(out.body_last_cycles == UINT32_MAX && out.arrival_last_cycles == UINT64_MAX);
        clock_valid[1] = true; out = sample(); assert(out.timing_samples == 1u);
        assert(out.arrival_last_cycles == UINT64_MAX); /* No new copy, no invented age. */
    } else if (!strcmp(mode, "future_irq")) {
        publish(1u, 1101u, 0x10u); out = sample(); expect_raw(&out, 1u);
        assert(out.clock_failures == 1u && !out.timing_samples && !out.arrival_max_cycles);
        assert(out.body_last_cycles == UINT32_MAX && out.arrival_last_cycles == UINT64_MAX);
    } else if (!strcmp(mode, "clock_body_range")) {
        publish(1u, 1u, 0x10u); clock_values[0] = 1100u; clock_values[1] = 1000u;
        out = sample(); assert(out.clock_failures == 1u && !out.timing_samples);
        clock_values[0] = 0u; clock_values[1] = (uint64_t)UINT32_MAX + 1u;
        out = sample(); assert(out.clock_failures == 2u && !out.timing_samples);
        clock_values[0] = UINT64_MAX - 10u; clock_values[1] = 3u;
        out = sample(); assert(out.clock_failures == 3u && !out.timing_samples);
    } else if (!strcmp(mode, "clock_fail_preserves_max")) {
        publish(1u, 900u, 0x10u); out = sample();
        assert(out.body_max_cycles == 100u && out.arrival_max_cycles == 200u);
        publish(2u, 950u, 0x10u); clock_valid[1] = false; out = sample();
        expect_raw(&out, 2u);
        assert(out.body_last_cycles == UINT32_MAX && out.arrival_last_cycles == UINT64_MAX);
        assert(out.body_max_cycles == 100u && out.arrival_max_cycles == 200u && out.timing_samples == 1u);
    } else if (!strcmp(mode, "arrival_64bit")) {
        publish(1u, 1u, 0x10u);
        clock_values[0] = (uint64_t)UINT32_MAX + 10u; clock_values[1] = clock_values[0] + 100u;
        out = sample(); assert(out.arrival_last_cycles == clock_values[1] - 1u);
        assert(out.arrival_last_cycles > UINT32_MAX && out.arrival_max_cycles == out.arrival_last_cycles);
        publish(2u, clock_values[1] - 1u, 0x10u); out = sample();
        assert(out.arrival_last_cycles == 1u && out.arrival_max_cycles > UINT32_MAX);
        clock_values[0] = UINT32_MAX - 2ull; clock_values[1] = (uint64_t)UINT32_MAX + 3u;
        out = sample(); assert(out.body_last_cycles == 5u && out.body_max_cycles == 100u);
    } else if (!strcmp(mode, "guard_getter")) {
        publish(1u, 900u, 0x10u); out = sample();
        vdc_priority_ingress_snapshot_t sentinel; memset(&sentinel, 0xa5, sizeof(sentinel)); out = sentinel;
        assert(!vdc_dpll_manager_get_priority_ingress(NULL));
        ++s_priority_ingress_guard;
        assert(!vdc_dpll_manager_get_priority_ingress(&out) && !memcmp(&out, &sentinel, sizeof(out)));
        ++s_priority_ingress_guard; collision_guard = &s_priority_ingress_guard;
        collision_word = &s_priority_ingress_words[3];
        assert(!vdc_dpll_manager_get_priority_ingress(&out) && !memcmp(&out, &sentinel, sizeof(out)));
        assert(!collision_word && vdc_dpll_manager_get_priority_ingress(&out));
        expect_raw(&out, 1u);
    } else if (!strcmp(mode, "ordinary_and_unknown")) {
        publish(1u, 900u, 0x10u); out = sample(); expect_raw(&out, 1u);
        assert(out.last_class == 0x10u);
        publish(2u, 900u, 0x7fu); out = sample(); expect_raw(&out, 2u);
        assert(out.last_class == 0x7fu && out.copied_count == 2u);
    } else if (!strcmp(mode, "counter_saturation")) {
        out = sample(); s_priority_ingress.service_count = UINT32_MAX;
        s_priority_ingress.empty_polls = UINT32_MAX; s_priority_ingress.timing_samples = UINT32_MAX;
        out = sample(); assert(out.service_count == UINT32_MAX && out.empty_polls == UINT32_MAX);
        assert(out.timing_samples == UINT32_MAX);
        s_priority_ingress.copied_count = UINT32_MAX; publish(1u, 900u, 0x10u); out = sample();
        assert(out.copied_count == UINT32_MAX);
        s_priority_ingress.duplicate_polls = UINT32_MAX; out = sample();
        assert(out.duplicate_polls == UINT32_MAX);
        s_priority_ingress.clock_failures = UINT32_MAX; clock_valid[0] = false; out = sample();
        assert(out.clock_failures == UINT32_MAX);
    } else assert(!"unknown test case");
    puts("real priority ingress passed"); return 0;
}
'''
