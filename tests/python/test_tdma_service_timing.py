"""Run the production Core1 timing recorder with a deterministic clk_sys clock."""
import os
from pathlib import Path
import shutil
import subprocess
import pytest

from tools.tdma_ring_monitor.tdma_service_timing import parse_service_timing, FIELDS_V6


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
    "2,250000000", "9,250000000,0,1,15,0,1,20,100,0",
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


@pytest.mark.parametrize('version,count', [(6, 31), (7, 44), (8, 49)])
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
