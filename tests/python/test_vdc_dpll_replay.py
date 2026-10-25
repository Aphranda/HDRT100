from __future__ import annotations

import hashlib
import importlib.util
import sys
import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "domains" / "vdc" / "dpll_replay"
LEGACY_ENTRYPOINT = ROOT / "tools" / "vdc_dpll_replay" / "vdc_dpll_replay.py"
# The tools migration keeps the runner's byte-identical compatibility copy under
# tools/vdc/vdc_dpll_replay/ (registry role "resource"); the deepest legacy
# directory only carries the Python wrapper and README.
LEGACY_RUNNER = ROOT / "tools" / "vdc" / "vdc_dpll_replay" / "vdc_dpll_replay_runner.c"
ARCHIVE_DIR = (ROOT / "tools" / "archive" / "vdc" / "dpll_replay" /
               "pre_domain_migration")
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.domains.vdc.dpll_replay.vdc_dpll_replay import (  # noqa: E402
    DIAGNOSTIC_ONLY,
    SCHEMA,
    demo_trace,
    parse_int_list,
    validate_trace,
)
from tools.domains.vdc.dpll_replay import vdc_dpll_replay as replay_module  # noqa: E402


def test_classified_source_and_compatibility_entrypoint_are_bound() -> None:
    archived = ARCHIVE_DIR / "vdc_dpll_replay.py"
    assert hashlib.sha256(archived.read_bytes()).hexdigest() == (
        "0e4a340dab10b52fe8ae5148d6ff18c452c00f05777c08e17f1eaf8f678e29ba")
    runner = (ARCHIVE_DIR / "vdc_dpll_replay_runner.c").read_bytes()
    assert hashlib.sha256(runner).hexdigest() == (
        "8640d710f6dae809bd51c47f25aa85356781f785078dfa40ed6bed9eca168516")
    assert runner == LEGACY_RUNNER.read_bytes()
    canonical_runner = (TOOL_DIR / "vdc_dpll_replay_runner.c").read_bytes()
    normalize = lambda value: b"\n".join(  # noqa: E731
        line.rstrip() for line in value.replace(b"\r\n", b"\n").split(b"\n"))
    assert normalize(runner) == normalize(canonical_runner)

    spec = importlib.util.spec_from_file_location(
        "legacy_vdc_dpll_replay_entrypoint", LEGACY_ENTRYPOINT)
    assert spec is not None and spec.loader is not None
    legacy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(legacy)
    public_api = (
        "SCHEMA", "RESULT_SCHEMA", "DIAGNOSTIC_ONLY", "DPLL_ELIGIBLE", "LOCKED",
        "integer", "validate_trace", "demo_trace", "build_runner", "run_core",
        "write_csv", "svg_polyline", "write_svg", "parse_int_list", "run_score",
        "parse_args", "main",
    )
    for name in public_api:
        assert getattr(legacy, name) is getattr(replay_module, name)


def test_classified_and_compatibility_cli_failures_match(tmp_path: Path) -> None:
    invalid = tmp_path / "invalid.json"
    invalid.write_text('{"schema":"INVALID"}\n', encoding="utf-8")
    for trace in (tmp_path / "missing.json", invalid):
        results = []
        for entrypoint in (TOOL_DIR / "vdc_dpll_replay.py", LEGACY_ENTRYPOINT):
            results.append(subprocess.run(
                [sys.executable, str(entrypoint), "--trace", str(trace)],
                cwd=ROOT, text=True, capture_output=True, timeout=20))
        assert [result.returncode for result in results] == [2, 2]
        assert results[0].stdout == results[1].stdout == ""
        assert results[0].stderr == results[1].stderr
        assert results[0].stderr.startswith("ERROR: ")


