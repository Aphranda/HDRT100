"""Real Core1 append helpers -> binary capture -> independent Python decoder."""
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_owner import ROOT, compile_executable, function_body
from tools.dpll_observation_decode.dpll_observation_decode import HEADER, MAGIC, decode


@pytest.fixture(scope="module")
def capture_exe(tmp_path_factory):
    source = (ROOT / "components/vdc_dpll_manager/src/vdc_boundary_capture.inc").read_text(encoding="utf-8")
    manager = (ROOT / "components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(encoding="utf-8")
    functions = '\n'.join(signature + '{' + function_body(manager, name) + '}' for name, signature in (
        ('vdc_dpll_manager_dpll_capture_arm', 'bool vdc_dpll_manager_dpll_capture_arm(void)'),
        ('vdc_dpll_manager_dpll_capture_stop', 'bool vdc_dpll_manager_dpll_capture_stop(void)'),
        ('vdc_dpll_manager_publish_runtime_snapshot_locked', 'static void vdc_dpll_manager_publish_runtime_snapshot_locked(void)')))
    return compile_executable(tmp_path_factory.mktemp("auto-capture"), "capture",
                              PREAMBLE + source + functions + CASES,
                              [ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def run_capture(capture_exe, case):
    result = subprocess.run([str(capture_exe), case], capture_output=True, timeout=5)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    return result.stdout


def read_records(tmp_path, records, schema=6):
    path = tmp_path / "capture.bin"
    path.write_bytes(HEADER.pack(MAGIC, schema, 100, len(records) // 100, 0, 100, 100,
                                zlib.crc32(records)) + records)
    return decode(path, "NO1")


def test_actual_auto_helpers_roundtrip_and_hold_dedup(capture_exe, tmp_path):
    decoded = read_records(tmp_path, run_capture(capture_exe, "flow"))
    samples = decoded["samples"]["NO1"]
    assert [s["capture_kind"] for s in samples] == [
        "auto_observation", "auto_offer", "auto_apply", "auto_ack", "auto_hold"]
    assert all("dpll_vector" not in s for s in samples)
    offer, applied, ack = [s["auto_control"] for s in samples[1:4]]
    assert offer["command"] == applied["command"]
    assert offer["command"]["signed_delta_rate_ppb"] == -249
    assert applied["actual_rate_ppb"] == -249 and applied["dco_update_seq"] == 18
    assert (offer["error_ppb_lo"], offer["error_ppb_hi"]) == (999, 1001)
    feedback = ack["feedback"]
    assert feedback == dict(source_slot=1, target_slot=0, clock_epoch_id=5,
        clock_run_id=6, observer_epoch=7, measurement_sequence=103,
        tick_hz=250000000, source_arm_epoch=0x123456789abcdef,
        absolute_output_ns_lo=12345678999, coordinate_ns=22345678999,
        model_token=4, applied_command_seq=1, control_session=99)
    assert ack["command_seq"] == 1 and ack["prior_model_token"] == 3


@pytest.mark.parametrize("case", ["disarmed", "capacity", "full", "hold_peers", "legacy_gate"])
def test_capture_bounded_lifecycle_and_legacy_gate(capture_exe, case):
    run_capture(capture_exe, case)


@pytest.mark.parametrize("case", ["auto_mode", "legacy_mode", "busy_arm", "already_armed"])
def test_actual_capture_arm_freezes_mode_and_preserves_control(capture_exe, case):
    run_capture(capture_exe, case)


@pytest.mark.parametrize("offset,value,fmt", [
    (20, 99, "I"), (0, 2, "I"), (96, 2, "I"), (8, 0, "q"),
    (100+16, 4, "I"), (100+96, 7, "I"), (100+32+16, 100, "I"),
    (100+32+40, 999, "I"), (100+32+62, 2, "B"),
    (300+32+24, 999, "I"),
])
def test_decoder_rejects_corrupt_semantics_even_with_valid_file_crc(capture_exe, tmp_path, offset, value, fmt):
    records = bytearray(run_capture(capture_exe, "flow"))
    struct.pack_into("<"+fmt, records, offset, value)
    with pytest.raises(ValueError):
        read_records(tmp_path, records)


def test_decoder_rejects_orphan_pair(capture_exe, tmp_path):
    records = run_capture(capture_exe, "flow")
    with pytest.raises(ValueError, match="orphan"):
        read_records(tmp_path, records[:100])
    orphan = bytearray(records[100:200])
    struct.pack_into("<I", orphan, 0, 1)
    with pytest.raises(ValueError, match="orphan"):
        read_records(tmp_path, orphan)


PREAMBLE = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "vdc_domain.h"
static vdc_domain_context_t s_vdc_domain;
static vdc_domain_snapshot_t s_published_snapshot;
static uint32_t s_published_snapshot_guard,s_published_dpll_update_seq;
static bool s_published_snapshot_valid,s_dpll_capture_auto_only;
static uint32_t s_vdc_follower_capture_kind_hint,s_dpll_capture_first_update_seq;
static bool auto_requested, mode_available=true;
bool vdc_dpll_manager_try_boundary_auto_enabled(bool *out) {
    if(!mode_available) return false;
    *out=auto_requested; return true;
}
bool vdc_dpll_manager_try_local_follow_enabled(bool *out) { *out=false;return true; }
static void osal_critical_enter(void) {}
static void osal_critical_exit(void) {}
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_MASTER 1u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_COMMAND 2u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_STATE 3u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_EVIDENCE 4u
static vdc_dpll_manager_dpll_capture_record_t s_dpll_capture_records[VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES];
static bool s_dpll_capture_armed=true, s_dpll_capture_complete;
static uint32_t s_dpll_capture_count, s_dpll_capture_dropped;
static uint32_t s_dpll_capture_start_ms, s_dpll_capture_end_ms;
static uint32_t s_dpll_capture_last_update_seq=17;
static uint32_t board_uptime_ms(void) { return 100u; }
'''

CASES = r'''
int main(int argc, char **argv) {
    (void)vdc_boundary_capture_local_core1;
    assert(argc==2);
    refmem_sync_vdc_boundary_command_t c = {.target_arm_epoch=0x123456789abcdefULL,
        .basis_source_output_ns_lo=12345678900ULL,.control_session=99,.command_seq=1,
        .schedule_crc32=33,.target_clock_epoch_id=5,.target_clock_run_id=6,
        .target_observer_epoch=7,.basis_measurement_sequence=102,
        .expected_target_model_token=3,.expected_applied_command_seq=0,
        .signed_delta_rate_ppb=-249,.schema_version=3,.source_slot=0,.target_slot=1,.flags=3};
    vdc_feedback_match_snapshot_t m = {.has_pair=1,.reserved=3,.raw_ppb_lo=999,.raw_ppb_hi=1001,
        .pairs={{.rx_elapsed_cycles=100,.rx_width_ns=1,.reference_tx_lo=100,.reference_tx_hi=100,
                 .source_model_token=3,.reference_identity_crc32=9,.measurement_sequence=1},
                {.rx_elapsed_cycles=1000001100,.rx_width_ns=1,.reference_tx_lo=1000000100,
                 .reference_tx_hi=1000000100,.source_model_token=3,.reference_identity_crc32=9,
                 .measurement_sequence=102}}};
    refmem_sync_vdc_feedback_record_t f = {.source_arm_epoch=c.target_arm_epoch,
        .source_clock_epoch_id=5,.source_clock_run_id=6,.observer_epoch=7,.measurement_sequence=103,
        .tick_hz=250000000,.schema_version=4,.source_slot=1,.target_slot=0,.domain_flags=0x1f,
        .rate={.absolute_output_ns_lo=12345678999ULL,.coordinate_ns=22345678999ULL,
               .model_token=4,.applied_command_seq=1,.control_session=99}};
    uint8_t wire[64];
    assert(refmem_sync_vdc_feedback_encode(&f,4,wire));
    if(!strcmp(argv[1],"auto_mode") || !strcmp(argv[1],"legacy_mode")) {
        s_dpll_capture_armed=false;
        auto_requested=!strcmp(argv[1],"auto_mode");
        assert(vdc_dpll_manager_dpll_capture_arm());
        const bool captured_auto=auto_requested;
        auto_requested=!auto_requested; /* Revocation cannot change capture kind. */
        for(unsigned i=1;i<200;++i) {
            s_vdc_domain.dpll.update_seq=i;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        }
        assert(s_published_snapshot.dpll.update_seq==199 && s_published_dpll_update_seq==199);
        if(captured_auto) {
            assert(s_dpll_capture_auto_only && s_dpll_capture_count==0 && s_dpll_capture_last_update_seq==0);
            vdc_boundary_capture_offer_core1(&c,&m);
            assert(s_dpll_capture_count==2);
        } else assert(!s_dpll_capture_auto_only && s_dpll_capture_count==VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES);
        assert(vdc_dpll_manager_dpll_capture_stop());
        assert(vdc_dpll_manager_dpll_capture_arm());
        assert(s_dpll_capture_auto_only==auto_requested && !s_dpll_capture_count);
    } else if(!strcmp(argv[1],"busy_arm") || !strcmp(argv[1],"already_armed")) {
        s_dpll_capture_armed=!strcmp(argv[1],"already_armed");
        mode_available=false; s_dpll_capture_auto_only=true;
        s_dpll_capture_count=5;s_dpll_capture_records[0].update_seq=123;
        assert(!vdc_dpll_manager_dpll_capture_arm());
        assert(s_dpll_capture_auto_only && s_dpll_capture_count==5 && s_dpll_capture_records[0].update_seq==123);
    } else if (!strcmp(argv[1],"disarmed")) {
        s_dpll_capture_armed=false;
        vdc_boundary_capture_offer_core1(&c,&m);
        vdc_boundary_capture_apply_core1(&c,-249,18);
        vdc_boundary_capture_ack_core1(wire,1,3);
        vdc_boundary_capture_hold_core1(99,1,0,&m);
        assert(!s_dpll_capture_count && !s_dpll_capture_dropped);
    } else if (!strcmp(argv[1],"capacity")) {
        s_dpll_capture_count=VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES-1;
        memset(&s_dpll_capture_records[s_dpll_capture_count],0x55,100);
        vdc_boundary_capture_offer_core1(&c,&m);
        assert(!s_dpll_capture_armed && s_dpll_capture_complete && s_dpll_capture_dropped==2);
        assert(s_dpll_capture_records[s_dpll_capture_count].update_seq==0x55555555);
    } else if (!strcmp(argv[1],"full")) {
        s_dpll_capture_count=VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES-2;
        vdc_boundary_capture_offer_core1(&c,&m);
        assert(!s_dpll_capture_armed && s_dpll_capture_complete && !s_dpll_capture_dropped);
        assert(s_dpll_capture_count==VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES);
    } else if (!strcmp(argv[1],"hold_peers")) {
        for(unsigned i=0;i<100;i++) for(unsigned peer=1;peer<4;peer++)
            vdc_boundary_capture_hold_core1(99,peer,0,&m);
        assert(s_dpll_capture_count==3);
        ++m.pairs[1].measurement_sequence;
        vdc_boundary_capture_hold_core1(99,1,0,&m);
        vdc_boundary_capture_hold_core1(100,1,0,&m);
        assert(s_dpll_capture_count==5);
    } else {
        vdc_boundary_capture_offer_core1(&c,&m);
        vdc_boundary_capture_apply_core1(&c,-249,18);
        vdc_boundary_capture_ack_core1(wire,1,3);
        for(unsigned i=0;i<100;i++) vdc_boundary_capture_hold_core1(99,1,0,&m);
        assert(s_dpll_capture_count==5 && !s_dpll_capture_dropped && s_dpll_capture_armed);
        assert(s_dpll_capture_last_update_seq==17 && s_dpll_capture_start_ms==100 && s_dpll_capture_end_ms==100);
        if(!strcmp(argv[1],"flow")) fwrite(s_dpll_capture_records,100,s_dpll_capture_count,stdout);
    }
    return 0;
}
'''
