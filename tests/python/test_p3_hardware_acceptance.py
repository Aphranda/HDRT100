import json
import sys
import types
from pathlib import Path

import pytest

from tools.hardware_acceptance.p3_hardware_acceptance import (
    AcceptanceError,
    LIMITED_RECEIPT_SCHEMA,
    RECEIPT_SCHEMA,
    TDMA_RECEIPT_SCHEMA,
    _validate_evidence,
    acceptance_timing,
    calibration_coded_probe_phase_cycles,
    calibration_probe_phase_cycles,
    is_acceptance_source,
    parse_schedule,
    p3_link_delays,
    reset_acceptance_boards,
    validate_ota,
    validate_online_builds,
    validate_p3,
    validate_runtime_schedules,
    validate_schedule_isolation,
    validate_sma_observer_topology,
    _record_timing_event,
    _start_timing_probe,
)


ROOT = Path(__file__).resolve().parents[2]


def test_code_scope_includes_runtime_tools_and_build_inputs() -> None:
    for path in (
        "application/src/app.c",
        "components/tdma/inc/tdma.h",
        "tools/example/check.py",
        "tests/python/test_example.py",
        ".githooks/pre-commit",
        "CMakeLists.txt",
        "config/hardware_acceptance/p3_bench.json",
    ):
        assert is_acceptance_source(path)
    for path in (
        "docs/calibration/CALIBRATION_DOMAIN_TODO.md",
        "out/build/image.bin",
        "config/hardware_acceptance/p3_acceptance_receipt.json",
    ):
        assert not is_acceptance_source(path)


def test_precommit_enforces_p3_receipt_after_doc_gates() -> None:
    hook = (ROOT / ".githooks" / "pre-commit").read_text(encoding="utf-8")
    assert "p3_hardware_acceptance.py check-staged" in hook
    assert hook.index("doc gates OK") < hook.index("p3_hardware_acceptance.py")


def test_schedule_parser_and_isolation_gate() -> None:
    raw = "2,250000000,250000,1,91,0,10,0," + ",".join("0" for _ in range(11))
    parsed = parse_schedule(raw)
    assert parsed["enabled_mask"] == 91
    validate_schedule_isolation({"n0": parsed}, {"n0": parsed}, 4)
    changed = dict(parsed, enabled_mask=95)
    with pytest.raises(AcceptanceError, match="load mask"):
        validate_schedule_isolation({"n0": parsed}, {"n0": changed}, 4)
    quarantined = dict(parsed, quarantined_mask=4)
    with pytest.raises(AcceptanceError, match="quarantined"):
        validate_schedule_isolation({"n0": parsed}, {"n0": quarantined}, 4)

    validate_runtime_schedules({"n0": parsed, "n1": parsed}, 4)
    missed = dict(parsed, schedule_miss_count=1)
    with pytest.raises(AcceptanceError, match="schedule miss"):
        validate_runtime_schedules({"n0": missed}, 4)


def test_ota_gate_requires_exact_five_board_set() -> None:
    ids = [f"node{index}" for index in range(5)]
    summary = {
        "passed": True, "dry_run": False, "failed_count": 0,
        "board_count": 5, "updated_count": 5, "expected_build": "build",
        "boards": [{"serial_number": value} for value in ids],
    }
    validate_ota(summary, ids, "build")
    summary["updated_count"] = 4
    with pytest.raises(AcceptanceError, match="OTA"):
        validate_ota(summary, ids, "build")


def test_online_build_gate_requires_exact_reused_firmware() -> None:
    ids = [f"node{index}" for index in range(5)]
    validate_online_builds({value: "build" for value in ids}, ids, "build")
    with pytest.raises(AcceptanceError, match="board set"):
        validate_online_builds({value: "build" for value in ids[:4]}, ids,
                               "build")
    mismatched = {value: "build" for value in ids}
    mismatched[ids[-1]] = "other-build"
    with pytest.raises(AcceptanceError, match="expected build"):
        validate_online_builds(mismatched, ids, "build")