def test_classified_and_compatibility_cli_replay_match(tmp_path: Path) -> None:
    summaries = []
    build_dir = tmp_path / "build"
    for name, entrypoint in (("classified", TOOL_DIR / "vdc_dpll_replay.py"),
                             ("compatibility", LEGACY_ENTRYPOINT)):
        command = [
            sys.executable, str(entrypoint), "--demo", "settle", "--samples", "16",
            "--node-count", "4", "--build-dir", str(build_dir),
            "--out-dir", str(tmp_path / name),
        ]
        completed = subprocess.run(
            command, cwd=ROOT, text=True, capture_output=True, timeout=60)
        assert completed.returncode == 0, completed.stdout + completed.stderr
        summaries.append(json.loads(completed.stdout))
    keys = (
        "schema", "passed", "diagnostic_only", "reusable_as_active_calibration",
        "reusable_as_firmware_lock_evidence", "production_c_core", "trace_source",
        "node_count", "sample_count", "accepted_count", "rejected_count",
        "final_gate", "final_lock_state", "final_locked_in_replay",
        "final_quality_tier", "final_residual_ns", "final_phase_offset_ns",
        "final_period_adjust_ppb", "rms_offset_ns", "max_abs_offset_ns",
    )
    assert ({key: summaries[0][key] for key in keys} ==
            {key: summaries[1][key] for key in keys})
    assert summaries[0]["passed"] is True
    assert summaries[0]["accepted_count"] == 16


def test_demo_trace_is_diagnostic_and_supports_eight_nodes() -> None:
    trace = validate_trace(demo_trace("settle", 16, 8))
    assert trace["node_count"] == 8
    assert len(trace["samples"]) == 16
    assert {row["source_node"] for row in trace["samples"]} == set(range(8))
    assert all(row["timestamp_flags"] == DIAGNOSTIC_ONLY
               for row in trace["samples"])


def test_trace_requires_monotonic_sequence() -> None:
    trace = demo_trace("settle", 8, 4)
    trace["samples"][1]["sample_seq"] = 1
    with pytest.raises(ValueError, match="strictly increasing"):
        validate_trace(trace)


def test_trace_rejects_more_than_eight_nodes() -> None:
    trace = demo_trace("settle", 9, 9)
    assert trace["schema"] == SCHEMA
    with pytest.raises(ValueError, match="2..8"):
        validate_trace(trace)


def test_parameter_scan_values_are_unique_and_support_hex() -> None:
    assert parse_int_list("32768, 0x10000,32768") == [32768, 65536]
    with pytest.raises(ValueError, match="empty"):
        parse_int_list("32768,,65536")


