"""Execute Core0 role binding and complete RX FIFO command admission.

The full harness executes real FIFO publish/acquire/release, RefMem refresh,
mailbox parsing, fragment assembly, frame CRC and receiver retention. Owner
discovery is a stub. This proves the FIFO admission boundary; upstream DMA,
station work, complete later retransmissions and hardware DCO are outside it.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from test_vdc_command_owner import ROOT, REFMEM, compile_executable, function_body


def ingress_definition(source, name):
    """Copy a complete typed definition, including its production signature."""
    match = re.search(
        rf"(?m)^(?:static\s+)?[A-Za-z_]\w*\s+{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{",
        source)
    assert match, name
    cursor, depth = match.end(), 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


def build_full_ingress(directory, production_root=ROOT):
    """Execute complete production ingress; only owner discovery is a stub.

    The final fixture requires the admission-epoch API. The pre-fix run keeps
    its original generated full_ingress.c, executable and production snapshot
    in the evidence directory; use those artifacts to repeat that counterexample.
    production_root selects the source snapshot to extract without editing it.
    """
    def production(path):
        candidate = production_root / path
        return candidate if candidate.is_file() else ROOT / path

    source = production("components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    service = production("components/tdma/src/tdma_service.c").read_text(encoding="utf-8")
    state = re.search(r"typedef struct \{[^}]*\} distributed_refmem_tdma_flight_sync_t;", source, re.S)
    assert state
    names = [
        "distributed_refmem_next_nonzero_sequence", "distributed_refmem_put_le16",
        "distributed_refmem_put_le32", "distributed_refmem_get_le16",
        "distributed_refmem_get_i16", "distributed_refmem_get_le32",
        "distributed_refmem_flight_input_offset_for_slot",
        "distributed_refmem_tdma_reset_resident_command_state",
        "distributed_refmem_tdma_sync_resident_command_identity",
        "distributed_refmem_refresh_vdc_command_context_from_snapshot",
        "distributed_refmem_tdma_flight_expand_compact_delta",
        "distributed_refmem_tdma_flight_parse_vdc_fragment",
        "distributed_refmem_tdma_flight_parse_mailbox",
        "distributed_refmem_tdma_flight_sync_receive",
    ]
    wrappers = ["tdma_service_acquire_flight_rx", "tdma_service_release_flight_rx"]
    if "tdma_service_core0_advance_flight_rx_admission_epoch" in service:
        wrappers.append("tdma_service_core0_advance_flight_rx_admission_epoch")
    crc_port = (ROOT / "middleware/portable_ota_port/src/portable_ota_core_port.c").read_text(encoding="utf-8")
    crc_api = (ROOT / "components/ota_manager/src/ota_crc32.c").read_text(encoding="utf-8")
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED 1
#include "refmem_sync.h"
#include "refmem_application_model.h"
#include "vdc_dpll_manager.h"
#include "tdma_process_image_layout.h"
#include "pota_types.h"
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE TDMA_FLIGHT_SHORT_SLOT_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE TDMA_FLIGHT_SHORT_PAYLOAD_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT TDMA_FLIGHT_SHORT_SLOT_COUNT
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC TDMA_FLIGHT_MAILBOX_MAGIC
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION TDMA_FLIGHT_MAILBOX_VERSION
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH 1u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN 1u
static refmem_sync_vdc_context_t s_vdc_command_context;
static bool s_vdc_command_context_initialized, s_vdc_command_context_ready;
static uint8_t s_vdc_command_context_local_slot;
static uint32_t s_vdc_command_context_epoch_id, s_vdc_command_context_run_id;
static uint32_t s_vdc_command_context_schedule_epoch;
static tdma_service_service_t s_owner;
static bool owner_available = true;
static tdma_service_service_t *tdma_runtime_owner_get(void)
{ return owner_available ? &s_owner : NULL; }
''' + state.group(0) + "\nstatic distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;\n"
    # The old refresh did not discover the owner; expose that harmless stub
    # in fixture setup too so both baseline and current builds use it.
    harness += "\n".join(ingress_definition(service, name) for name in wrappers)
    harness += "\n" + "\n".join(ingress_definition(crc_port, name) for name in (
        "portable_ota_port_crc32_update", "portable_ota_port_crc32_compute"))
    harness += "\n" + "\n".join(ingress_definition(crc_api, name) for name in (
        "ota_crc32_update", "ota_crc32_compute"))
    harness += "\n" + "\n".join(ingress_definition(source, name) for name in names)
    harness += r'''
static vdc_dpll_manager_refmem_snapshot_t identity;
static tdma_ring_runtime_snapshot_t ring;
static uint32_t rx_sequence;
static bool rebind_during_rx_copy;
static uint32_t copy_interleavings;
static void bind_identity(void)
{
    s_vdc_command_context_ready =
        distributed_refmem_refresh_vdc_command_context_from_snapshot(&identity);
    assert(s_vdc_command_context_ready);
    distributed_refmem_tdma_sync_resident_command_identity(&identity);
}
/* Only the separately compiled real FIFO redirects memcpy here. Refresh at
 * mid-copy models Core0 rebinding while Core1 publishes, without firmware hooks. */
void *ingress_fifo_memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *dst = destination;
    const unsigned char *src = source;
    size_t copied = 0;
    if (rebind_during_rx_copy && size == tdma_flight_payload_size(ring.node_count)) {
        for (; copied < size / 2; ++copied) dst[copied] = src[copied];
        rebind_during_rx_copy = false;
        identity.control_profile.generation = 4;
        bind_identity();
        ++copy_interleavings;
    }
    for (; copied < size; ++copied) dst[copied] = src[copied];
    return destination;
}
static void fixture(void)
{
    assert(tdma_runtime_owner_get() == &s_owner);
    assert(tdma_flight_fifo_init(&s_owner.flight_fifo));
    identity.schedule.local_slot_id = 2;
    identity.schedule.schedule_epoch = 7;
    identity.schedule.schedule_crc32 = 0xA5A5;
    identity.clock_epoch_id = 7; identity.clock_run_id = 8;
    identity.control_profile.valid = 1;
    identity.control_profile.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    identity.control_profile.follow_master_slot_id = 0;
    identity.control_profile.generation = 2;
    ring.local_slot_id = 2; ring.node_count = 6;
    s_tdma_flight_sync.local_slot = 2; s_tdma_flight_sync.node_count = 6;
    s_tdma_flight_sync.active_mask = 0x3f;
    assert(refmem_sync_delta_init(&s_tdma_flight_sync.context, 2, 1, 1));
    bind_identity();
}
static refmem_sync_vdc_command_payload_t command(uint32_t seq)
{
    refmem_sync_vdc_command_payload_t value = {0};
    value.version = REFMEM_SYNC_VDC_COMMAND_VERSION;
    value.source_slot = 0; value.target_slot = 2;
    value.control_generation = 77; value.command_seq = seq;
    value.schedule_crc32 = identity.schedule.schedule_crc32;
    value.epoch_id = identity.clock_epoch_id; value.run_id = identity.clock_run_id;
    value.effective_vdc_time_ns = 9000000000ull + seq;
    value.period_adjust_ppb = 123; value.phase_offset_ns = -45;
    value.lock_state = VDC_DOMAIN_LOCK_FREQ_LOCK; value.quality = 1;
    value.payload_crc32 = refmem_sync_vdc_command_payload_crc32(&value);
    assert(refmem_sync_vdc_command_payload_validate(&value, sizeof(value)));
    return value;
}
static void mailbox_header(uint8_t *mailbox, uint8_t source, uint8_t message_class,
                           uint16_t sequence)
{
    distributed_refmem_put_le16(mailbox, TDMA_FLIGHT_MAILBOX_MAGIC);
    mailbox[2] = TDMA_FLIGHT_MAILBOX_VERSION; mailbox[3] = message_class;
    mailbox[4] = source; mailbox[5] = 4;
    distributed_refmem_put_le16(&mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET], sequence);
}
static void mailbox_crc(uint8_t *mailbox)
{
    distributed_refmem_put_le16(&mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET],
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET));
}
static void enqueue_fragment(const refmem_sync_vdc_command_payload_t *value,
                             uint8_t fragment, uint16_t first_sequence)
{
    uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE] = {0};
    uint8_t *mailbox = &image[distributed_refmem_flight_input_offset_for_slot(0)];
    mailbox_header(mailbox, 0, TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS,
                   (uint16_t)(first_sequence + fragment));
    mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET] = fragment;
    mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET] = REFMEM_SYNC_VDC_FRAGMENT_COUNT;
    memcpy(&mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET],
           (const uint8_t *)value + fragment * REFMEM_SYNC_VDC_FRAGMENT_DATA_SIZE,
           REFMEM_SYNC_VDC_FRAGMENT_DATA_SIZE);
    mailbox_crc(mailbox);
    /* A distinct ordinary source shares this exact queued RX view. */
    mailbox = &image[distributed_refmem_flight_input_offset_for_slot(1)];
    const uint32_t seq = ++rx_sequence;
    mailbox_header(mailbox, 1, TDMA_PROCESS_IMAGE_MESSAGE_CLASS, (uint16_t)seq);
    distributed_refmem_put_le32(&mailbox[TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET], seq);
    distributed_refmem_put_le16(&mailbox[TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET], 1);
    distributed_refmem_put_le32(&mailbox[TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET], 1000 + seq);
    mailbox_crc(mailbox);
    assert(tdma_flight_fifo_core1_publish_rx(&s_owner.flight_fifo, image,
        tdma_flight_payload_size(ring.node_count), 31, seq, 3, 1000000 + seq, 0));
}
static void receive(void)
{
    distributed_refmem_tdma_flight_sync_receive(&s_owner, &ring);
    assert(s_tdma_flight_sync.rx_accept_count == rx_sequence);
    assert(s_tdma_flight_sync.last_value_u32 == 1000 + rx_sequence);
    assert(s_tdma_flight_sync.context.mirror[1].visible);
    assert(s_tdma_flight_sync.context.mirror[1].slot_seq == rx_sequence);
    assert(s_tdma_flight_sync.context.mirror[1].value_u32 == 1000 + rx_sequence);
    tdma_flight_fifo_snapshot_t snapshot;
    assert(tdma_flight_fifo_get_snapshot(&s_owner.flight_fifo, &snapshot));
    assert(snapshot.rx_queued_count == 0 && snapshot.rx_parse_count == 0);
    assert(snapshot.rx_release_count == snapshot.rx_publish_count);
}
int main(int argc, char **argv)
{
    assert(argc == 2); fixture();
    const bool same = !strncmp(argv[1], "same_", 5);
    const bool partial = !strcmp(argv[1], "same_partial");
    const bool skipped = !strcmp(argv[1], "skipped_roundtrip");
    const bool unavailable = !strcmp(argv[1], "owner_unavailable");
    const bool exhausted = !strcmp(argv[1], "exhausted");
    const bool epoch = !strcmp(argv[1], "epoch_change");
    const bool run = !strcmp(argv[1], "run_change");
    const bool schedule = !strcmp(argv[1], "schedule_change");
    const bool copying = !strcmp(argv[1], "publish_rebind");
    refmem_sync_vdc_command_payload_t old = command(41);
    /* Queue capacity is four views, less than a complete command. Keep its
     * previously unseen start in the old queue; deliver its suffix later. */
    const uint32_t first_epoch = s_owner.flight_fifo.rx_admission_epoch;
    rebind_during_rx_copy = copying;
    enqueue_fragment(&old, 0, 100);
    assert(copy_interleavings == (copying ? 1u : 0u));
    assert(!s_vdc_command_context.vdc_command[0].valid);
    assert(!s_tdma_flight_sync.vdc_command_fragments.active);
    bool prefix_drained = false;
    if (partial) {
        receive(); prefix_drained = true;
        assert(s_tdma_flight_sync.vdc_command_fragments.active);
    }
    if (unavailable || exhausted) {
        identity.control_profile.generation = 4;
        if (exhausted) s_owner.flight_fifo.rx_admission_epoch = UINT32_MAX;
        else owner_available = false;
        const refmem_sync_vdc_context_t before = s_vdc_command_context;
        s_vdc_command_context_ready =
            distributed_refmem_refresh_vdc_command_context_from_snapshot(&identity);
        assert(!s_vdc_command_context_ready);
        assert(s_tdma_flight_sync.vdc_command_rx_admission_epoch == 0);
        assert(memcmp(&before, &s_vdc_command_context, sizeof(before)) == 0);
        receive(); prefix_drained = true;
        assert(!s_tdma_flight_sync.vdc_command_fragments.active);
        if (unavailable) owner_available = true;
    } else if (epoch) ++identity.clock_epoch_id;
    else if (run) ++identity.clock_run_id;
    else if (schedule) ++identity.schedule.schedule_epoch;
    else if (!same && !copying) {
        identity.control_profile.generation = 3;
        identity.control_profile.follow_master_slot_id = 1;
        if (!skipped) bind_identity();
        identity.control_profile.generation = 4;
        identity.control_profile.follow_master_slot_id = 0;
    }
    if (!exhausted) bind_identity();
    if (same) {
        assert(s_owner.flight_fifo.rx_admission_epoch == first_epoch);
        if (partial) assert(s_tdma_flight_sync.vdc_command_fragments.active);
    } else if (!exhausted) {
        assert(s_owner.flight_fifo.rx_admission_epoch > first_epoch);
    }
    if (!prefix_drained) receive();
    /* Observe rejection before reassembly or command epoch checks could hide
     * the problem. Ordinary data from the same view already updated above. */
    assert(s_tdma_flight_sync.vdc_command_fragment_rx_count == (same ? 1u : 0u));
    for (uint8_t i = 1; i < REFMEM_SYNC_VDC_FRAGMENT_COUNT; ++i) {
        enqueue_fragment(&old, i, 100); receive();
    }
    refmem_sync_vdc_command_snapshot_t copied;
    assert(refmem_sync_vdc_copy_command_for_generation(&s_vdc_command_context,
        0, s_vdc_command_context.consumer_generation, &copied));
    printf("%s: old command valid=%u seq=%u accepted=%u ordinary=%u releases=%u expected_old=%u\n",
        argv[1], copied.valid, copied.command_seq, s_tdma_flight_sync.vdc_command_accept_count,
        s_tdma_flight_sync.rx_accept_count, s_owner.flight_fifo.rx_release_count, same ? 1u : 0u);
    fflush(stdout);
    assert(copied.valid == (same ? 1u : 0u));
    assert(s_tdma_flight_sync.vdc_command_accept_count == (same ? 1u : 0u));
    if (exhausted) {
        assert(s_owner.flight_fifo.rx_admission_epoch == UINT32_MAX);
        assert(s_tdma_flight_sync.vdc_command_rx_admission_epoch == 0);
        assert(!s_vdc_command_context_ready && copied.command_seq == 0);
        assert(!refmem_sync_vdc_copy_command_for_generation(&s_vdc_command_context,
            0, identity.control_profile.generation, &copied));
        assert(!distributed_refmem_refresh_vdc_command_context_from_snapshot(&identity));
    } else if (!same) {
        assert(copied.command_seq == 0);
        /* A fresh complete command remains admissible under the new role. */
        refmem_sync_vdc_command_payload_t fresh = command(42);
        for (uint8_t i = 0; i < REFMEM_SYNC_VDC_FRAGMENT_COUNT; ++i) {
            enqueue_fragment(&fresh, i, 200); receive();
        }
        assert(refmem_sync_vdc_copy_command_for_generation(&s_vdc_command_context,
            0, identity.control_profile.generation, &copied));
        assert(copied.valid && copied.command_seq == 42 && copied.control_generation == 77);
        assert(copied.effective_vdc_time_ns == fresh.effective_vdc_time_ns);
        assert(s_tdma_flight_sync.vdc_command_accept_count == 1);
    }
    return 0;
}
'''
    directory.mkdir(parents=True, exist_ok=True)
    harness_path = directory / "full_ingress.c"
    harness_path.write_text(harness, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler
    includes = [base / f"components/{component}/inc" for base in (production_root, ROOT)
                for component in ("tdma", "vdc_domain", "vdc_dpll_manager", "distributed_refmem",
                                  "calibration_manager", "ota_manager")]
    includes.append(ROOT / "third_party/portable_ota/include")
    linked = [production(path) for path in (
        "components/tdma/src/tdma_flight_fifo.c",
        "components/distributed_refmem/src/refmem_sync.c",
        "components/distributed_refmem/src/refmem_sync_frame.c",
        "third_party/portable_ota/src/pota_crc32.c")]
    exe = directory / ("full_ingress.exe" if os.name == "nt" else "full_ingress")
    common = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
              *[f"-I{path}" for path in includes]]
    fifo_object = directory / "fifo.o"
    fifo_compile = subprocess.run([
        *common, "-Dmemcpy=ingress_fifo_memcpy", "-c", str(linked[0]), "-o", str(fifo_object)],
        capture_output=True, text=True, timeout=60)
    (directory / "compile-fifo.stdout.txt").write_text(fifo_compile.stdout, encoding="utf-8")
    (directory / "compile-fifo.stderr.txt").write_text(fifo_compile.stderr, encoding="utf-8")
    assert fifo_compile.returncode == 0, fifo_compile.stdout + fifo_compile.stderr
    invocation = [*common, str(harness_path), str(fifo_object), *map(str, linked[1:]), "-o", str(exe)]
    result = subprocess.run(invocation, capture_output=True, text=True, timeout=60)
    (directory / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (directory / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.fixture(scope="module")
def full_ingress_executable(tmp_path_factory):
    return build_full_ingress(tmp_path_factory.mktemp("full-ingress"))


@pytest.mark.parametrize("scenario", [
    "roundtrip", "skipped_roundtrip", "same_generation", "same_partial",
    "owner_unavailable", "exhausted", "epoch_change", "run_change", "schedule_change",
    "publish_rebind",
])
def test_queued_command_role_admission(full_ingress_executable, scenario):
    result = subprocess.run([str(full_ingress_executable), scenario],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


def test_role_binding_and_parser_admission(tmp_path):
    source = REFMEM.read_text(encoding="utf-8")
    refresh = function_body(source, "distributed_refmem_refresh_vdc_command_context_from_snapshot")
    parser = function_body(source, "distributed_refmem_tdma_flight_parse_vdc_fragment")
    boundary = "refmem_sync_vdc_command_payload_t command;"
    assert boundary in parser
    admission = parser.split(boundary, 1)[0]
    service = (ROOT / "components/tdma/src/tdma_service.c").read_text(encoding="utf-8")
    advance = ingress_definition(service, "tdma_service_core0_advance_flight_rx_admission_epoch")
    harness = r'''
#include <assert.h>
#include <string.h>
#include "refmem_sync.h"
#include "vdc_dpll_manager.h"
#include "tdma_process_image_layout.h"
#define DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED 1
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE TDMA_FLIGHT_SHORT_SLOT_SIZE
static refmem_sync_vdc_context_t s_vdc_command_context;
static bool s_vdc_command_context_initialized, s_vdc_command_context_ready;
static uint8_t s_vdc_command_context_local_slot;
static uint32_t s_vdc_command_context_epoch_id, s_vdc_command_context_run_id;
static uint32_t s_vdc_command_context_schedule_epoch, admitted;
static tdma_service_service_t s_owner;
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &s_owner; }
static struct {
    uint32_t vdc_command_transport_generation, vdc_command_transport_valid;
    uint32_t vdc_command_transport_mode, vdc_command_transport_source_slot;
    uint32_t vdc_command_fragment_reject_count;
    uint32_t vdc_command_rx_admission_epoch;
    refmem_sync_vdc_fragment_context_t vdc_command_fragments;
} s_tdma_flight_sync;
/* Frame CRC32 is outside this harness; fail if unexpectedly invoked. */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{ (void)crc; (void)data; (void)length; assert(!"unexpected frame CRC"); return 0; }
uint32_t ota_crc32_compute(const uint8_t *data, size_t length)
{ (void)data; (void)length; assert(!"unexpected frame CRC"); return 0; }
static uint16_t distributed_refmem_get_le16(const uint8_t *data)
{ return (uint16_t)(data[0] | ((uint16_t)data[1] << 8u)); }
''' + advance + r'''
static bool refresh(const vdc_dpll_manager_refmem_snapshot_t *snapshot) {
''' + refresh + r'''
}
static void parser_admission(const uint8_t *mailbox, size_t mailbox_size) {
''' + admission + r'''
    ++admitted; /* Actual code would now enter fragment_push. */
}
static void begin_fragment(void)
{
    s_tdma_flight_sync.vdc_command_fragments.active = 1;
    s_tdma_flight_sync.vdc_command_fragments.next_fragment = 1;
    s_tdma_flight_sync.vdc_command_fragments.accepted_fragment_count = 8;
}
int main(void)
{
    assert(tdma_flight_fifo_init(&s_owner.flight_fifo));
    vdc_dpll_manager_refmem_snapshot_t snapshot = {0};
    snapshot.schedule.local_slot_id = 2;
    snapshot.schedule.schedule_epoch = 7;
    snapshot.clock_epoch_id = 7; snapshot.clock_run_id = 8;
    snapshot.control_profile.valid = 1;
    snapshot.control_profile.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    snapshot.control_profile.generation = 2;
    s_vdc_command_context_ready = refresh(&snapshot);
    assert(s_vdc_command_context_ready && s_vdc_command_context_initialized);
    assert(s_vdc_command_context.consumer_generation == 2);
    /* Existing accepted state: reception/order itself is tested in the C suite. */
    s_vdc_command_context.vdc_command[0] = (refmem_sync_vdc_command_snapshot_t){
        .valid = 1, .command_seq = 11, .frame_seq32 = 31, .control_generation = 77};
    begin_fragment();
    refmem_sync_vdc_context_t before = s_vdc_command_context;
    assert(refresh(&snapshot));
    assert(memcmp(&before, &s_vdc_command_context, sizeof(before)) == 0);
    assert(s_tdma_flight_sync.vdc_command_fragments.active == 1);
    assert(!refresh(NULL));
    snapshot.control_profile.valid = 0; assert(!refresh(&snapshot));
    snapshot.control_profile.valid = 1;
    snapshot.control_profile.generation = 0; assert(!refresh(&snapshot));
    snapshot.control_profile.generation = 2;
    snapshot.schedule.local_slot_id = REFMEM_SYNC_NODE_COUNT; assert(!refresh(&snapshot));
    snapshot.schedule.local_slot_id = 2;
    assert(memcmp(&before, &s_vdc_command_context, sizeof(before)) == 0);
    /* Core0 can miss the intermediate source B entirely. */
    snapshot.control_profile.generation = 4;
    assert(refresh(&snapshot));
    assert(s_vdc_command_context.consumer_generation == 4);
    assert(!s_vdc_command_context.vdc_command[0].valid);
    assert(s_vdc_command_context.vdc_command[0].command_seq == 11);
    assert(s_vdc_command_context.vdc_command[0].frame_seq32 == 31);
    assert(s_vdc_command_context.vdc_command[0].control_generation == 77);
    assert(!s_tdma_flight_sync.vdc_command_fragments.active);
    assert(s_tdma_flight_sync.vdc_command_fragments.accepted_fragment_count == 8);
    refmem_sync_vdc_command_snapshot_t copy;
    assert(!refmem_sync_vdc_copy_command_for_generation(&s_vdc_command_context, 0, 2, &copy));
    assert(refmem_sync_vdc_copy_command_for_generation(&s_vdc_command_context, 0, 4, &copy));
    assert(!copy.valid);
    /* An older Core0 role snapshot cannot grant Core1's current role. */
    snapshot.control_profile.generation = 2; assert(refresh(&snapshot));
    assert(!refmem_sync_vdc_copy_command_for_generation(&s_vdc_command_context, 0, 4, &copy));
    snapshot.control_profile.generation = 4; assert(refresh(&snapshot));
    begin_fragment(); snapshot.clock_run_id = 9;
    assert(refresh(&snapshot));
    assert(s_vdc_command_context.consumer_generation == 4);
    assert(s_vdc_command_context.active_run_id == 9);
    assert(s_vdc_command_context.vdc_command[0].command_seq == 0);
    assert(!s_tdma_flight_sync.vdc_command_fragments.active);
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE] = {0};
    mailbox[5] = 4;
    uint16_t crc = tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET);
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1] = (uint8_t)(crc >> 8);
    s_tdma_flight_sync.vdc_command_transport_valid = 1;
    s_tdma_flight_sync.vdc_command_transport_mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    s_tdma_flight_sync.vdc_command_transport_generation = 6;
    before = s_vdc_command_context; begin_fragment();
    parser_admission(mailbox, sizeof(mailbox));
    assert(!admitted && s_tdma_flight_sync.vdc_command_fragment_reject_count == 1);
    assert(!s_tdma_flight_sync.vdc_command_fragments.active);
    assert(memcmp(&before, &s_vdc_command_context, sizeof(before)) == 0);
    s_tdma_flight_sync.vdc_command_transport_generation = 4;
    parser_admission(mailbox, sizeof(mailbox)); assert(admitted == 1);
    s_vdc_command_context_ready = false; begin_fragment();
    parser_admission(mailbox, sizeof(mailbox)); assert(admitted == 1);
    assert(!s_tdma_flight_sync.vdc_command_fragments.active);
    return 0;
}
'''
    executable = compile_executable(tmp_path, "ingress", harness, [
        ROOT / "components/tdma/src/tdma_flight_fifo.c",
        ROOT / "components/distributed_refmem/src/refmem_sync.c",
        ROOT / "components/distributed_refmem/src/refmem_sync_frame.c",
    ])
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
