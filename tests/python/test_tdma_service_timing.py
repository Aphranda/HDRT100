"""Run the production Core1 timing recorder with a deterministic clk_sys clock."""
import os
from pathlib import Path
import shutil
import subprocess
import pytest

from tools.tdma_ring_monitor.tdma_service_timing import parse_service_timing, FIELDS_V6
from tools.tdma_ring_monitor.tdma_service_timing import parse_rx_timing
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body


ROOT = Path(__file__).resolve().parents[2]


def test_phase_attribution_preserves_one_complete_worst_case(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    exe = tmp_path / ("timing.exe" if os.name == "nt" else "timing")
    subprocess.run([
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DTDMA_SERVICE_TIMING_ENABLED=1",
        "-I" + str(ROOT / "components/tdma/inc"),
        "-I" + str(ROOT / "components/vdc_domain/inc"),
        str(ROOT / "tests/unit/test_tdma_service_timing.c"), "-o", str(exe),
    ], check=True, timeout=60)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("version,count", [(1, 11), (2, 15), (3, 19), (4, 25), (5, 31)])
def test_profile_wire_versions_and_inclusive_intervals(version, count):
    fields = [version, 250000000, 2, 9, count, 1, 8, 2**40, 1000, 0]
    fields += [value for i in range(count) for value in (100 + i, i + 1)]
    result = parse_service_timing(",".join(map(str, fields)))
    assert result["start_ticks"] == 2**40 and result["total_ticks"] == 1000
    assert result["stages"]["rx_capture"] == {"ticks": 107, "calls": 8}
    if version >= 2:
        assert result["stages"]["rx_latch"] == {"ticks": 114, "calls": 15}
    else:
        assert "rx_latch" not in result["stages"]
    if version >= 3:
        assert result["stages"]["rx_dma_observe"] == {"ticks": 115, "calls": 16}
        assert result["stages"]["rx_ring_copy"] == {"ticks": 118, "calls": 19}
    else:
        assert "rx_dma_observe" not in result["stages"]
    if version >= 4:
        assert result["stages"]["ring_runtime"] == {"ticks": 119, "calls": 20}
        assert result["stages"]["adapter_status"] == {"ticks": 124, "calls": 25}
    else:
        assert "ring_runtime" not in result["stages"]
    if version >= 5:
        assert result["stages"]["rx_inspect"] == {"ticks": 125, "calls": 26}
        assert result["stages"]["rx_complete"] == {"ticks": 130, "calls": 31}
    else:
        assert "rx_inspect" not in result["stages"]


@pytest.mark.parametrize("raw", [
    "2,250000000", "12,250000000,0,1,15,0,1,20,100,0",
    "2,250000000,0,1,11,0,1,20,100,0" + ",0,0"*11,
    "1,250000000,0,1,11,0,1,20,100,0" + ",0,0"*15,
    "2,250000000,0,1,15,0,1,20,100,0" + ",0,0"*14,
    "3,250000000,0,1,15,0,1,20,100,0" + ",0,0"*15,
    "3,250000000,0,1,19,0,1,20,100,0" + ",0,0"*18,
    "4,250000000,0,1,19,0,1,20,100,0" + ",0,0"*19,
    "4,250000000,0,1,25,0,1,20,100,0" + ",0,0"*24,
    "5,250000000,0,1,25,0,1,20,100,0" + ",0,0"*25,
    "5,250000000,0,1,31,0,1,20,100,0" + ",0,0"*30,
    "4,250000000,0,1,25,0,1,20,100,0" + ",0,0"*31,
])
def test_profile_rejects_unknown_truncated_or_mismatched_schema(raw):
    with pytest.raises(ValueError):
        parse_service_timing(raw)


def test_profile_unavailable():
    assert parse_service_timing('"UNAVAILABLE"') is None


@pytest.mark.parametrize('version,count', [(6, 31), (7, 44), (8, 49), (9, 51), (10, 54), (11, 64)])
def test_state_profile_schema_preserves_full_interval_and_generations(version, count):
    fields = [version, 250000000, 2, 90, count, 2, 42, 2**40, 1000, 0,
              1200, 2, 4, 6, 80, 2, 4, 6, 81, 60, 30]
    assert len(fields) == len(FIELDS_V6)
    fields += [n for i in range(count) for n in (100+i, i+1)]
    result = parse_service_timing(','.join(map(str, fields)))
    assert result['full_phase_ticks'] == 1200 and result['total_ticks'] == 1000
    assert result['entry_return_sequence'] == 80 and result['exit_return_sequence'] == 81
    assert result['autonomous_phase_count'] == 60 and result['other_phase_count'] == 30
    assert result['stages']['rx_complete'] == {'ticks': 130, 'calls': 31}
    if version >= 7:
        assert result['stages']['rx_request'] == {'ticks': 131, 'calls': 32}
        assert result['stages']['select_empty'] == {'ticks': 138, 'calls': 39}
        assert result['stages']['intent_bind'] == {'ticks': 143, 'calls': 44}
    if version >= 8:
        assert result['stages']['rx_dma_initial'] == {'ticks': 144, 'calls': 45}
        assert result['stages']['rx_dma_frame_recheck'] == {'ticks': 145, 'calls': 46}
        assert result['stages']['rx_dma_discovery_recheck'] == {'ticks': 146, 'calls': 47}
        assert result['stages']['rx_latch_read'] == {'ticks': 147, 'calls': 48}
        assert result['stages']['rx_latch_rearm'] == {'ticks': 148, 'calls': 49}
    if version >= 9:
        assert result['stages']['origin_observe'] == {'ticks': 149, 'calls': 50}
        assert result['stages']['origin_publish'] == {'ticks': 150, 'calls': 51}
    if version >= 10:
        assert result['stages']['origin_admit'] == {'ticks': 151, 'calls': 52}
        assert result['stages']['origin_calibration_crc'] == {'ticks': 152, 'calls': 53}
        assert result['stages']['origin_begin'] == {'ticks': 153, 'calls': 54}
    if version >= 11:
        assert result['stages']['event_entry'] == {'ticks': 154, 'calls': 55}
        assert result['stages']['event_publish'] == {'ticks': 161, 'calls': 62}
        assert result['stages']['reference_tx'] == {'ticks': 162, 'calls': 63}
        assert result['stages']['reference_submit'] == {'ticks': 163, 'calls': 64}
    with pytest.raises(ValueError):
        parse_service_timing(','.join(map(str, fields[:-1])))


@pytest.mark.parametrize('case', ['request', 'dispatch'])
def test_real_request_and_dispatch_branch_attribution(tmp_path, case):
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler, 'A host C compiler is required'
    names = ['tdma_profile', 'tdma_traffic_scheduler'] if case == 'dispatch' else [
        'tdma_pio_spi_ring_adapter', 'tdma_adapter_comm_fsm', 'tdma_flight_fifo',
        'tdma_flight_engine', 'tdma_flight_overlay', 'tdma_overlay_prepare', 'tdma_rx_prepare',
        'tdma_receive_health', 'tdma_process_image_map', 'tdma_ring_runtime',
        'tdma_transport_frame', 'tdma_profile']
    exe = tmp_path / (case + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-DTDMA_SERVICE_TIMING_ENABLED=1', '-I' + str(ROOT/'components/tdma/inc'),
        '-I' + str(ROOT/'components/vdc_domain/inc'),
        str(ROOT/f'tests/unit/test_tdma_{case}_timing.c'),
        *[str(ROOT/f'components/tdma/src/{name}.c') for name in names], '-o', str(exe)],
        check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=5)