def build_sample_skip_executable(directory: Path, domain_source: Path | None = None) -> Path:
    """Run production Domain APIs, reusing the existing C sample fixtures."""
    harness = directory / "sample_skip.c"
    harness.write_text(r'''
#include <assert.h>
#include <stdlib.h>
#define main existing_vdc_domain_tests
#include "tests/unit/test_vdc_domain.c"
#undef main

static void fixture(vdc_domain_context_t *context, unsigned count)
{
    assert(vdc_domain_init(context));
    assert(install_test_path_delay(context));
    vdc_domain_set_ready(context, true);
    vdc_servo_profile_t servo = context->servo;
    servo.kp_q16 = 0;
    servo.ki_q16 = 65536;
    assert(vdc_domain_apply_debug_servo_profile(context, &servo));
    for (unsigned i = 1; i <= count; ++i) {
        vdc_tdma_timestamp_evidence_t sample = make_hardware_sample(&context->schedule, i, 10);
        assert(vdc_domain_submit_tdma_evidence(context, &sample));
    }
    if (count >= 4) {
        assert(context->dpll.state == VDC_DOMAIN_LOCK_LOCKED);
        assert(context->dpll.loop_filter_integrator_ppb != 0);
    }
}

static void preserved(const vdc_domain_context_t *context, const vdc_domain_context_t *before)
{
    assert(memcmp(&context->clock, &before->clock, sizeof(context->clock)) == 0);
    assert(memcmp(&context->dco, &before->dco, sizeof(context->dco)) == 0);
    assert(memcmp(&context->control, &before->control, sizeof(context->control)) == 0);
    assert(memcmp(&context->oscillator_discipline, &before->oscillator_discipline,
                  sizeof(context->oscillator_discipline)) == 0);
    assert(context->dpll.state == before->dpll.state);
    assert(context->dpll.accepted_sample_count == before->dpll.accepted_sample_count);
    assert(context->dpll.loop_filter_integrator_ppb == before->dpll.loop_filter_integrator_ppb);
    assert(context->dpll.last_frequency_error_ppb == before->dpll.last_frequency_error_ppb);
    assert(context->dpll.last_raw_phase_error_ns == before->dpll.last_raw_phase_error_ns);
    assert(context->dpll.last_expected_window_start_ns == before->dpll.last_expected_window_start_ns);
    assert(context->dpll.last_observed_time_ns == before->dpll.last_observed_time_ns);
    assert(context->dpll.last_sample_seq == before->dpll.last_sample_seq);
    assert(context->quality.accepted_sample_count == before->quality.accepted_sample_count);
    assert(context->quality.consecutive_good_samples == before->quality.consecutive_good_samples);
    assert(context->quality.consecutive_fine_samples == before->quality.consecutive_fine_samples);
    assert(context->quality.consecutive_debug_samples == before->quality.consecutive_debug_samples);
    assert(context->quality.consecutive_coarse_samples == before->quality.consecutive_coarse_samples);
    assert(context->quality.last_sample_time_ns == before->quality.last_sample_time_ns);
    assert(context->quality.last_sample_seq == before->quality.last_sample_seq);
    assert(context->quality.last_timestamp_source == before->quality.last_timestamp_source);
    assert(context->quality.last_timestamp_flags == before->quality.last_timestamp_flags);
    assert(context->quality.last_timestamp_resolution_ns == before->quality.last_timestamp_resolution_ns);
}

static void reject_case(unsigned code, unsigned count, bool debug, bool compact_path)
{
    vdc_domain_context_t context;
    fixture(&context, count);
    context.dpll.debug_continue_enabled = debug;
    const vdc_domain_context_t before = context;
    vdc_tdma_timestamp_evidence_t sample = make_hardware_sample(&context.schedule, count + 1, 10);
    bool result;
    if (compact_path) {
        vdc_compact_observation_sample_t compact = {0};
        compact.valid = 1;
        compact.sample_seq = count + 1;
        compact.event_id = UINT32_MAX; /* Dictionary miss -> real BAD_FRAME. */
        result = vdc_domain_submit_compact_observation(&context, &compact);
    } else {
        switch (code) {
        case VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH: sample.schedule_crc32 ^= 1; break;
        case VDC_DOMAIN_GATE_EPOCH_MISMATCH: ++sample.schedule_epoch; break;
        case VDC_DOMAIN_GATE_REFERENCE_MISMATCH: ++sample.reference_slot_id; break;
        case VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE: sample.source_slot_id = VDC_DOMAIN_NODE_COUNT; break;
        case VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE: sample.payload_class = VDC_DOMAIN_PAYLOAD_REFMEM_DELTA; break;
        case VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE: sample.timestamp_flags = 0; break;
        case VDC_DOMAIN_GATE_TIMESTAMP_RESOLUTION: sample.timestamp_resolution_ns = 10000; break;
        case VDC_DOMAIN_GATE_WINDOW_BOUND: sample.observed_time_ns += 4ull * context.schedule.period_ns; break;
        case VDC_DOMAIN_GATE_DELAY_GENERATION: sample.delay_generation = 0; break;
        case VDC_DOMAIN_GATE_BIAS_GENERATION: ++sample.bias_generation; break;
        case VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED:
            sample.correlation_flags = TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
                TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME |
                TDMA_RING_CLOCK_OBSERVATION_FLAG_LOCAL_PHASE_RAW;
            break;
        default: assert(!"unhandled gate fixture");
        }
        result = vdc_domain_submit_tdma_evidence(&context, &sample);
    }
    preserved(&context, &before);
    /* Debug continuation has never meant a servo acceptance. */
    bool continued = debug && (code == 8 || code == 9 || code == 10 || code == 11 || code == 12);
    assert(result == continued);
    if (continued) {
        assert(context.dpll.debug_continue_count == before.dpll.debug_continue_count + 1);
        assert(context.dpll.last_debug_gate_code == code);
        assert(context.dpll.rejected_sample_count == before.dpll.rejected_sample_count);
    } else if (code != VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH) {
        assert(context.gate.reject_code == code && !context.gate.passed);
        assert(context.dpll.rejected_sample_count == before.dpll.rejected_sample_count + 1);
        assert(context.quality.consecutive_bad_samples == before.quality.consecutive_bad_samples + 1);
        assert(context.quality.health_state != VDC_DOMAIN_HEALTH_HEALTHY);
    }
    if (count) {
        vdc_domain_service(&context, before.quality.last_sample_time_ns + 9000);
        assert(context.quality.last_sample_age_us == 9);
        preserved(&context, &before);
    }
    /* Admission may resume on the next valid current frame, including warmup. */
    sample = make_hardware_sample(&context.schedule, count + 2, 10);
    assert(vdc_domain_submit_tdma_evidence(&context, &sample));
    assert(context.dpll.accepted_sample_count == before.dpll.accepted_sample_count + 1);
    assert(context.clock.model_seq > before.clock.model_seq);
    if (count >= 4) assert(context.dpll.state == VDC_DOMAIN_LOCK_LOCKED);
}

static void reset_case(unsigned kind)
{
    vdc_domain_context_t context;
    fixture(&context, 4);
    if (kind == 1) {
        vdc_domain_set_ready(&context, false);
        assert(context.dpll.state == VDC_DOMAIN_LOCK_OFF);
        assert(context.dco.lock_state == VDC_DOMAIN_LOCK_OFF);
        return;
    }
    if (kind == 7 || kind == 8) {
        vdc_compact_observation_sample_t compact = {0};
        const int32_t retained_integrator = context.dpll.loop_filter_integrator_ppb;
        if (kind == 7) {
            compact.valid = 1;
            context.schedule.period_ns = 0;
        }
        assert(!vdc_domain_submit_compact_observation(&context, &compact));
        assert(context.gate.reject_code == (kind == 7
            ? VDC_DOMAIN_GATE_BAD_SCHEDULE : VDC_DOMAIN_GATE_BAD_ARGUMENT));
        assert(context.dpll.state == VDC_DOMAIN_LOCK_CHECKING);
        /* Compact configuration failures already retained the integrator. */
        assert(context.dpll.loop_filter_integrator_ppb == retained_integrator);
        return;
    }
    if (kind == 2) {
        vdc_dpll_control_profile_t role = context.control.profile;
        role.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
        role.follow_master_slot_id = 1;
        assert(vdc_domain_set_dpll_control_profile(&context, &role));
    } else if (kind == 3) {
        vdc_servo_profile_t servo = context.servo;
        ++servo.ki_q16;
        assert(vdc_domain_apply_debug_servo_profile(&context, &servo));
    } else if (kind == 4) {
        vdc_tdma_timestamp_evidence_t sample = make_hardware_sample(&context.schedule, 5, 10);
        context.schedule.enabled = 0;
        assert(!vdc_domain_submit_tdma_evidence(&context, &sample));
        assert(context.gate.reject_code == VDC_DOMAIN_GATE_DISABLED);
    } else if (kind == 5) {
        vdc_tdma_timestamp_evidence_t sample = make_hardware_sample(&context.schedule, 5, 10);
        context.schedule.period_ns = 0;
        assert(!vdc_domain_submit_tdma_evidence(&context, &sample));
        assert(context.gate.reject_code == VDC_DOMAIN_GATE_BAD_SCHEDULE);
    } else {
        assert(kind == 6);
        context.path_delay.entry_count = context.schedule.ring_binding.node_count;
        for (unsigned i = 0; i < context.path_delay.entry_count; ++i) {
            context.path_delay.entries[i].reference_slot_id =
                (i + 1) % context.path_delay.entry_count;
        }
        for (unsigned i = context.path_delay.entry_count;
             i < VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT; ++i) {
            context.path_delay.entries[i].valid = 0;
        }
        assert(vdc_domain_load_observation_path_matrix(&context.path_delay,
            context.schedule.ring_binding.node_count));
        context.path_delay.table_crc32 = vdc_domain_path_delay_table_crc32(&context.path_delay);
        assert(vdc_domain_activate_tdma_configuration(&context, &context.schedule,
            &context.timestamp_dictionary, &context.path_delay));
    }
    assert(context.dpll.loop_filter_integrator_ppb == 0);
    assert(context.dpll.accepted_sample_count == 0);
    assert(context.dpll.state == VDC_DOMAIN_LOCK_CHECKING);
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    unsigned code = (unsigned)strtoul(argv[1], NULL, 10);
    if (code == 100) return existing_vdc_domain_tests();
    if (code == 101) reset_case((unsigned)strtoul(argv[2], NULL, 10));
    else reject_case(code, (unsigned)strtoul(argv[2], NULL, 10),
                     atoi(argv[3]) != 0, atoi(argv[4]) != 0);
    puts("sample skip scenario passed");
    return 0;
}
''', encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "Host C compiler required"
    sources = [domain_source or ROOT / "components/vdc_domain/src/vdc_domain.c"]
    sources += [ROOT / f"components/vdc_domain/src/{name}.c" for name in (
        "vdc_domain_evidence", "vdc_domain_profile",
        "vdc_timestamp", "vdc_ring_observer", "vdc_sync_io_adapter", "vdc_tdma_payload")]
    sources += [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_service", "tdma_service_core1", "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map", "tdma_ring_runtime",
        "tdma_traffic_scheduler", "tdma_service_timing")]
    executable = directory / ("sample_skip.exe" if os.name == "nt" else "sample_skip")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{ROOT}",
               f"-I{ROOT / 'components/vdc_domain/inc'}", f"-I{ROOT / 'components/tdma/inc'}",
               str(harness), *map(str, sources), "-o", str(executable)]
    (directory / "compile-command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (directory / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.fixture(scope="module")
def sample_skip_executable(tmp_path_factory):
    return build_sample_skip_executable(tmp_path_factory.mktemp("vdc-sample-skip"))


def run_sample_skip_case(executable, *args):
    command = [str(executable), *map(str, args)]
    name = "case-" + "-".join(map(str, args))
    (executable.parent / f"{name}.command.json").write_text(json.dumps(command), encoding="utf-8")
    result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    (executable.parent / f"{name}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (executable.parent / f"{name}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("code", [4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 17])
@pytest.mark.parametrize("count", [0, 2, 4])
@pytest.mark.parametrize("debug", [0, 1])
def test_invalid_sample_preserves_servo_and_valid_anchors(sample_skip_executable, code, count, debug):
    run_sample_skip_case(sample_skip_executable, code, count, debug, int(code == 12))


@pytest.mark.parametrize("kind", [1, 2, 3, 4, 5, 6, 7, 8])
def test_explicit_stop_and_context_changes_keep_existing_reset(sample_skip_executable, kind):
    run_sample_skip_case(sample_skip_executable, 101, kind, 0, 0)


def test_existing_domain_regressions(sample_skip_executable):
    run_sample_skip_case(sample_skip_executable, 100, 0, 0, 0)