def test_acceptance_initialization_resets_each_uid_once_and_reenumerates(
        monkeypatch: pytest.MonkeyPatch) -> None:
    ids = ["n0", "n1"]

    class Board:
        def __init__(self, board_id: str, port: str) -> None:
            self.address = board_id
            self.port = port
            self.build = "build"

    complete = {
        "n0": Board("n0", "COM5"),
        "n1": Board("n1", "COM4"),
    }
    discoveries = [complete, {"n0": complete["n0"]}, complete]
    commands: list[tuple[str, str]] = []
    closed: list[bool] = []
    fake = types.ModuleType("tdma_start_ring")
    fake.discover = lambda _args: discoveries.pop(0)
    fake.board_command = lambda board, command, _args: (
        commands.append((board.address, command)) or "OK")
    fake.close_persistent_connections = lambda: closed.append(True)
    monkeypatch.setitem(sys.modules, "tdma_start_ring", fake)
    monkeypatch.setattr(
        "tools.hardware_acceptance.p3_hardware_acceptance.time.sleep",
        lambda _seconds: None)

    result = reset_acceptance_boards(ids, "build", acceptance_timing({}))

    assert result["passed"] is True
    assert result["board_ids"] == ids
    assert result["ports_before"] == {"n0": "COM5", "n1": "COM4"}
    assert result["ports_after"] == {"n0": "COM5", "n1": "COM4"}
    assert commands == [
        ("n0", "SYSTem:BOOT:RESet"),
        ("n1", "SYSTem:BOOT:RESet"),
    ]
    assert closed == [True]


def _trial(delay: float = 80.0) -> dict:
    return {
        "passed": True,
        "delay_estimate_ns": delay,
        "initiator": {"dma_overrun_count": 0, "pio_stall_count": 0},
        "responder": {"dma_overrun_count": 0, "pio_stall_count": 0},
    }


def test_p3_gate_requires_complete_repeated_matrix() -> None:
    config = {
        "p3_board_ids_in_physical_order": ["n0", "n1", "n2", "n3"],
        "frequency_ladder_mhz": [10, 25, 30], "repeats": 3,
        "stable_frequency_mhz": 25,
        "minimum_link_delay_ns": 70, "maximum_link_delay_ns": 90,
    }
    summary = {
        "passed": True,
        "frequency_policy": {
            "stable_profiles_passed": True,
            "highest_stable_frequency_mhz": 25,
        },
        "trials": [_trial() for _ in range(72)],
    }
    result = validate_p3(summary, config)
    assert result["trial_count"] == 72
    summary["trials"].pop()
    with pytest.raises(AcceptanceError, match="matrix"):
        validate_p3(summary, config)


def test_receipt_uses_complete_calibration_to_dpll_schema() -> None:
    assert RECEIPT_SCHEMA == "HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_V4"
    assert TDMA_RECEIPT_SCHEMA.endswith("TDMA_4NODE_V2")
    assert LIMITED_RECEIPT_SCHEMA.endswith("10MHZ_LIMITED_V1")


