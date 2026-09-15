"""Run the actual Core0 identity refresh and parser admission prefix.

The receiver/reset/copy library and mailbox CRC are real. The parser stops
immediately before fragment_push, so this does not prove full transport,
ingress fencing, or cancellation of previously unseen queued records.
"""
import subprocess

from test_vdc_command_owner import ROOT, REFMEM, compile_executable, function_body


def test_role_binding_and_parser_admission(tmp_path):
    source = REFMEM.read_text(encoding="utf-8")
    refresh = function_body(source, "distributed_refmem_refresh_vdc_command_context_from_snapshot")
    parser = function_body(source, "distributed_refmem_tdma_flight_parse_vdc_fragment")
    boundary = "refmem_sync_vdc_command_payload_t command;"
    assert boundary in parser
    admission = parser.split(boundary, 1)[0]
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
static struct {
    uint32_t vdc_command_transport_generation, vdc_command_transport_valid;
    uint32_t vdc_command_transport_mode, vdc_command_transport_source_slot;
    uint32_t vdc_command_fragment_reject_count;
    refmem_sync_vdc_fragment_context_t vdc_command_fragments;
} s_tdma_flight_sync;
/* Frame CRC32 is outside this harness; fail if unexpectedly invoked. */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{ (void)crc; (void)data; (void)length; assert(!"unexpected frame CRC"); return 0; }
uint32_t ota_crc32_compute(const uint8_t *data, size_t length)
{ (void)data; (void)length; assert(!"unexpected frame CRC"); return 0; }
static uint16_t distributed_refmem_get_le16(const uint8_t *data)
{ return (uint16_t)(data[0] | ((uint16_t)data[1] << 8u)); }
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
        ROOT / "components/distributed_refmem/src/refmem_sync.c",
        ROOT / "components/distributed_refmem/src/refmem_sync_frame.c",
    ])
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
