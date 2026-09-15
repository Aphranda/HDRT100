"""Exercise the offline audit with bytes emitted by the production C recorder."""
import copy
from pathlib import Path
import shutil
import struct
import subprocess

import pytest

from tools.calibration_ring_validate import tdma_board_record as record
from tools.calibration_ring_validate import tdma_observation_audit as audit

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "diagnostics_tdma_record.h"
#include "ota_crc32.h"
static uint8_t bytes[16384];
static uint32_t used;
static uint64_t now=100;
static FILE *input;
extern uint32_t pota_crc32_update(uint32_t, const void *, size_t);
uint32_t ota_crc32_update(uint32_t crc,const uint8_t *p,size_t n) {
    return pota_crc32_update(crc,p,n);
}
static bool begin(uint32_t epoch,uint32_t *cap) { assert(epoch==123); *cap=sizeof(bytes); return true; }
static bool append(uint32_t off,const uint8_t *p,size_t n) {
    assert(off==used && used+n<=sizeof(bytes)); memcpy(bytes+used,p,n); used+=(uint32_t)n; return true;
}
static uint32_t snapshot(uint32_t *words) {
    assert(fread(words,4,DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS,input)==DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS);
    now+=10; return DIAGNOSTICS_TDMA_RECORD_VALID;
}
static uint64_t clock_now(void) { return now; }
static void identity(uint64_t *build,uint64_t *board) { *build=1; *board=2; }
static const diagnostics_tdma_record_port_t port={begin,append,NULL,NULL,snapshot,clock_now,identity};
int main(int argc,char **argv) {
    assert(argc==3); input=fopen(argv[1],"rb"); assert(input);
    assert(diagnostics_tdma_record_arm(123,1000,4)); diagnostics_tdma_record_service(&port);
    now=1000; diagnostics_tdma_record_start(now);
    for(unsigned i=0;i<4;i++) { diagnostics_tdma_record_service(&port); now+=1000; }
    diagnostics_tdma_record_status_t status;
    assert(diagnostics_tdma_record_status(&status) && status.state==TDMA_RECORD_FROZEN);
    assert(status.written==4 && status.missed==0 && status.reason==0);
    FILE *out=fopen(argv[2],"wb"); assert(out);
    assert(fwrite(bytes,1,used,out)==used); fclose(out); fclose(input);
}
'''


@pytest.fixture(scope="module")
def encoder(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("observation-audit")
    source, exe = tmp / "record.c", tmp / "record.exe"
    source.write_text(HARNESS, encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe",
                    "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "components/diagnostics/inc"),
                    "-I", str(ROOT / "components/ota_manager/inc"),
                    "-I", str(ROOT / "third_party/portable_ota/include"), str(source),
                    str(ROOT / "components/diagnostics/src/diagnostics_tdma_record.c"),
                    str(ROOT / "third_party/portable_ota/src/pota_crc32.c"), "-o", str(exe)],
                   check=True, capture_output=True, text=True)

    def encode(snapshots):
        words = []
        for snapshot in snapshots:
            for kind, group, name in record.field_schema():
                value = snapshot.get(group, {}).get(name, 0)
                words.append(value & audit.U32_MASK)
                if kind == "U64":
                    words.append(value >> 32)
        (tmp / "input.bin").write_bytes(struct.pack(f"<{len(words)}I", *words))
        subprocess.run([str(exe), str(tmp / "input.bin"), str(tmp / "record.bin")],
                       check=True, capture_output=True)
        return (tmp / "record.bin").read_bytes()
    return encode


def snapshots(*, node=0, autonomous=True):
    result = []
    for i in range(5):
        auto = node == 0 and autonomous and i >= 3
        persona = 16 if auto else (11 if node == 0 else 13)
        sequence, count = 100 + i, 10 + i
        result.append(dict(
            runtime=dict(ring_enabled=1, ring_adapter_started=1, ring_up_running=1,
                         ring_down_running=1, ring_node_count=4, ring_local_slot_id=node,
                         ring_reference_slot_id=0, ring_config_seq=7, ring_applied_config_seq=7,
                         ring_up_tx_sequence=sequence, ring_down_rx_sequence=sequence,
                         ring_up_tx_frame_crc32=sequence * 11, ring_down_rx_frame_crc32=sequence * 11,
                         ring_adapter_service_count=count, ring_adapter_tx_count=12 if auto else count,
                         ring_adapter_rx_count=count, ring_last_error=5 if auto else 0,
                         ring_timestamp_resolution_ns=0 if auto else 8),
            physical=dict(program_persona=persona, program_target_persona=persona,
                          program_lifecycle_state=5, armed=1, rx_count=count),
            process=dict(configured=1, active=1, local_slot=node, receive_configured=1,
                         receive_state=1, receive_accepted_segment_mask=14,
                         receive_expected_segment_mask=14, receive_accepted_wkc=3,
                         receive_expected_wkc=3, receive_accepted_map_generation=2,
                         map_generation=2, receive_accepted_payload_size=132, payload_size=132,
                         receive_accepted_sequence=sequence, receive_accepted_identity_crc32=sequence * 11,
                         receive_accepted_count=count, receive_image_generation=count,
                         comm_fsm_state=5 if auto else (2 if node == 0 else 1)),
            fifo=dict(rx_publish_count=count, rx_acquire_count=count, rx_release_count=count,
                      tx_acquire_count=count), schedule=dict(sys_clock_hz=250000000, phase_count=0)))
    return result


def handoff():
    stages = [0, 0, 0]
    for i in range(1, 9):
        stages += [i * 10, 2, 1]
    stages += [0] * 6
    return ','.join(map(str, ["ORIGINHANDOFF", 1, 9, 7, 250000000, 2, 0, 90, 100, 11] + stages))


def test_handoff_ready_schema_preserves_history_and_requires_release_stage():
    kwargs = dict(trial_epoch=9, config_seq=7, clock_hz=250000000)
    assert audit.handoff_context(handoff(), **kwargs)
    fields = handoff().split(',')
    fields[1], fields[7], fields[9] = '2', '120', '12'
    fields += ['90', '2', '2']
    assert audit.handoff_context(','.join(fields), **kwargs)['elapsed_ticks'] == 120
    for position, value in [(1, '1'), (9, '11'), (45, '0'), (43, '79'), (44, '31')]:
        bad = fields.copy()
        bad[position] = value
        with pytest.raises(ValueError):
            audit.handoff_context(','.join(bad), **kwargs)
    with pytest.raises(ValueError):
        audit.handoff_context(','.join(fields[:-1]), **kwargs)


def run(encoder, samples=None, **overrides):
    kwargs = dict(expected_build=1, expected_board=2, expected_epoch=123, config_seq=7,
                  node_index=0, node_count=4, interval_us=1000, sample_count=4,
                  handoff_raw=handoff(), trial_epoch=9, clock_hz=250000000)
    kwargs.update(overrides)
    return audit.audit_record(encoder(snapshots() if samples is None else samples), **kwargs)


def test_autonomous_tx_plateau_requires_real_return_progress(encoder):
    result = run(encoder)
    assert result["observation_checks_passed"], result["issues"]
    assert len(result["intervals"]) == 4 and result["autonomous_slots"] == [2, 3]
    assert result["time_input_issues"]
    assert all(result[key] is False for key in ("physical_cadence_proven", "per_sample_grant_proven",
               "timestamp_input_proven", "wcet_proven", "formal_lock_accepted"))


@pytest.mark.parametrize("node", [0, 1, 2, 3])
def test_ordinary_origin_and_followers_still_require_tx_progress(encoder, node):
    samples = snapshots(node=node, autonomous=False)
    result = run(encoder, samples, node_index=node, handoff_raw=None)
    assert result["observation_checks_passed"], result["issues"]
    samples[3]["runtime"]["ring_adapter_tx_count"] = samples[2]["runtime"]["ring_adapter_tx_count"]
    result = run(encoder, samples, node_index=node, handoff_raw=None)
    assert any(x["reason"] == "runtime.ring_adapter_tx_count:stalled" for x in result["issues"])


@pytest.mark.parametrize("group,field,value,reason", [
    ("runtime", "ring_down_rx_sequence", 102, "feedback_sequence_mismatch"),
    ("runtime", "ring_adapter_rx_count", 12, "runtime.ring_adapter_rx_count:stalled"),
    ("physical", "rx_count", 12, "physical.rx_count:stalled"),
    ("process", "receive_accepted_count", 12, "process.receive_accepted_count:stalled"),
    ("process", "receive_accepted_sequence", 102, "process.receive_accepted_sequence:stalled"),
    ("process", "receive_image_generation", 12, "process.receive_image_generation:stalled"),
    ("process", "receive_accepted_identity_crc32", 123, "accepted_identity_mismatch"),
    ("runtime", "ring_up_tx_frame_crc32", 123, "feedback_identity_mismatch"),
    ("process", "receive_accepted_map_generation", 1, "receive_map_generation_mismatch"),
    ("runtime", "ring_applied_config_seq", 6, "ring_applied_config_seq_mismatch"),
    ("physical", "program_lifecycle_state", 0, "physical_program_lifecycle_state_mismatch"),
    ("process", "comm_fsm_state", 2, "autonomous_fsm_mismatch"),
    ("process", "map_generation", 3, "map_changed_in_frozen_config"),
    ("process", "comm_fsm_last_error", 1, "comm_fsm_error"),
    ("process", "receive_missing_count", 1, "process.receive_missing_count:grew"),
    ("process", "receive_rejected_count", 1, "process.receive_rejected_count:grew"),
    ("runtime", "ring_adapter_rx_count", 1, "runtime.ring_adapter_rx_count:reset_or_backward"),
    ("runtime", "ring_last_error", 8, "ring_transport_error"),
])
def test_intermittent_failure_cannot_be_hidden_by_later_recovery(encoder, group, field, value, reason):
    samples = snapshots()
    samples[3][group][field] = value
    result = run(encoder, samples)
    assert not result["observation_checks_passed"]
    assert dict(slot=2, reason=reason) in result["issues"]


def test_no_input_and_no_context_cannot_pass(encoder):
    samples = snapshots()
    samples[4] = copy.deepcopy(samples[3])
    result = run(encoder, samples)
    assert not result["observation_checks_passed"]
    result = run(encoder, handoff_raw=None)
    assert dict(slot=2, reason="persona_without_matching_context") in result["issues"]


def test_startup_is_retained_and_window_cannot_be_shortened(encoder):
    samples = snapshots()
    samples[1]["runtime"]["ring_down_running"] = 0
    samples[1]["process"]["receive_rejected_count"] = 1
    result = run(encoder, samples, sample_count=5)
    assert dict(slot=0, reason="ring_down_running_mismatch") in result["issues"]
    assert dict(slot=0, reason="process.receive_rejected_count:grew") in result["issues"]
    assert dict(slot=None, reason="capture_window_incomplete") in result["issues"]


def test_cross_group_skew_is_reported_without_inventing_identity_match(encoder):
    samples = snapshots()
    samples[3]["process"]["receive_accepted_sequence"] += 1
    samples[4]["process"]["receive_accepted_sequence"] += 1
    result = run(encoder, samples)
    assert result["observation_checks_passed"], result["issues"]
    assert result["identity_comparisons"][2] == dict(slot=2, comparable=False)


def test_forward_wrap_is_allowed_but_half_range_jump_is_not(encoder):
    samples = snapshots()
    for i, sample in enumerate(samples):
        seq = (audit.U32_MASK - 2 + i) & audit.U32_MASK
        sample["runtime"]["ring_up_tx_sequence"] = seq
        sample["runtime"]["ring_down_rx_sequence"] = seq
        sample["process"]["receive_accepted_sequence"] = seq
    assert run(encoder, samples)["observation_checks_passed"]
    samples[3]["runtime"]["ring_adapter_rx_count"] += 0x80000000
    assert not run(encoder, samples)["observation_checks_passed"]


@pytest.mark.parametrize("overrides", [dict(expected_build=3), dict(expected_board=3),
    dict(expected_epoch=124), dict(trial_epoch=8), dict(config_seq=8), dict(clock_hz=125000000),
    dict(handoff_raw='UNAVAILABLE')])
def test_foreign_capture_or_handoff_is_rejected(encoder, overrides):
    with pytest.raises(ValueError):
        run(encoder, **overrides)


def test_native_crc_cannot_be_bypassed_with_copied_decoded_json(encoder):
    data = bytearray(encoder(snapshots()))
    data[200] ^= 1
    with pytest.raises(ValueError, match="CRC/length mismatch"):
        audit.audit_record(bytes(data), expected_build=1, expected_board=2, expected_epoch=123,
                           config_seq=7, node_index=0, node_count=4, interval_us=1000, sample_count=4)