def test_receipt_evidence_requires_every_acceptance_phase(tmp_path: Path) -> None:
    names = (
        "firmware_package", "ota_summary", "topology_summary",
        "coarse_calibration_summary", "coded_calibration_summary",
        "initialization_reset", "p3_summary", "trn00_summary",
        "trn01_summary", "trn02_summary",
        "trn03_matrix", "tdma_summary", "sma_observer_wiring",
        "dpll_summary",
    )
    record = {}
    for name in names:
        path = tmp_path / f"{name}.json"
        path.write_text("{}\n", encoding="utf-8")
        import hashlib
        record[name] = {
            "path": path.name,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    _validate_evidence(tmp_path, record)
    del record["trn02_summary"]
    with pytest.raises(AcceptanceError, match="trn02_summary"):
        _validate_evidence(tmp_path, record)


def test_tdma_only_receipt_does_not_require_dpll_observer(tmp_path: Path) -> None:
    names = (
        "firmware_package", "ota_summary", "topology_summary",
        "coarse_calibration_summary", "coded_calibration_summary",
        "initialization_reset", "p3_summary", "trn00_summary",
        "trn01_summary", "trn02_summary",
        "trn03_matrix", "tdma_summary",
    )
    record = {
        "schema": TDMA_RECEIPT_SCHEMA,
        "passed": True,
        "acceptance_scope": "FOUR_NODE_TDMA",
        "ota_board_ids": ["n0", "n1", "n2", "n3"],
        "tdma_board_ids": ["n0", "n1", "n2", "n3"],
    }
    import hashlib
    for name in names:
        path = tmp_path / f"{name}.json"
        path.write_text("{}\n", encoding="utf-8")
        record[name] = {
            "path": path.name,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    _validate_evidence(tmp_path, record, include_dpll=False)


def test_acceptance_timing_probe_writes_action_events(tmp_path: Path) -> None:
    path = tmp_path / "timing.json"
    _start_timing_probe(path)
    _record_timing_event({"action": "unit-test", "status": "PASS"})
    payload = json.loads(path.read_text(encoding="utf-8"))
    assert payload["schema"] == "HAOFV_HARDWARE_ACCEPTANCE_TIMING_PROBE_V1"
    assert payload["events"][-1]["action"] == "unit-test"


def test_p3_link_delay_reduction_uses_each_stable_clk_data_link() -> None:
    board_ids = ["n0", "n1", "n2", "n3"]
    config = {
        "p3_board_ids_in_physical_order": board_ids,
        "stable_frequency_mhz": 25,
        "repeats": 3,
    }
    trials = []
    for link, source in enumerate(board_ids):
        destination = board_ids[(link + 1) % len(board_ids)]
        for delay in (79.0 + link, 80.0 + link, 81.0 + link):
            trials.append({
                "source": source, "destination": destination,
                "signal_group": 0, "frequency_hz": 25_000_000,
                "delay_estimate_ns": delay, "passed": True,
            })
    delays = p3_link_delays({"trials": trials}, config)
    assert delays == [80, 82, 82, 84]
    assert all(value % 2 == 0 for value in delays)
    trials.pop()
    with pytest.raises(AcceptanceError, match="link3"):
        p3_link_delays({"trials": trials}, config)


def test_bench_and_orchestrator_cover_full_hardware_acceptance() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    for key in (
        "topology_anchor_board_id", "calibration_profile_levels",
        "calibration_probe_phase_cycles_by_level",
        "calibration_coded_probe_phase_cycles_by_level",
        "training_marker_codebook", "training_sck_codebook",
        "training_data_codebook",
        "training_marker_offsets_by_node", "training_sck_offsets_by_node",
        "training_data_offsets_by_node", "tdma_window_s",
        "dpll_observer_board_id", "sma_observer_routes",
        "hardware_acceptance_timing",
    ):
        assert key in bench
    source = (ROOT / "tools" / "hardware_acceptance" /
              "p3_hardware_acceptance.py").read_text(encoding="utf-8")
    for tool in (
        "calibration_ring_topology.py", "calibration_clk_train.py",
        "calibration_clk_coded.py", "calibration_link_p3.py",
        "calibration_marker_train.py", "calibration_sck_train.py",
        "calibration_data_train.py", "trn03_matrix.py",
        "trn03_closed_loop.py", "sma_cable_symmetric_rtt.py",
        "dpll_vdc_monitor.py",
    ):
        assert tool in source
    assert "--tdma-only" in source
    assert "FOUR_NODE_TDMA" in source
    assert '"--codebook", str(config["training_marker_codebook"])' in source
    assert '"--codebook", str(config["training_sck_codebook"])' in source
    assert '"--codebook", str(config["training_data_codebook"])' in source
    assert "Hardware acceptance: TRN-00 accepted MARK offset row" in source
    assert "Hardware acceptance: TRN-01 SCK offset matrix" in source
    assert '"trn00_summary": evidence_entry' in source
    assert '"trn01_summary": evidence_entry' in source
    assert '"sck_summary": evidence_entry' not in source


def test_sma_observer_topology_is_fixed_to_measured_bidirectional_routes() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    identities = {
        str(index): {"address": board_id}
        for index, board_id in enumerate(bench["ota_board_ids"], start=1)
    }
    summary = {
        "passed": True,
        "identities": identities,
        "wire_order": {
            "passed": True,
            "routes": bench["sma_observer_routes"],
        },
    }
    assert validate_sma_observer_topology(summary, bench) == \
        bench["sma_observer_routes"]

    summary["wire_order"]["routes"] = [
        dict(route) for route in bench["sma_observer_routes"]
    ]
    summary["wire_order"]["routes"][1]["validator_output_channel"] = 3
    with pytest.raises(AcceptanceError, match="differs from bench contract"):
        validate_sma_observer_topology(summary, bench)


def test_hardware_acceptance_timing_is_explicit_and_bounded() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    timing = acceptance_timing(bench)
    assert timing["serial_settle_s"] == 0.05
    assert timing["input_settle_s"] == timing["serial_settle_s"]
    assert timing["output_handoff_s"] == timing["phase_gap_s"]
    assert timing["serial_read_timeout_s"] < timing["serial_timeout_s"]
    assert timing["action_timeout_s"] <= timing["serial_timeout_s"]
    assert timing["board_reset_timeout_s"] == 15.0
    assert timing["board_reset_poll_interval_s"] == 0.5
    with pytest.raises(AcceptanceError, match="must be > 0"):
        acceptance_timing({"hardware_acceptance_timing": {
            "serial_timeout_s": 0,
        }})
    with pytest.raises(AcceptanceError, match="serial_read_timeout_s"):
        acceptance_timing({"hardware_acceptance_timing": {
            "serial_timeout_s": 0.1,
            "serial_read_timeout_s": 0.2,
        }})


def test_calibration_probe_phase_is_selected_per_profile() -> None:
    bench = json.loads((ROOT / "config" / "hardware_acceptance" /
                        "p3_bench.json").read_text(encoding="utf-8"))
    assert calibration_probe_phase_cycles(bench, 7) == 10
    assert calibration_probe_phase_cycles(bench, 8) == 2
    assert calibration_probe_phase_cycles(bench, 9) == 2
    assert calibration_coded_probe_phase_cycles(bench, 7) == [8, 9, 10]
    assert calibration_coded_probe_phase_cycles(bench, 8) == [1, 2, 3]
    with pytest.raises(AcceptanceError, match="profile level 10"):
        calibration_probe_phase_cycles(bench, 10)
    with pytest.raises(AcceptanceError, match="profile level 10"):
        calibration_coded_probe_phase_cycles(bench, 10)


def test_precalibration_tools_clear_stale_stage_and_gate_arm_result() -> None:
    for tool in ("calibration_clk_train.py", "calibration_clk_coded.py"):
        source = (ROOT / "tools" / "calibration_ring_validate" /
                  tool).read_text(encoding="utf-8")
        assert "CALibration:TRAINing:STAGe:CLEar" in source
        assert "SYSTem:TDMA:RING:ARM:STATus?" in source
        assert "ARM rejected with result=" in source
        assert "ARM_SUBMIT" in source
        assert "ARM_STARTED" in source
        assert "CALibration:TOPology:PROBe 1," in source
        assert "CALibration:TOPology:PROBe 0" in source
    coded = (ROOT / "tools" / "calibration_ring_validate" /
             "calibration_clk_coded.py").read_text(encoding="utf-8")
    assert "snapshot[\"state\"] == STATE_IDLE" in coded
    assert "deadline = time.monotonic() + args.coded_timeout" in coded
    assert "def wait_ring_stopped" in coded
    assert "STOP_APPLIED" in coded


def test_calibration_and_training_tools_own_realtime_load_guard() -> None:
    guard = (ROOT / "tools" / "calibration_ring_validate" /
             "calibration_load_guard.py").read_text(encoding="utf-8")
    assert "CALIBRATION_LOAD_MASK = 1 << 2" in guard
    assert "ring_stopped_offline_core1" in guard
    assert "offline calibration requires the" in guard
    assert "offline calibration disturbed TDMA schedule" in guard
    assert "SYSTem:TDMA:LOAD:MASK {" not in guard
    assert "schedule misses changed" not in guard
    for tool in (
            "calibration_clk_coded.py", "calibration_marker_train.py",
            "calibration_sck_train.py", "calibration_data_train.py"):
        source = (ROOT / "tools" / "calibration_ring_validate" /
                  tool).read_text(encoding="utf-8")
        assert "CalibrationLoadGuard" in source
        assert "realtime_calibration_load" in source