def test_rx_profile_decodes_real_scpi_serialization(tmp_path):
    source = (ROOT/'middleware/scpi_port/src/scpi_system_snapshot_commands.c').read_text(encoding='utf-8')
    callback = c_definition_body(source, 'scpi_cmd_system_tdma_profile_rx_q')
    unit = tmp_path/'rx_wire.c'
    unit.write_text(r'''
#include <stdio.h>
#include <inttypes.h>
#include "tdma_service_timing.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
static bool available = true;
static tdma_rx_timing_snapshot_t model;
bool tdma_service_timing_rx_try_snapshot(tdma_rx_timing_snapshot_t *s) { *s=model; return available; }
static unsigned fields;
static void SCPI_ResultUInt64(scpi_t *c, uint64_t n) { (void)c; printf("%s%" PRIu64, fields++ ? "," : "", n); }
static void SCPI_ResultUInt32(scpi_t *c, uint32_t n) { SCPI_ResultUInt64(c,n); }
static void SCPI_ResultText(scpi_t *c, const char *s) { (void)c; printf("%s",s); }
static scpi_result_t result(scpi_t *context) {
''' + callback + r'''
}
int main(void) {
    model.version=TDMA_RX_TIMING_VERSION; model.clock_hz=250000000;
    model.reset_generation=17; model.phase_count=99;
    model.initial_observation_count=51; model.initial_gap_count=50;
    model.initial_gap_max_ticks=UINT64_C(1)<<40;
    model.initial_backlog_max_words=1090; model.clamp_skipped_words=UINT64_C(1)<<48;
    for (unsigned i=0; i<TDMA_RX_TIMING_STATION_STATES; ++i) {
        model.station_polls[i]=100+i;
        if (i) model.station_age_max_ns[i-1]=(UINT64_C(1)<<36)+i;
    }
    for (unsigned i=0; i<TDMA_RX_DROP_CAUSE_COUNT; ++i) model.drop_count[i]=200+i;
    result(NULL); puts(""); available=false; result(NULL); puts(""); return 0;
}
''', encoding='utf-8')
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path/('rx_wire.exe' if os.name == 'nt' else 'rx_wire')
    subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror',
        '-DTDMA_SERVICE_TIMING_ENABLED=1','-I'+str(ROOT/'components/tdma/inc'),
        str(unit),'-o',str(exe)],check=True,timeout=60)
    output = subprocess.check_output([str(exe)],text=True,timeout=3).splitlines()
    decoded = parse_rx_timing(output[0])
    assert parse_rx_timing(output[1]) is None
    assert decoded['reset_generation'] == 17 and decoded['phase_count'] == 99
    assert decoded['initial_gap_max_ticks'] == 2**40 and decoded['clamp_skipped_words'] == 2**48
    assert decoded['station']['idle'] == {'polls':100,'age_max_ns':0}
    assert decoded['station']['ready'] == {'polls':103,'age_max_ns':2**36+3}
    assert decoded['drops'] == dict(epoch=200,clamp=201,stale_hint=202,frame_copy=203,discovery_copy=204)
    fields = output[0].split(',')
    for bad in (fields[:-1],fields+['0'],['2']+fields[1:]):
        with pytest.raises(ValueError): parse_rx_timing(','.join(bad))
    for index, value in ((5,'4'),(6,'6'),(7,str(2**32)),(9,str(2**64)),(13,'-1'),(22,str(2**32))):
        bad = fields.copy(); bad[index] = value
        with pytest.raises(ValueError): parse_rx_timing(','.join(bad))
