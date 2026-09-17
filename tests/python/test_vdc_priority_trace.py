"""Execute the real typed SRAM recorder, matcher and Domain DCO pipeline.

The same legacy capture array and production legacy append/read/ARM/STOP code
are linked to test storage exclusion. Only clocks and owner publications are
controlled. Python checks native bytes, rational interval arithmetic and CRC
independently; these tests do not establish hardware timing or physical lock.
"""
from fractions import Fraction
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import (
    DOMAIN_HARNESS, OWNER_PRELUDE, MATCH_STORAGE, OWNER_TESTS,
    EXTERNAL_INPUTS as FOLLOW_INPUTS, SCENARIOS as FOLLOW_SCENARIOS,
    domain_sources, ratio_oracle,
)


def production(path):
    return (ROOT / path).read_text(encoding="utf-8")


def real_crc_source():
    """Link the production CRC primitive and its two port forwarding layers."""
    primitive = production("third_party/portable_ota/src/pota_crc32.c").replace(
        '#include "pota_types.h"', '#include "' +
        (ROOT / "third_party/portable_ota/include/pota_types.h").as_posix() + '"')
    port = production("middleware/portable_ota_port/src/portable_ota_core_port.c")
    api = production("components/ota_manager/src/ota_crc32.c")
    return primitive + "\n" + "\n".join(ingress_definition(port, name) for name in (
        "portable_ota_port_crc32_update", "portable_ota_port_crc32_compute")) + "\n" + "\n".join(
        ingress_definition(api, name) for name in ("ota_crc32_update", "ota_crc32_compute"))


def recorder_base():
    """Real owner sources with only external publication and clock controls."""
    manager = production("components/vdc_dpll_manager/src/vdc_dpll_manager.c")
    physical = production("components/tdma/inc/tdma_pio_spi_phys.h")
    begin = physical.index("enum {\n    TDMA_EVENT_LIVE_RETAINED")
    end = physical.index("} tdma_pio_spi_event_exact_t;", begin) + len("} tdma_pio_spi_event_exact_t;")
    event_types = '#include "tdma_event_history.h"\n' + physical[begin:end]
    prelude = OWNER_PRELUDE.replace("EVENT_TYPES", event_types).replace(
        '#include <assert.h>', '#include <assert.h>\n#include <stddef.h>\n#include <inttypes.h>\n'
        '#include "vdc_priority_follow.h"\n#include "vdc_priority_rx.h"\n'
        '#include "vdc_priority_match.h"\n#include "vdc_priority_trace.h"\n'
        'static unsigned core;\nstatic unsigned get_core_num(void) { return core; }')
    for name in ("vdc_dpll_manager_publish_runtime_snapshot_locked", "vdc_boundary_capture_offer_core1",
                 "vdc_boundary_capture_apply_core1", "vdc_boundary_capture_ack_core1",
                 "vdc_boundary_capture_hold_core1", "vdc_boundary_capture_local_core1"):
        prelude = prelude.replace(ingress_definition(prelude, name), "")
    prelude = prelude.replace("{ (void)hz;(void)out;return false; }",
        '{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(vdc_timestamp_clock_bridge_t){'
        '.tick_hz=hz,.raw_before=raw_now,.raw_after=raw_now+1,.local_ns=now_ns};return true; }')
    # Storage declaration is copied verbatim: both producers address the same
    # actual legacy array instead of independent test buffers or busy flags.
    storage_start = manager.index("static vdc_dpll_manager_dpll_capture_record_t\n")
    storage_end = manager.index("static vdc_dpll_manager_waveform_storage_record_t", storage_start)
    storage = manager[storage_start:storage_end]
    defines = "\n".join(re.findall(r"(?m)^#define VDC_DPLL_MANAGER_DPLL_CAPTURE_.*$", manager))
    header = re.search(r"typedef struct __attribute__\(\(packed\)\) \{[^}]+\} vdc_dpll_manager_dpll_capture_header_t;", manager)
    assert header
    legacy = "\n".join(ingress_definition(manager, name) for name in (
        "vdc_dpll_manager_publish_runtime_snapshot_locked", "dpll_capture_arm_legacy",
        "dpll_capture_stop_legacy", "dpll_capture_read_legacy", "dpll_capture_save_legacy",
        "vdc_dpll_manager_dpll_capture_arm",
        "vdc_dpll_manager_dpll_capture_stop", "vdc_dpll_manager_get_dpll_capture_status",
        "vdc_dpll_manager_dpll_capture_read", "vdc_dpll_manager_dpll_capture_save"))
    matcher = production("components/vdc_dpll_manager/src/vdc_dpll_feedback_match.inc")
    source_type = re.search(r'typedef struct \{\s*uint64_t next_ordinal.*?\} vdc_feedback_match_source_t;', matcher, re.S)
    assert source_type
    helpers = matcher[matcher.index("static uint32_t match_inc"):matcher.index("/* Keep authorization")]
    follow_inputs = FOLLOW_INPUTS.replace(
        '{ if(!ring_available)return false;*out=ring;return true; }',
        '{ ++ring_reads;if(ring_read_hook)ring_read_hook(ring_reads);'
        'if(!ring_available)return false;*out=ring;return true; }')
    return prelude + TRACE_EXTERNALS + follow_inputs + defines + "\n" + header.group(0) + "\n" + storage + \
        real_crc_source() + "\n" + source_type.group(0) + MATCH_STORAGE + helpers, legacy


@pytest.fixture(scope="module")
def trace_executable(tmp_path_factory):
    prefix, legacy = recorder_base()
    harness = prefix + "\n" + legacy
    for name in ("vdc_model_feedback.inc", "vdc_boundary_capture.inc", "vdc_boundary_control.inc", "vdc_priority_match.inc"):
        harness += "\n" + production(f"components/vdc_dpll_manager/src/{name}")
    harness += r'''
static void priority_trace_decision_core1(const vdc_priority_follow_snapshot_t *decision);
#define VDC_PRIORITY_TRACE_DECISION_HOOK(snapshot) priority_trace_decision_core1(snapshot)
'''
    harness += production("components/vdc_dpll_manager/src/vdc_priority_follow.inc")
    harness += "\n#undef VDC_PRIORITY_TRACE_DECISION_HOOK\n"
    harness += production("components/vdc_dpll_manager/src/vdc_priority_trace.inc")
    harness += "\n" + ingress_definition(DOMAIN_HARNESS, "fixture")
    # Keep the tested real model fixture; replace only the wrapper's hooks to
    # use the same recorder ordering as the production Core1 service wrapper.
    helpers = FOLLOW_SCENARIOS[:FOLLOW_SCENARIOS.index("static void flow_test")]
    helpers = helpers.replace("OWNER_REMOTE_INPUT", "")
    helpers = helpers.replace("    vdc_priority_match_core1();\n    priority_follow_prepare_core1();",
        "    priority_trace_service_core1();\n    vdc_priority_match_core1();\n"
        "    priority_trace_match_core1();\n    priority_follow_prepare_core1();")
    harness += helpers + TRACE_CASES
    return compile_executable(tmp_path_factory.mktemp("native-trace"), "priority_trace", harness,
        domain_sources() + [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c",
                           ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def execute(executable, case):
    command = [str(executable), case]
    result = subprocess.run(command, capture_output=True, timeout=20)
    directory = executable.parent
    (directory / f"{case}.bin").write_bytes(result.stdout)
    (directory / f"{case}.json").write_text(json.dumps(dict(command=command,
        returncode=result.returncode, stderr=result.stderr.decode("utf-8", errors="replace"),
        stdout_bytes=len(result.stdout)), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")
    return result.stdout


STATUS_FIELDS = (
    "schema request_seq ack_seq command state reason capture_id session generation "
    "sample_interval_ms capacity record_count match_count decision_count skipped_count dropped_count "
    "first_ms last_ms freeze_ms ring_config_seq ring_applied_seq local_slot reference_slot node_count "
    "schedule_crc32 profile_crc32 path_table_crc32 path_crc32 delay_ns clock_epoch clock_run "
    "arm_epoch_lo arm_epoch_hi observer_epoch rx_epoch tick_hz mode"
).split()


def native_trace(raw):
    """Independent public-schema decoder, deliberately not the product tool."""
    assert len(STATUS_FIELDS) == 37
    assert len(raw) >= 168
    magic, schema, header_size, record_size, payload_crc = struct.unpack_from("<5I", raw)
    assert (magic, schema, header_size, record_size) == (0x52545056, 1, 168, 100)
    status = dict(zip(STATUS_FIELDS, struct.unpack_from("<37I", raw, 20), strict=True))
    assert status["schema"] == 1 and status["request_seq"] == status["ack_seq"]
    assert status["state"] == 3 and status["capacity"] == 76
    assert len(raw) == 168 + 100 * status["record_count"]
    assert zlib.crc32(raw[168:]) == payload_crc
    records = []
    for ordinal, offset in enumerate(range(168, len(raw), 100)):
        index, kind, uptime, event, carrier = struct.unpack_from("<5I", raw, offset)
        assert index == ordinal and kind in (1, 2)
        row = dict(index=index, kind=kind, uptime=uptime, event=event, carrier=carrier)
        if kind == 1:
            fields = ("raw_lo raw_hi residual_lo residual_hi local_lo local_hi remote_lo remote_hi "
                      "model_token dco_seq actual_ppb reason").split()
            values = struct.unpack_from("<QQqqQQQQIIiI", raw, offset + 20)
        else:
            fields = ("baseline model_token before_seq after_seq before_ppb after_ppb delta reason "
                      "error_lo error_hi expected_lo expected_hi local_lo local_hi").split()
            values = struct.unpack_from("<IIIIiiiIqqQQQQ", raw, offset + 20)
        row.update(zip(fields, values, strict=True))
        records.append(row)
    assert status["record_count"] == status["match_count"] + status["decision_count"]
    assert status["match_count"] == sum(row["kind"] == 1 for row in records)
    assert status["decision_count"] == sum(row["kind"] == 2 for row in records)
    return status, records


@pytest.mark.parametrize("case,rate", [("flow_fast", 6000), ("flow_slow", -6000), ("flow_hold", 0)])
def test_real_pipeline_native_values(trace_executable, case, rate):
    raw = execute(trace_executable, case)
    status, records = native_trace(raw)
    assert (status["capture_id"], status["session"], status["generation"]) == (1, 123, 101)
    assert (status["local_slot"], status["reference_slot"], status["node_count"]) == (1, 0, 4)
    assert status["sample_interval_ms"] == 200
    # Fixture reverse-DATA links are 0->1->2->3->0 at 80,81,82,83 ns.
    # The provisional forward-CS transpose from reference 0 to local 1
    # therefore uses the measured 1->2->3->0 reverse path.
    assert status["delay_ns"] == 81 + 82 + 83
    matches = [row for row in records if row["kind"] == 1]
    decisions = [row for row in records if row["kind"] == 2]
    assert [row["event"] for row in matches] == [100, 103, 200]
    assert len(decisions) == 1
    elapsed_by_sequence = {100: 0, 103: 200_000_000, 200: 1_500_000_000}
    for row in matches:
        elapsed = elapsed_by_sequence[row["event"]]
        raw_now = (3_000_000_000 + elapsed) // 4
        assert (row["raw_lo"], row["raw_hi"]) == (raw_now - 100, raw_now - 99)
        assert (row["remote_lo"], row["remote_hi"]) == (12_000_000_000 + elapsed, 12_000_000_007 + elapsed)
        # TIMER0's enclosed us observation gives the full 999 ns interval;
        # one raw tick of read placement and one tick of event width add 8 ns.
        local_times = (3_000_000_000 + elapsed - 404, 3_000_000_000 + elapsed + 603)
        output = tuple(5_000_000_000 + (t - 1234) +
                       int(Fraction((t - 1234) * rate, 10**9)) - 77 for t in local_times)
        assert (row["local_lo"], row["local_hi"]) == output
        assert row["residual_lo"] == output[0] - (row["remote_hi"] + status["delay_ns"])
        assert row["residual_hi"] == output[1] - (row["remote_lo"] + status["delay_ns"])
        assert row["actual_ppb"] == rate and row["model_token"] > 0 and row["reason"] == 0
    decision = decisions[0]
    assert (decision["baseline"], decision["event"], decision["carrier"]) == (100, 200, 203)
    first, last = matches[0], matches[-1]
    assert (decision["local_lo"], decision["local_hi"]) == (
        last["local_lo"] - first["local_hi"], last["local_hi"] - first["local_lo"])
    assert (decision["expected_lo"], decision["expected_hi"]) == (
        last["remote_lo"] - first["remote_hi"], last["remote_hi"] - first["remote_lo"])
    assert (decision["model_token"], decision["before_seq"], decision["before_ppb"]) == (
        last["model_token"], last["dco_seq"], last["actual_ppb"])
    assert (decision["error_lo"], decision["error_hi"]) == ratio_oracle(
        decision["local_lo"], decision["local_hi"], decision["expected_lo"], decision["expected_hi"])
    expected_delta = -1000 if rate > 0 else 1000 if rate < 0 else 0
    assert (decision["before_ppb"], decision["delta"], decision["after_ppb"]) == (rate, expected_delta, rate + expected_delta)
    assert decision["after_seq"] == decision["before_seq"] + bool(expected_delta)
    assert decision["reason"] == (0 if expected_delta else 9)


@pytest.mark.parametrize("case", ["request_ack", "release_ack", "legacy_exclusive", "legacy_active",
                                 "duplicate_busy", "generation", "cancel_before_ack", "cancel_after_ack",
                                 "read_pending", "read_busy", "read_bad_range", "read_changed_ring",
                                 "read_release_race", "read_restart_race", "read_core1_race",
                                 "ack_publication", "status_busy", "legacy_completed_reuse"])
def test_capture_owner_and_read_lifecycle(trace_executable, case):
    execute(trace_executable, case)


@pytest.mark.parametrize("case", ["empty", "capacity"])
def test_native_bounds_and_capacity(trace_executable, case):
    status, records = native_trace(execute(trace_executable, case))
    if case == "empty":
        assert not records and status["record_count"] == 0
    else:
        assert len(records) == 76 and status["reason"] == 4
        assert status["record_count"] <= status["capacity"]
        assert status["dropped_count"] == 1


def test_decimation_uses_raw_time_across_uptime_wrap(trace_executable):
    status, rows = native_trace(execute(trace_executable, "uptime_wrap"))
    assert [row["event"] for row in rows] == [100, 102]
    assert rows[1]["raw_lo"] - rows[0]["raw_lo"] == 50_000_000
    assert rows[0]["uptime"] == (1 << 32) - 50 and rows[1]["uptime"] == 150
    assert status["skipped_count"] == 1


TRACE_EXTERNALS = r'''
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static vdc_domain_snapshot_t s_published_snapshot;
static uint32_t s_published_snapshot_guard,s_published_dpll_update_seq;
static bool s_published_snapshot_valid;
static unsigned ring_reads;
static void (*ring_read_hook)(unsigned);
static void (*atomic_store_hook)(const void *);
/* Single-thread interleavings at actual production release-store boundaries.
 * The builtin still executes with its original type and memory order. */
#define __atomic_store_n(pointer,value,order) ({ \
    __atomic_store_n((pointer),(value),(order)); \
    if(atomic_store_hook)atomic_store_hook((const void *)(pointer)); \
})
static void osal_critical_enter(void) {}
static void osal_critical_exit(void) {}
static unsigned storage_calls;
static bool storage_manager_begin_file_write(const char *path,uint32_t size,uint32_t crc,uint32_t *txn)
{ assert(path && txn);(void)size;(void)crc;++storage_calls;return false; }
static bool storage_manager_write_file_chunk(uint32_t txn,uint32_t offset,const uint8_t *data,size_t size)
{ (void)txn;(void)offset;(void)data;(void)size;++storage_calls;return false; }
static bool storage_manager_commit_file_write(uint32_t txn,uint32_t *job)
{ (void)txn;(void)job;++storage_calls;return false; }
static bool storage_manager_abort_file_write(uint32_t txn)
{ (void)txn;++storage_calls;return false; }
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{
    ++ring_reads;
    if(ring_read_hook)ring_read_hook(ring_reads);
    if(!ring_available)return false;
    *out=(tdma_ring_runtime_snapshot_t){.enabled=ring.enabled,.adapter_started=ring.adapter_started,
        .config_seq=ring.config_seq,.applied_config_seq=ring.applied_config_seq};
    return true;
}
'''


TRACE_CASES = r'''
static vdc_priority_trace_status_t trace_status(void)
{
    vdc_priority_trace_status_t out;
    assert(vdc_dpll_manager_get_priority_trace(&out));return out;
}
static void stopped_ring(void)
{ ring.enabled=ring.adapter_started=ring.data_enabled=0;stopped=true;core=0; }
static void running_ring(void)
{ ring.enabled=ring.adapter_started=ring.data_enabled=1;stopped=false;core=1; }
static void trace_service(void)
{
    const unsigned saved=core;core=1;priority_trace_service_core1();core=saved;
}
static void armed_trace(uint32_t capture)
{
    stopped_ring();assert(vdc_dpll_manager_priority_trace_arm(capture));
    assert(trace_status().request_seq!=trace_status().ack_seq);
    trace_service();
    const vdc_priority_trace_status_t s=trace_status();
    assert(s.request_seq==s.ack_seq && s.state==VDC_PRIORITY_TRACE_ARMED && s.capture_id==capture);
}
static void frozen_trace(void)
{
    stopped_ring();
    if(trace_status().state!=VDC_PRIORITY_TRACE_FROZEN) {
        assert(vdc_dpll_manager_priority_trace_stop());
        assert(trace_status().request_seq!=trace_status().ack_seq);trace_service();
    }
    assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN);
    assert(trace_status().request_seq==trace_status().ack_seq);
}
static void reject_read(uint32_t capture,uint32_t offset,uint32_t size)
{
    uint8_t buffer[128],saved[128];memset(buffer,0xa5,sizeof(buffer));memcpy(saved,buffer,sizeof(saved));
    uint32_t total=0x12345678u,crc=0x87654321u;
    assert(!vdc_dpll_manager_priority_trace_read(capture,offset,buffer,size,&total,&crc));
    assert(total==0x12345678u && crc==0x87654321u && !memcmp(buffer,saved,sizeof(saved)));
}
static void export_trace(void)
{
    core=0;const vdc_priority_trace_status_t s=trace_status();
    const uint32_t expected=168u+s.record_count*100u;
    uint32_t offset=0,identity=0,crc=0,total=0,file_crc=0,page=0;
    while(offset<expected) {
        uint32_t n=(page++%3u)==0?1u:(page%3u)==2?37u:128u;
        if(n>expected-offset)n=expected-offset;
        uint8_t bytes[128];
        assert(vdc_dpll_manager_priority_trace_read(s.capture_id,offset,bytes,n,&total,&file_crc));
        if(!offset)identity=file_crc;
        assert(file_crc==identity && total==expected);
        crc=ota_crc32_update(crc,bytes,n);
        assert(fwrite(bytes,1,n,stdout)==n);offset+=n;
    }
    assert(crc==identity);
    fprintf(stderr,"FILE_CRC %u %u\n",expected,identity);
}
static void flow_trace(const char *name)
{
    setup(!strcmp(name,"flow_slow")?-6000:!strcmp(name,"flow_hold")?0:6000);
    armed_trace(1);running_ring();
    event(100,0);tick();
    event(101,100000000u);tick();
    event(102,199999996u);tick();
    event(103,200000000u);tick();
    event(200,1500000000u);tick();
    const vdc_priority_trace_status_t before=trace_status();
    assert(before.match_count==3u && before.decision_count==1u && before.skipped_count==2u);
    for(unsigned i=0;i<5u;++i)tick();
    assert(trace_status().record_count==before.record_count && trace_status().decision_count==1u);
    assert_remote_metadata();frozen_trace();export_trace();
}
static void capacity_trace(void)
{
    setup(0);armed_trace(1);running_ring();
    for(uint32_t i=0;i<200u && trace_status().state!=VDC_PRIORITY_TRACE_FROZEN;++i) {
        event(100+i,(uint64_t)i*200000000u);tick();
    }
    assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN && trace_status().reason==VDC_PRIORITY_TRACE_FULL);
    assert(trace_status().record_count==76u);
    uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
    event(400,41000000000ull);tick();
    assert(!memcmp(saved,s_dpll_capture_records,sizeof(saved)));
    frozen_trace();export_trace();
}
static void old_capture_inert(void)
{
    uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
    const uint32_t count=s_dpll_capture_count;
    core=0;assert(!vdc_dpll_manager_dpll_capture_arm());
    assert(!vdc_dpll_manager_dpll_capture_stop());
    uint8_t page[28];uint32_t total=23,crc=29;memset(page,0xa5,sizeof(page));
    assert(!vdc_dpll_manager_dpll_capture_read(0,page,sizeof(page),&total,&crc));
    assert(total==23 && crc==29);for(unsigned i=0;i<sizeof(page);++i)assert(page[i]==0xa5);
    core=1;
    refmem_sync_vdc_boundary_command_t c={0};vdc_feedback_match_snapshot_t m={0};
    vdc_boundary_capture_offer_core1(&c,&m);vdc_boundary_capture_hold_core1(1,1,0,&m);
    ++s_vdc_domain.dpll.update_seq;vdc_dpll_manager_publish_runtime_snapshot_locked();
    assert(s_dpll_capture_count==count && !memcmp(saved,s_dpll_capture_records,sizeof(saved)));
}
static void lifecycle_trace(const char *name)
{
    setup(6000);stopped_ring();
    if(!strcmp(name,"legacy_completed_reuse")) {
        assert(vdc_dpll_manager_dpll_capture_arm());core=1;
        s_vdc_domain.dpll.update_seq=1;vdc_dpll_manager_publish_runtime_snapshot_locked();
        core=0;assert(vdc_dpll_manager_dpll_capture_stop());
        assert(s_dpll_capture_complete && s_dpll_capture_count==1u);
        uint8_t page[28];uint32_t total=23,crc=29,job=77;char path[96]="unchanged";
        assert(vdc_dpll_manager_dpll_capture_read(0,page,sizeof(page),&total,&crc));
        assert(!vdc_dpll_manager_dpll_capture_save(&job,path,sizeof(path)) && storage_calls==1u);
        armed_trace(1);running_ring();event(100,0);tick();frozen_trace();
        assert(vdc_dpll_manager_priority_trace_release());trace_service();
        storage_calls=0;total=23;crc=29;memset(page,0xa5,sizeof(page));
        assert(!vdc_dpll_manager_dpll_capture_read(0,page,sizeof(page),&total,&crc));
        assert(total==23 && crc==29);for(unsigned i=0;i<sizeof(page);++i)assert(page[i]==0xa5);
        assert(!vdc_dpll_manager_dpll_capture_save(&job,path,sizeof(path)) && storage_calls==0u);
        assert(job==77 && !strcmp(path,"unchanged"));
        assert(vdc_dpll_manager_dpll_capture_arm());assert(!s_dpll_capture_count);return;
    }
    if(!strcmp(name,"legacy_active")) {
        assert(vdc_dpll_manager_dpll_capture_arm());core=1;
        s_vdc_domain.dpll.update_seq=1;vdc_dpll_manager_publish_runtime_snapshot_locked();
        assert(s_dpll_capture_count==1);
        uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
        core=0;assert(vdc_dpll_manager_priority_trace_arm(1));trace_service();
        assert(trace_status().state==VDC_PRIORITY_TRACE_REJECTED);
        assert(trace_status().reason==VDC_PRIORITY_TRACE_LEGACY_BUSY);
        assert(s_dpll_capture_count==1 && s_dpll_capture_armed && !memcmp(saved,s_dpll_capture_records,sizeof(saved)));
        assert(vdc_dpll_manager_dpll_capture_stop());return;
    }
    if(!strcmp(name,"request_ack") || !strcmp(name,"cancel_before_ack")) {
        memset(s_dpll_capture_records,0xa5,sizeof(s_dpll_capture_records));
        uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
        assert(vdc_dpll_manager_priority_trace_arm(1));
        assert(!memcmp(saved,s_dpll_capture_records,sizeof(saved)));
        assert(trace_status().request_seq!=trace_status().ack_seq);
        assert(!vdc_dpll_manager_priority_trace_arm(2));
        assert(!vdc_dpll_manager_priority_trace_release());reject_read(1,0,28);
        if(!strcmp(name,"cancel_before_ack"))++s_model_feedback_session;
        trace_service();assert(trace_status().request_seq==trace_status().ack_seq);
        if(!strcmp(name,"cancel_before_ack")) {
            assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN &&
                trace_status().reason==VDC_PRIORITY_TRACE_SESSION && !trace_status().record_count);
            assert(!memcmp(saved,s_dpll_capture_records,sizeof(saved)));
            --s_model_feedback_session;running_ring();event(100,0);tick();
            assert(!trace_status().record_count && !memcmp(saved,s_dpll_capture_records,sizeof(saved)));
        } else assert(trace_status().state==VDC_PRIORITY_TRACE_ARMED && !trace_status().record_count);
        return;
    }
    armed_trace(1);
    if(!strcmp(name,"status_busy")) {
        vdc_priority_trace_status_t out,saved;memset(&out,0xa5,sizeof(out));saved=out;
        ++s_priority_trace_status_guard;
        assert(!vdc_dpll_manager_get_priority_trace(&out) && !memcmp(&out,&saved,sizeof(out)));
        --s_priority_trace_status_guard;++s_priority_trace_request_guard;
        assert(!vdc_dpll_manager_get_priority_trace(&out) && !memcmp(&out,&saved,sizeof(out)));
        --s_priority_trace_request_guard;assert(!vdc_dpll_manager_get_priority_trace(NULL));return;
    }
    if(!strcmp(name,"legacy_exclusive"))old_capture_inert();
    running_ring();event(100,0);tick();
    if(!strcmp(name,"duplicate_busy")) {
        const uint32_t count=trace_status().record_count;
        for(unsigned i=0;i<5u;++i)tick();
        assert(trace_status().record_count==count);
        event(101,200000000u);exact_result=TDMA_EVENT_EXACT_PENDING;tick();
        assert(trace_status().record_count==count);
        exact_result=TDMA_EVENT_EXACT_BUSY;tick();assert(trace_status().record_count==count);
        exact_result=TDMA_EVENT_EXACT_OK;tick();assert(trace_status().match_count==2u);
        return;
    }
    if(!strcmp(name,"cancel_after_ack")) {
        ++s_model_feedback_session;trace_service();
        assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN && trace_status().reason==VDC_PRIORITY_TRACE_SESSION);
        const uint32_t count=trace_status().record_count;--s_model_feedback_session;
        event(200,1500000000u);tick();assert(trace_status().record_count==count);return;
    }
    frozen_trace();
    if(!strcmp(name,"legacy_exclusive")) {old_capture_inert();return;}
    if(!strcmp(name,"read_pending")) {
        assert(vdc_dpll_manager_priority_trace_release());reject_read(1,0,28);return;
    }
    if(!strcmp(name,"release_ack") || !strcmp(name,"generation")) {
        assert(vdc_dpll_manager_priority_trace_release());
        assert(trace_status().request_seq!=trace_status().ack_seq);
        assert(!vdc_dpll_manager_dpll_capture_arm());reject_read(1,0,28);
        trace_service();assert(trace_status().request_seq==trace_status().ack_seq);
        assert(vdc_dpll_manager_dpll_capture_arm());assert(vdc_dpll_manager_dpll_capture_stop());
        if(!strcmp(name,"generation")) {
            assert(!vdc_dpll_manager_priority_trace_arm(1));armed_trace(2);
            reject_read(1,0,28);assert(!trace_status().record_count);
            running_ring();event(200,100000000u);tick();frozen_trace();
            assert(trace_status().record_count==1 && trace_status().capture_id==2);reject_read(1,0,28);
        }
        return;
    }
    assert(0);
}
static unsigned ack_observations,ack_complete;
static void observe_ack_store(const void *address)
{
    if(address!=(const void *)&s_priority_trace_status_guard &&
       address!=(const void *)&s_priority_trace_ack &&
       address!=(const void *)&s_dpll_capture_pool_owner)return;
    void (*saved_hook)(const void *)=atomic_store_hook;atomic_store_hook=NULL;
    const unsigned saved_core=core;core=0;
    vdc_priority_trace_status_t out,sentinel;memset(&out,0xa5,sizeof(out));sentinel=out;
    const bool readable=vdc_dpll_manager_get_priority_trace(&out);
    const uint32_t owner=__atomic_load_n(&s_dpll_capture_pool_owner,__ATOMIC_ACQUIRE);
    const uint32_t ack=__atomic_load_n(&s_priority_trace_ack,__ATOMIC_ACQUIRE);
    const uint32_t request=__atomic_load_n(&s_priority_trace_request.sequence,__ATOMIC_ACQUIRE);
    ++ack_observations;
    if(readable && out.request_seq==out.ack_seq) {
        assert(out.ack_seq==ack && ack==request);
        assert(out.state==VDC_PRIORITY_TRACE_IDLE && owner==DPLL_CAPTURE_POOL_LEGACY);
        ++ack_complete;
    } else {
        if(!readable)assert(!memcmp(&out,&sentinel,sizeof(out)));
        assert(!vdc_dpll_manager_priority_trace_stop());
        assert(!vdc_dpll_manager_priority_trace_release());
        assert(!vdc_dpll_manager_dpll_capture_arm());
        assert(s_priority_trace_request.sequence==request);
    }
    core=saved_core;atomic_store_hook=saved_hook;
}
static void ack_publication_trace(void)
{
    setup(6000);armed_trace(1);running_ring();event(100,0);tick();frozen_trace();
    assert(vdc_dpll_manager_priority_trace_release());
    atomic_store_hook=observe_ack_store;trace_service();atomic_store_hook=NULL;
    assert(ack_observations>=3u && ack_complete==1u);
    assert(trace_status().state==VDC_PRIORITY_TRACE_IDLE && trace_status().request_seq==trace_status().ack_seq);
    assert(vdc_dpll_manager_dpll_capture_arm());assert(vdc_dpll_manager_dpll_capture_stop());
}
static unsigned race_kind;
static bool rejected_reuse;
static void read_race(unsigned ordinal)
{
    if(ordinal!=2u)return;
    if(race_kind==1u)++ring.config_seq;
    else if(race_kind==2u)rejected_reuse=!vdc_dpll_manager_priority_trace_release();
    else if(race_kind==3u)ring.enabled=1u;
    else if(race_kind==4u){++s_model_feedback_session;trace_service();}
    else assert(0);
}
static void reader_trace(const char *name)
{
    setup(6000);armed_trace(1);running_ring();event(100,0);tick();frozen_trace();
    if(!strcmp(name,"read_bad_range")) {
        reject_read(1,0,0);reject_read(1,0,129);reject_read(1,UINT32_MAX,1);
        reject_read(1,168+100,1);reject_read(2,0,1);
        uint8_t byte;uint32_t total,crc;
        assert(vdc_dpll_manager_priority_trace_read(1,0,&byte,1,&total,&crc));return;
    }
    if(!strcmp(name,"read_busy")) {
        ring_available=false;reject_read(1,0,28);ring_available=true;
        ring.config_seq++;reject_read(1,0,28);ring.config_seq--;
        uint8_t byte;uint32_t total,crc;
        assert(vdc_dpll_manager_priority_trace_read(1,0,&byte,1,&total,&crc));return;
    }
    race_kind=!strcmp(name,"read_changed_ring")?1u:!strcmp(name,"read_release_race")?2u:
        !strcmp(name,"read_core1_race")?4u:3u;
    ring_reads=0;ring_read_hook=read_race;
    if(race_kind==4u) {
        const vdc_priority_trace_status_t before=trace_status();
        uint8_t data[128];uint32_t total,crc;
        assert(vdc_dpll_manager_priority_trace_read(1,0,data,sizeof(data),&total,&crc));
        const vdc_priority_trace_status_t after=trace_status();
        assert(!memcmp(&before,&after,sizeof(before)));return;
    }
    if(race_kind==2u) {
        uint8_t data[128];uint32_t total,crc;
        assert(vdc_dpll_manager_priority_trace_read(1,0,data,sizeof(data),&total,&crc));
        assert(rejected_reuse && trace_status().state==VDC_PRIORITY_TRACE_FROZEN);
        ring_read_hook=NULL;
        assert(vdc_dpll_manager_priority_trace_release());trace_service();
        assert(trace_status().state==VDC_PRIORITY_TRACE_IDLE);
    } else reject_read(1,0,28);
}
int main(int argc,char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    (void)match_publish;(void)publication_count;(void)captured_offers;(void)captured_applies;(void)captured_acks;
    (void)status;(void)model;
    assert(argc==2);
    if(!strcmp(argv[1],"sizes"))printf("%zu %zu %zu\n",sizeof(vdc_priority_trace_work_t),
        sizeof(vdc_priority_trace_status_t),sizeof(vdc_priority_trace_request_t));
    else if(!strncmp(argv[1],"flow_",5))flow_trace(argv[1]);
    else if(!strcmp(argv[1],"uptime_wrap")) {
        setup(0);armed_trace(1);running_ring();
        const uint64_t start=((UINT64_C(1)<<32)-50u)*1000000u-3000000000u;
        event(100,start);tick();event(101,start+100000000u);tick();event(102,start+200000000u);tick();
        frozen_trace();export_trace();
    }
    else if(!strcmp(argv[1],"empty")){setup(6000);armed_trace(1);frozen_trace();export_trace();}
    else if(!strcmp(argv[1],"capacity"))capacity_trace();
    else if(!strcmp(argv[1],"ack_publication"))ack_publication_trace();
    else if(!strncmp(argv[1],"read_",5) && strcmp(argv[1],"read_pending"))reader_trace(argv[1]);
    else lifecycle_trace(argv[1]);
    return 0;
}
'''
