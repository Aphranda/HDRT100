#!/usr/bin/env python3
"""Run and gate the scoped sequence single-board hardware acceptance.

This is an alternative only for the exact sequence source allowlist.  It
does not produce P3, multi-board, waveform, RF, independent edge-count, or
TDMA stability evidence.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import shutil
from pathlib import Path
import sys
import subprocess
import xml.etree.ElementTree as ET
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance import p3_hardware_acceptance as p3
from tools.hardware_acceptance import sequence_tdma_cycle_validate as cycle
from tools.hardware_acceptance import sequence_repeat_validate as repeat
from tools.hardware_acceptance import sequence_start_validate as start
from tools.hardware_acceptance import sequence_position_validate as position
from tools.hardware_acceptance import sequence_feedback_validate as feedback
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError
from tools.calibration_loopback_validate import calibration_loopback_validate as calibration


RECEIPT_SCHEMA = "HAOFV_SEQUENCE_SINGLE_BOARD_RECEIPT_V3"
PROFILE_THREE_MODE = "SEQUENCE_THREE_MODE_FUNCTIONAL"
PROFILE_RESERVATION_FOUNDATION = "RESERVATION_VDC_TIMER1_FOUNDATION"
ACCEPTANCE_SCOPE = "SEQUENCE_SINGLE_BOARD_THREE_MODE_FUNCTIONAL"
FOUNDATION_ACCEPTANCE_SCOPE = "SEQUENCE_SINGLE_BOARD_RESERVATION_VDC_TIMER1_FOUNDATION"
DEFAULT_RECEIPT = Path(
    "config/hardware_acceptance/sequence_single_board_receipt.json")
FINITE_REPEAT = 10
MINIMUM_EVENTS = 9
POSITION_THRESHOLD = 1000
POSITION_COUNTER_INPUT = 3  # IN1 remains connected to the external pulse source.
POSITION_CYCLE_TARGET_MS = 200
# Foundation bench: the external 1 kHz source drives IN1, OUT4 is cabled to IN2,
# and the RJ45 loopback cable is in place.  Every foundation stage below is bound
# to exactly that wiring and refuses an injected/simulated substitute.
INDEPENDENT_SOURCE_INPUT = "IN1"          # physical source, never a SCPI NEXT
INDEPENDENT_SOURCE_DURATION_S = 30
DUAL_SLOT_READY_INPUT = "IN2"             # OUT4 -> IN2 physical READY
DUAL_SLOT_READY_INDEX = 2
POSITION_EXTERNAL_POSITIONS = 30
POSITION_EXTERNAL_DURATION_S = 40
EXTERNAL_POSITION_SCOPE = "single_board_position_external_finite_positions"
EXTERNAL_POSITION_INPUT_SIMULATION = {
    "method": "physical IN1",
    "physical_ready_verified": True,
    "physical_edge_count_verified": False,
}
STANDALONE_SP8T_SCOPE = "single_board_pad_and_sequence_observation"
STANDALONE_SP8T_POSITIONS = (1, 2, 3, 4, 5, 6, 7, 8, 1)
TIMING_TICK_FIELDS = ("request_ticks", "applied_ticks", "offered_ticks",
                      "returned_ticks", "fire_queued_ticks", "done_ticks")
SCPI_INJECTION_PREFIX = "TRIG:SEQ:INJECT"
FOUNDATION_STAGE_REPORT_KEYS = (
    "independent_source_report", "dual_slot_physical_report",
    "turntable_external_report", "standalone_sp8t_report",
)
CALIBRATION_WORDS = 128
CALIBRATION_RUNS = 3
CALIBRATION_DURATION_S = 5.0
HOST_TESTS = (
    "tests/python/test_refmem_layout.py",
    "tests/python/test_refmem_pack_build.py",
    "tests/python/test_refmem_sequence_role_scpi.py",
    "tests/python/test_sequence_scpi_runtime.py",
)
FOUNDATION_HOST_TESTS = HOST_TESTS + (
    "tests/python/test_trigger_reservation_config.py",
    "tests/python/test_trigger_reservation_clock.py",
    "tests/python/test_trigger_reservation_planner.py",
    "tests/python/test_vdc_timestamp_clock.py",
    "tests/python/test_sequence_flight_clock.py",
    "tests/python/test_sync_io_workspace.py",
    "tests/python/test_calibration_loopback_validate.py",
    "tests/python/test_vdc_dpll_owner_boundary.py",
    "tests/python/test_vdc_owner_state.py",
    "tests/python/test_vdc_owner_execution.py",
    "tests/python/test_dpll_data_direction.py",
    "tests/python/test_trn03_candidate.py",
    "tests/python/test_trn03_closed_loop.py",
    "tests/python/test_trn03_candidate_import.py",
    "tests/python/test_scpi_serial.py",
    "tests/python/test_tdma_single_board_loopback.py",
    "tests/python/test_sequence_realtime_dispatch.py",
    "tests/python/test_sequence_trigger_debug_ui.py",
    "tests/python/test_refmem_vector_projection.py",
    "tests/python/test_tdma_foundation_snapshot.py",
)
PROFILE_HOST_TESTS = {
    PROFILE_THREE_MODE: HOST_TESTS,
    PROFILE_RESERVATION_FOUNDATION: FOUNDATION_HOST_TESTS,
}
PROFILE_SCOPES = {
    PROFILE_THREE_MODE: ACCEPTANCE_SCOPE,
    PROFILE_RESERVATION_FOUNDATION: FOUNDATION_ACCEPTANCE_SCOPE,
}
BASE_LIMITATIONS = frozenset({
    "no_p3", "no_multi_board", "no_waveform", "no_rf",
    "no_independent_edge_count", "no_tdma_stability",
})
PROFILE_LIMITATIONS = {
    PROFILE_THREE_MODE: BASE_LIMITATIONS,
    PROFILE_RESERVATION_FOUNDATION: BASE_LIMITATIONS | frozenset({
        "foundation_only", "no_multi_board_vdc", "no_sync_accuracy",
        "no_physical_deadline_edge", "no_formal_calibration",
    }),
}
VALIDATOR_PATHS = {
    "gate": "tools/hardware_acceptance/sequence_single_board_gate.py",
    "cycle": "tools/hardware_acceptance/sequence_tdma_cycle_validate.py",
    "repeat": "tools/hardware_acceptance/sequence_repeat_validate.py",
    "start": "tools/hardware_acceptance/sequence_start_validate.py",
    "position": "tools/hardware_acceptance/sequence_position_validate.py",
    "feedback": "tools/hardware_acceptance/sequence_feedback_validate.py",
    "gui": "tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py",
    "vdc_domain_host": "tools/tests/run_vdc_domain_tests.ps1",
    "calibration": "tools/calibration_loopback_validate/calibration_loopback_validate.py",
}

PRODUCTION_ALLOWLIST = frozenset({
    "application/src/app.c",
    "application/src/app_runtime.c",
    "components/distributed_refmem/inc/distributed_refmem.h",
    "components/distributed_refmem/inc/refmem_realtime_tdma.h",
    "components/distributed_refmem/src/refmem_realtime_tdma.c",
    "components/distributed_refmem/inc/refmem_vector_table.h",
    "components/distributed_refmem/src/refmem_application_model.c",
    "components/distributed_refmem/src/refmem_vector_table.c",
    "components/sync_io/inc/sync_io_persona_resources.h",
    "components/sync_io/src/sync_io.c",
    "components/sync_io/src/sync_io_core_internal.h",
    "components/sync_io/src/sync_io_persona_resources.c",
    "middleware/scpi_port/inc/scpi_sequence_node_commands.h",
    "components/distributed_refmem/inc/refmem_sequence_roles.h",
    "components/distributed_refmem/src/distributed_refmem.c",
    "components/sync_io/inc/sync_io_sequence.h",
    "components/sync_io/src/sync_io_sequence.c",
    "components/sync_io/src/sync_io_sequence.pio",
    "components/sync_trigger/inc/trigger_sequence_link.h",
    "components/sync_trigger/inc/trigger_sequence_link_protocol.h",
    "components/sync_trigger/inc/trigger_sequence_service.h",
    "components/sync_trigger/src/trigger_sequence_link.c",
    "components/sync_trigger/src/trigger_sequence_link_protocol.c",
    "components/sync_trigger/src/trigger_sequence_service.c",
    "components/tdma/inc/tdma_pio_spi_ring_adapter.h",
    "components/tdma/src/tdma_pio_spi_ring_adapter.c",
    # Shared TDMA/clock files used by the single-board physical sequence loop.
    # Their inclusion does not extend the receipt to multi-board acceptance.
    "components/tdma/inc/tdma_runtime_owner.h",
    "components/tdma/inc/tdma_service.h",
    "components/tdma/src/tdma_runtime_owner.c",
    "components/tdma/src/tdma_service.c",
    "components/vdc_domain/inc/vdc_timestamp_clock.h",
    "components/vdc_domain/src/vdc_timestamp_clock.c",
    "middleware/scpi_port/inc/scpi_config_commands.h",
    "middleware/scpi_port/src/scpi_config_commands.c",
    "middleware/scpi_port/src/scpi_system_snapshot_commands.c",
    "middleware/scpi_port/inc/scpi_system_snapshot_commands.h",
    "middleware/scpi_port/inc/scpi_sequence_commands.h",
    "middleware/scpi_port/inc/scpi_sequence_node_commands.h",
    "middleware/scpi_port/src/scpi_sequence_commands.c",
    "middleware/scpi_port/src/scpi_sequence_node_commands.c",
})

SUPPORT_ALLOWLIST = frozenset({
    "tests/python/test_app_realtime_release.py",
    "tests/python/test_sequence_angle_scpi.py",
    "tests/python/test_sequence_flight_clock.py",
    "tests/python/test_sequence_gui_settings.py",
    "tests/python/test_sequence_loopback_observe.py",
    "tests/python/test_sequence_timer1_clock.py",
    "tests/python/test_sequence_timing_analyze.py",
    "tests/python/test_tdma_service_nonblocking.py",
    "tests/python/test_tdma_foundation_snapshot.py",
    "tests/unit/test_refmem_realtime_tdma.c",
    "tests/python/test_tdma_snapshot_scpi.py",
    "tests/python/test_tdma_single_board_loopback.py",
    "tests/unit/test_tdma_service_nonblocking.c",
    "tests/unit/test_trigger_sequence_history_vector.c",
    "tests/python/test_refmem_layout.py",
    "tests/python/test_refmem_pack_build.py",
    "tests/python/test_sequence_position_validate.py",
    "tests/python/test_sequence_start_validate.py",
    "tests/python/test_sequence_realtime_dispatch.py",
    "tests/python/test_usb_runtime_switch.py",
    "tests/python/test_visa_ota_update.py",
    "tests/unit/sync_io_sequence_fake.h",
    "tests/unit/test_node_capacity.c",
    "tests/unit/test_refmem_table_registry.c",
    "tests/unit/test_refmem_vdc_vector.c",
    "tests/unit/test_refmem_vector_table.c",
    "tools/refmem_pack_build/refmem_pack_build.py",
    "tools/refmem_table_image/refmem_table_image.py",
    "tools/hardware_acceptance/sequence_position_validate.py",
    "tools/hardware_acceptance/sequence_start_validate.py",
    "tools/usb_runtime_switch/usb_runtime_switch.py",
    "tools/visa_ota_update/visa_ota_update.py",
    ".githooks/pre-commit",
    "tests/python/test_p3_hardware_acceptance.py",
    "tests/python/test_refmem_activation_diagnostic.py",
    "tests/python/test_refmem_scpi_freeze.py",
    "tests/python/test_refmem_sequence_role_scpi.py",
    "tests/python/test_sequence_feedback_validate.py",
    "tests/python/test_sequence_repeat_validate.py",
    "tests/python/test_sequence_scpi_runtime.py",
    "tests/python/test_sequence_single_board_gate.py",
    "tests/python/test_sequence_tdma_cycle_validate.py",
    "tests/python/test_sequence_trigger_acceptance.py",
    "tests/python/test_sequence_trigger_debug_ui.py",
    "tests/python/test_sequence_trigger_debug_ui_layout.py",
    "tests/python/test_sync_io_sequence.py",
    "tests/python/test_trigger_sequence_link.py",
    "tests/python/test_sequence_config_validate.py",
    "tests/unit/test_refmem_sequence_role_scpi.c",
    "tests/unit/test_refmem_activation_diagnostic.c",
    "tests/unit/test_refmem_scpi_freeze.c",
    "tests/unit/test_refmem_sequence_roles.c",
    "tests/unit/test_sequence_scpi_config.c",
    "tests/unit/test_sequence_scpi_runtime.c",
    "tests/unit/test_sync_io_sequence_resources.c",
    "tests/unit/test_sync_io_sequence.c",
    "tests/unit/test_tdma_local_return.c",
    "tests/unit/test_trigger_sequence_link.c",
    "tests/unit/test_trigger_sequence_link_protocol.c",
    "tests/unit/test_trigger_sequence_service.c",
    "tools/hardware_acceptance/sequence_single_board_gate.py",
    "tools/hardware_acceptance/sequence_feedback_validate.py",
    "tools/hardware_acceptance/sequence_repeat_validate.py",
    "tools/hardware_acceptance/sequence_tdma_cycle_validate.py",
    "tools/hardware_acceptance/sequence_trigger_acceptance.py",
    "tools/hardware_acceptance/sequence_start_observe.py",
    "tools/hardware_acceptance/sequence_timing_analyze.py",
    "tools/hardware_acceptance/sequence_config_validate.py",
    "tools/tdma_ring_monitor/tdma_single_board_loopback.py",
    "tools/sequence_trigger_debug_ui/5711_-_Sync_Event.png",
    "tools/sequence_trigger_debug_ui/5711_-_Sync_Event.svg",
    "tools/sequence_trigger_debug_ui/build_windows.py",
    "tools/sequence_trigger_debug_ui/frozen_entry.py",
    "tools/sequence_trigger_debug_ui/requirements-build.txt",
    "tools/sequence_trigger_debug_ui/sequence_debug.spec",
    "tools/sequence_trigger_debug_ui/settings.py",
    "tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py",
})

FOUNDATION_PRODUCTION_ALLOWLIST = frozenset({
    "CMakeLists.txt",
    "application/src/app.c",
    "components/calibration_manager/inc/calibration_manager.h",
    "components/distributed_refmem/inc/distributed_refmem.h",
    "middleware/scpi_port/inc/scpi_sync_commands.h",
    "middleware/scpi_port/src/scpi_calibration_commands.c",
    "middleware/scpi_port/src/scpi_sync_commands.c",
    "middleware/scpi_port/src/scpi_system_snapshot_commands.c",
    "middleware/scpi_port/inc/scpi_system_snapshot_commands.h",
    "components/sync_io/src/sync_io.c",
    "components/sync_io/src/sync_io_model_sched.c",
    "components/sync_io/src/sync_io_logic_analyzer.c",
    "components/sync_io/src/sync_io_analyzer_burst.c",
    "components/sync_trigger/inc/trigger_reservation_clock.h",
    "components/sync_trigger/inc/trigger_reservation_config.h",
    "components/sync_trigger/inc/trigger_reservation_planner.h",
    "components/sync_trigger/src/trigger_reservation_clock.c",
    "components/sync_trigger/src/trigger_reservation_config.c",
    "components/sync_trigger/src/trigger_reservation_planner.c",
    "components/vdc_domain/inc/vdc_domain.h",
    "components/vdc_domain/inc/vdc_timestamp_clock.h",
    "components/vdc_domain/src/vdc_domain.c",
    "components/vdc_domain/src/vdc_timestamp_clock.c",
    "components/vdc_dpll_manager/inc/vdc_dpll_manager.h",
    "components/vdc_dpll_manager/inc/vdc_timer1_coordinate.h",
    "components/vdc_dpll_manager/src/vdc_dpll_manager.c",
    "components/calibration_manager/src/calibration_manager.c",
    "components/distributed_refmem/src/distributed_refmem.c",
})
FOUNDATION_SUPPORT_ALLOWLIST = frozenset({
    "tests/python/test_trigger_reservation_clock.py",
    "tests/python/test_trigger_reservation_config.py",
    "tests/python/test_trigger_reservation_planner.py",
    "tests/python/test_sync_io_workspace.py",
    "tests/python/test_calibration_loopback_validate.py",
    "tests/python/test_vdc_dpll_owner_boundary.py",
    "tests/python/test_vdc_owner_state.py",
    "tests/python/test_vdc_owner_execution.py",
    "tests/python/test_dpll_data_direction.py",
    "tests/python/test_trn03_candidate.py",
    "tests/python/test_trn03_closed_loop.py",
    "tests/python/test_trn03_candidate_import.py",
    "tests/python/test_scpi_serial.py",
    "tests/python/test_tdma_single_board_loopback.py",
    "tests/python/test_sequence_realtime_dispatch.py",
    "tests/python/test_sequence_trigger_debug_ui.py",
    "tests/unit/test_trigger_reservation_clock.c",
    "tests/unit/test_trigger_reservation_config.c",
    "tests/unit/test_trigger_reservation_planner.c",
    "tests/unit/test_vdc_domain.c",
    "tools/hardware_acceptance/sequence_single_board_gate.py",
    "tests/python/test_sequence_single_board_gate.py",
    "tools/calibration_loopback_validate/calibration_loopback_validate.py",
    "tools/scpi_common/vdc_owner_state.py",
    "tools/calibration_ring_validate/trn03_closed_loop.py",
    "tools/calibration_ring_validate/trn03_candidate_import.py",
    "tools/scpi_common/scpi_serial.py",
    "tools/tdma_ring_monitor/tdma_single_board_loopback.py",
    "tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py",
    # Validators and tests re-synchronised with this slice's protocol changes: the
    # shared SCPI reader's QUEUED/owner handshake, the board_uptime_ms time source
    # in tdma_component_core1_service, and the vdc_timestamp_clock_try_read_ns
    # hardware-clock deadline in sync_io_wave_output_start.  They are covered by
    # the single-board run so the foundation profile can stage them; none of them
    # extends the receipt beyond the foundation scope.
    "tools/hardware_acceptance/sequence_tdma_cycle_validate.py",
    "tests/python/test_sequence_tdma_cycle_validate.py",
    "tools/tdma_ring_monitor/tdma_start_ring.py",
    "tests/python/test_tdma_start_ring.py",
    "tests/python/test_tdma_arm_diagnostics.py",
    "tests/python/test_state_machine_resource_check.py",
    "tests/unit/test_tdma_origin_admission.c",
})
FOUNDATION_ANCHORS = frozenset({
    "components/sync_trigger/src/trigger_reservation_clock.c",
    "components/sync_trigger/src/trigger_reservation_config.c",
    "components/sync_trigger/src/trigger_reservation_planner.c",
})
FOUNDATION_SOURCE_ALLOWLIST = (
    FOUNDATION_PRODUCTION_ALLOWLIST | FOUNDATION_SUPPORT_ALLOWLIST)
SOURCE_ALLOWLIST = (
    PRODUCTION_ALLOWLIST | SUPPORT_ALLOWLIST | FOUNDATION_SOURCE_ALLOWLIST)
SOURCE_EXCLUDES = {
    DEFAULT_RECEIPT.as_posix(),
    p3.DEFAULT_RECEIPT.as_posix(),
}


def _normalized(path: str | Path) -> str:
    return str(path).replace("\\", "/").removeprefix("./")


def is_single_board_source(path: str | Path) -> bool:
    normalized = _normalized(path)
    return normalized not in SOURCE_EXCLUDES and p3.is_acceptance_source(normalized)


def _tree_digest(rows: Iterable[tuple[str, str, str]]) -> tuple[str, int]:
    digest = hashlib.sha256()
    count = 0
    for path, mode, blob_id in sorted(rows):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(mode.encode("ascii"))
        digest.update(b"\0")
        digest.update(blob_id.encode("ascii"))
        digest.update(b"\n")
        count += 1
    return digest.hexdigest(), count


def _index_entries(root: Path) -> list[tuple[str, str, str]]:
    raw = p3._run_git(root, "ls-files", "--stage", "-z")
    entries = []
    for entry in raw.split(b"\0"):
        if not entry or b"\t" not in entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        fields = metadata.split()
        if len(fields) != 3 or fields[2] != b"0":
            continue
        path = raw_path.decode("utf-8", errors="surrogateescape")
        if is_single_board_source(path):
            entries.append((_normalized(path), fields[0].decode("ascii"),
                            fields[1].decode("ascii")))
    return sorted(entries)


def staged_source_fingerprint(root: Path = ROOT) -> tuple[str, int]:
    return _tree_digest(_index_entries(root))


def working_source_fingerprint(root: Path = ROOT) -> tuple[str, int]:
    entries = {path: (mode, oid) for path, mode, oid in _index_entries(root)}
    raw = p3._run_git(root, "ls-files", "--modified", "--deleted", "--others",
                      "--exclude-standard", "-z")
    for raw_path in raw.split(b"\0"):
        if not raw_path:
            continue
        path = _normalized(raw_path.decode("utf-8", errors="surrogateescape"))
        if not is_single_board_source(path):
            continue
        full = root / path
        if not full.is_file():
            entries.pop(path, None)
            continue
        blob_id = p3._run_git(
            root, "hash-object", f"--path={path}", path).decode("ascii").strip()
        entries[path] = (entries.get(path, ("100644", ""))[0], blob_id)
    return _tree_digest(
        (path, mode, oid) for path, (mode, oid) in entries.items())


def changed_staged_sources(root: Path = ROOT) -> list[str]:
    raw = p3._run_git(root, "diff", "--cached", "--name-only", "-z")
    return sorted(
        _normalized(item.decode("utf-8", errors="surrogateescape"))
        for item in raw.split(b"\0")
        if item and is_single_board_source(
            item.decode("utf-8", errors="surrogateescape")))


def acceptance_profile(paths: Iterable[str]) -> str:
    changed = {_normalized(path) for path in paths}
    if changed and changed <= PRODUCTION_ALLOWLIST | SUPPORT_ALLOWLIST:
        return PROFILE_THREE_MODE
    if (changed and changed <= FOUNDATION_SOURCE_ALLOWLIST and
            bool(changed & FOUNDATION_ANCHORS)):
        return PROFILE_RESERVATION_FOUNDATION
    raise AcceptanceError(
        "staged source is outside the sequence single-board profile allowlists")


def validate_scope(paths: Iterable[str]) -> list[str]:
    changed = sorted({_normalized(path) for path in paths})
    if not changed:
        raise AcceptanceError("single-board gate has no staged source change")
    acceptance_profile(changed)
    return changed


def _load_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise AcceptanceError(f"invalid {label} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise AcceptanceError(f"{label} {path} is not an object")
    return value


def _is_no_error(value: object) -> bool:
    return isinstance(value, str) and value.lstrip().startswith("0,")


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise AcceptanceError(message)


def _last(rows: object, label: str) -> dict:
    _require(isinstance(rows, list) and bool(rows) and
             all(isinstance(row, dict) for row in rows), f"missing {label} observations")
    return rows[-1]


def _zero_io(io: dict) -> None:
    _require(all(io.get(key) == 0 for key in ("outputs", "owned", "armed", "busy")),
             "sequence IO was not released")


def _ring_stopped(rows: list) -> None:
    row = _last(rows, "TDMA STOP")
    _require(all(cycle.ring.field(row, index) == 0 for index in
                 (cycle.ring.RING_ENABLED, cycle.ring.RING_ADAPTER_STARTED)),
             "TDMA owner did not stop")


def _quiet(report: dict, baseline: dict | None = None) -> None:
    before = report["idle_before_quiet"]["sequence"]
    after = report["idle_after_quiet"]["sequence"]
    keys = ("run_id", "generation", "accepted", "completed", "cancelled", "faults", "backend_fault")
    _require(before["state"] == after["state"] == "IDLE" and
             all(before[k] == after[k] for k in keys), "sequence changed while IDLE")
    if baseline is not None:
        _require(all(before[k] == baseline[k] for k in keys), "quiet result changed run or counters")
    for name in ("idle_before_quiet", "idle_after_quiet"):
        _zero_io(report[name]["io"])


def _common_report(report: dict, scope: str, serial_number: str, build_id: str,
                   tool_sha256: str, claims: tuple[str, ...]) -> dict:
    _require(report.get("passed") is True and report.get("failure") is None and
             report.get("cleanup_failures") == [] and report.get("scope") == scope,
             f"{scope} report is not PASS")
    _require(report.get("tool_sha256") == tool_sha256, "report validator changed")
    settings, identity = report["settings"], report["identity"]
    _require(settings["serial_number"] == serial_number and settings["build"] == build_id and
             len(identity["idn"]) == 4 and identity["idn"][2] == serial_number and
             identity["build"] == build_id, "report board/build identity mismatch")
    _require(all(report.get(claim) is False for claim in claims), "report overclaims acceptance")
    return settings


def position_duration(source_hz: float) -> float:
    _require(isinstance(source_hz, (int, float)) and not isinstance(source_hz, bool) and
             math.isfinite(source_hz) and source_hz > 0, "source frequency must be positive and finite")
    return 2 * POSITION_THRESHOLD / source_hz + 15


def external_position_wait_s(source_hz: float) -> float:
    """Quiet-capture window required by the honest external turntable profile.

    Mirrors sequence_position_validate.quiet_wait_seconds for the fixed
    POSITION_EXTERNAL_POSITIONS profile; the declared frequency is a timeout
    input, never a measured edge count.
    """
    _require(isinstance(source_hz, (int, float)) and not isinstance(source_hz, bool) and
             math.isfinite(source_hz) and source_hz > 0, "source frequency must be positive and finite")
    return (POSITION_EXTERNAL_POSITIONS * POSITION_THRESHOLD / source_hz +
            POSITION_CYCLE_TARGET_MS / 1000 + 1)


def external_position_duration(source_hz: float) -> float:
    """Fixed floor of POSITION_EXTERNAL_DURATION_S, widened for slower sources."""
    return max(POSITION_EXTERNAL_DURATION_S, external_position_wait_s(source_hz))


def validate_repeat_report(report: dict, *, serial_number: str, build_id: str,
                           tool_sha256: str, source_hz: float,
                           source: str = "MANUAL") -> None:
    """Validate one finite independent SP8T repeat profile.

    ``source == "MANUAL"`` is the software-NEXT profile and keeps its original
    strictness; ``source == INDEPENDENT_SOURCE_INPUT`` additionally rejects every
    software-driven advance.
    """
    _require(source in ("MANUAL", INDEPENDENT_SOURCE_INPUT),
             "unknown independent repeat source profile")
    settings = _common_report(report, "single_board_independent_sp8t_repeat", serial_number,
        build_id, tool_sha256, ("independent_input_count_verified", "external_waveform_verified",
                              "rf_path_verified", "p3_receipt"))
    _require(settings["source"] == source and settings["repeat"] == FINITE_REPEAT and
             settings["configure_only"] is False and settings["source_hz"] == source_hz and
             report.get("functional_execution_verified") is True,
             "independent software-trigger execution profile mismatch")
    if source == "MANUAL":
        _require(report.get("software_input_simulation_verified") is True and
                 report["software_next_sent"] == 8 * FINITE_REPEAT - 1,
                 "independent software-trigger execution profile mismatch")
    else:
        # Fail closed: the physical IN1 profile must not carry any software
        # advance, and the MANUAL report's 8*repeat-1 NEXT count must not appear.
        _require(report.get("software_input_simulation_verified") is False and
                 report.get("software_next_sent") == 0,
                 "independent physical source report used software NEXT advances")
    _require(report["configured_repeat"]["configured"] == FINITE_REPEAT and
             report["repeat_result"] == dict(configured=FINITE_REPEAT, active=FINITE_REPEAT, finished=1),
             "independent finite repeat did not finish")
    rows = report["samples"]
    final = _last(rows, "independent runtime")["sequence"]
    _require(final["run_id"] > 0 and final["generation"] > 0 and final["state"] == "IDLE" and
             final["accepted"] == final["completed"] == 8 * FINITE_REPEAT - 1,
             "independent terminal step mismatch")
    previous = None
    for entry in rows:
        row = entry["sequence"]
        # START may initially return the old owner snapshot.
        if (row["run_id"], row["generation"]) != (final["run_id"], final["generation"]):
            _require(previous is None, "independent run changed")
            continue
        repeat.check_status(row, previous, 8 * FINITE_REPEAT - 1)
        previous = row
    _quiet(report, final)
    _quiet(report["cleanup"])
    _ring_stopped(report["cleanup"]["ring_stop_samples"])


def validate_independent_source_report(report: dict, *, serial_number: str, build_id: str,
                                       tool_sha256: str, source_hz: float) -> None:
    """Require the finite independent repeat to be driven by the physical IN1 source."""
    validate_repeat_report(report, serial_number=serial_number, build_id=build_id,
                           tool_sha256=tool_sha256, source_hz=source_hz,
                           source=INDEPENDENT_SOURCE_INPUT)


def validate_start_report(report: dict, *, serial_number: str, build_id: str,
                          tool_sha256: str) -> None:
    settings = _common_report(report, "single_board_start_status_samples", serial_number,
        build_id, tool_sha256, ("external_waveform_verified", "independent_pulse_count_verified",
                              "rf_path_verified", "p3_receipt"))
    _require(settings["output_only"] is False and report["io_loopback_verified"] is True and
             report["output_pad_sequence_verified"] is True, "START requires OUT4 to IN2 loopback")
    _require([settings[k] for k in ("settle_us", "pulse_us", "abort_pulse_us")] ==
             [300000, 300000, 1500000], "START fixed observation timing mismatch")
    for name, mode in (("pulse", "PULSE"), ("level", "LEVEL"), ("none", "NONE"),
                       ("pulse_reload", "PULSE"), ("singleton", "PULSE"),
                       ("stop_startup", "PULSE"), ("restart_after_stop", "PULSE")):
        phase = report["phases"][name]
        _require(phase["passed"] is True and phase["failure"] is None, f"START {name} failed")
        singleton = name == "singleton"
        plan = start.PLAN[:1] if singleton else start.PLAN
        config = phase["configuration"]
        _require(config["plan"][:2] == ["SP8T", "4294967295"] and
                 config["plan"][3:] == [str(len(plan)), "0", *map(str, plan)] and
                 config["source"] == ["MANUAL", "RISING"] and
                 config["repeat"]["configured"] == int(singleton), "START configuration mismatch")
        pulse_us = settings["abort_pulse_us"] if name == "stop_startup" else settings["pulse_us"]
        output = config["output"]
        _require(len(output) == 7 and output[:5] == ["7", "0" if mode == "NONE" else "8",
                 mode, str(settings["settle_us"]), str(pulse_us if mode == "PULSE" else 0)] and
                 output[-1] == "1", "START output readback mismatch")

        def pad(key: str, code: int, high: bool) -> None:
            io = _last(phase[key], f"{name} {key}")
            _require(io["owned"] == (7 if mode == "NONE" else 15) and
                     io["outputs"] == code | (8 if high else 0) and bool(io["inputs"] & 2) == high,
                     f"START {name} {key} pad/loopback mismatch")

        pad("settling_low", plan[0], False)
        if mode != "NONE":
            pad("startup_high", plan[0], True)
        if name == "stop_startup":
            baseline = phase["before_stop"]
            start.check_result(baseline, plan, 0)
            pad("high_before_stop", plan[0], True)
            start.check_result(phase["stopped"], plan, 0, baseline)
            _quiet(phase["quiet"], phase["stopped"])
            continue
        baseline = _last(phase["startup_result"], "START result")
        _require(baseline["state"] == ("IDLE" if singleton else "READY") and
                 baseline["run_id"] > 0 and baseline["generation"] > 0, "START owner state mismatch")
        start.check_result(baseline, plan, 0)
        _require(phase["repeat_result"] == dict(configured=int(singleton), active=int(singleton),
                                               finished=int(singleton)), "START repeat accounting mismatch")
        if singleton:
            _quiet(phase["quiet"], baseline)
        else:
            pad("startup_settled", plan[0], mode == "LEVEL")
            start.check_result(phase["startup_quiet"], plan, 0, baseline)
            _require(phase["startup_quiet"]["state"] == "READY", "START did not stay READY")
            if mode == "PULSE":
                pad("next_high", plan[1], True)
            for row in (_last(phase["next_result"], "NEXT result"), phase["next_quiet"]):
                start.check_result(row, plan, 1, baseline)
                _require(row["state"] == "READY", "NEXT did not stay READY")
            pad("next_settled", plan[1], mode == "LEVEL")
    _quiet(report["cleanup"])
    _ring_stopped(report["cleanup"]["ring_stop_samples"])


def _position_configuration(profile: dict, *, repeat_count: int, manual: bool,
                            gui_sha256: str) -> None:
    _require(profile["passed"] is True and profile["flight_mode"] == "2" and
             profile["repeat_configuration"]["configured"] == repeat_count and
             profile["gui_control"]["module_sha256"] == gui_sha256,
             "POSITION configuration/GUI identity mismatch")
    row, counter = profile["configured"]["link"], profile["configured"]["counter"]
    _require([counter[k] for k in ("enabled", "slot", "input", "threshold")] ==
             [1, 1, POSITION_COUNTER_INPUT, POSITION_THRESHOLD] and
             [row[k] for k in ("enabled", "phase", "error", "dutslot", "vnaslot", "input", "outputmask")] ==
             [1, 1, 0, 2, 3, 0 if manual else 2, 8], "POSITION slot/IO readback mismatch")
    for instance, name in ((2, "COUNTER"), (5, "DUT"), (7, "VNA")):
        role = position.parse_role(profile["roles"][str(instance)], instance, name)
        _require(role["active_enabled"] == 1, "POSITION role not active")
    _require(next(csv.reader([profile["plan"]]))[-8:] == list(map(str, range(8))) and
             profile["codes"] == {str(i): f"{i},{i}" for i in range(8)}, "POSITION plan/code mismatch")


def _position_injection(profile: dict, counts: list[int], manual_ready: bool) -> None:
    simulation = profile.get("input_simulation", {})
    _require(simulation == {
        "method": "SCPI", "counter_command": f"TRIG:SEQ:INJECT IN{POSITION_COUNTER_INPUT},<count>",
        "ready_command": "TRIG:SEQ:INJECT READY,<count>" if manual_ready else None,
        "physical_edge_count_verified": False, "physical_ready_verified": not manual_ready},
        "POSITION input simulation scope mismatch")
    injections = profile.get("injections")
    _require(isinstance(injections, list) and [row.get("count") for row in injections] == counts,
             "POSITION SCPI injection batches mismatch")
    for row, count in zip(injections, counts):
        _require(row.get("command") == f"TRIG:SEQ:INJECT IN{POSITION_COUNTER_INPUT},{count}" and
                 row.get("response") == "1" and isinstance(row.get("issued_at"), (int, float)),
                 "POSITION SCPI injection evidence invalid")
    ready = profile.get("ready_injection")
    _require(ready is None, "POSITION profile must not preload READY")


def validate_position_report(report: dict, *, serial_number: str, build_id: str,
                             tool_sha256: str, gui_sha256: str, source_hz: float) -> None:
    settings = _common_report(report, "single_board_position_two_rounds_and_busy_boundary",
        serial_number, build_id, tool_sha256, ("waveform_verified", "independent_input_count_verified",
                                              "rf_path_verified", "multi_board_verified", "p3_receipt"))
    _require(report.get("software_input_simulation_verified") is True,
             "POSITION software input simulation claim missing")
    duration = position_duration(source_hz)
    _require(settings["threshold"] == POSITION_THRESHOLD and settings["lifecycle"] is True and
             settings["source_hz"] == source_hz and settings["duration"] >= duration and
             settings.get("counter_input") == f"IN{POSITION_COUNTER_INPUT}" and
             settings["gateway_timeout_ms"] == 10000 and
             settings["position_cycle_target_ms"] == POSITION_CYCLE_TARGET_MS,
             "POSITION fixed profile or frequency-derived duration mismatch")
    for name, manual in (("two_positions", False), ("busy_boundary", True)):
        profile = report[name]
        _position_configuration(profile, repeat_count=2, manual=manual, gui_sha256=gui_sha256)
        _position_injection(profile, [POSITION_THRESHOLD - 1, 1, POSITION_THRESHOLD], manual)
        final = _last(profile["samples"], name)
        link, counter = final["link"], final["counter"]
        previous = None
        prethreshold, wait_ready = False, False
        for sample in profile["samples"]:
            row, count = sample["link"], sample["counter"]
            _require(count["enabled"] == 1 and count["input"] == POSITION_COUNTER_INPUT and
                     count["threshold"] == POSITION_THRESHOLD,
                     "POSITION counter binding changed")
            if row["phase"] == 1 and previous is None:
                continue
            _require(row["run"] > 0 and row["generation"] > 0 and row["repeat"] == 2 and
                     all(row[k] == link[k] for k in cycle.IDENTITY_FIELDS), "POSITION run identity mismatch")
            if previous:
                _require(count["events"] >= previous["counter"]["events"], "POSITION pulse counter regressed")
            if count["events"] < POSITION_THRESHOLD:
                _require(row["triggers"] == row["ready"] == row["completed"] == 0,
                         "POSITION sampling before threshold")
                prethreshold = True
            wait_ready |= row["phase"] == 3 and row["triggers"] == 1
            if not manual:
                _require(row["error"] == count["error"] == 0 and
                         row["triggers"] <= count["positions"] * 8, "POSITION unexpected fault/extra sample")
            previous = sample
        _require(prethreshold and profile["no_premature_sample_observed"] is True,
                 "POSITION lacks pre-threshold observation")
        owner = _last(profile["terminal_owner_samples"], "POSITION terminal owner")
        _require((owner["run_id"], owner["generation"]) == (link["run"], link["generation"]) and
                 owner["count"] == 8, "POSITION terminal owner identity mismatch")
        if manual:
            _require(profile["expected_busy_fault"] == final and wait_ready and
                     link["phase"] == counter["phase"] == 7 and link["error"] == counter["error"] == 5 and
                     counter["fault_events"] >= 2 * POSITION_THRESHOLD and counter["positions"] == 1 and
                     link["triggers"] == 1 and link["ready"] == link["completed"] == 0 and
                     owner["accepted"] == owner["completed"] == 0, "POSITION busy fault not proven")
            driver = (owner["state"] == "FAULT" and owner["backend_fault"] == position.BACKEND_COUNTER_BUSY and
                      owner["error"] == "BACKEND_FAULT" and owner["faults"] > 0)
            stopped = (owner["state"] == "IDLE" and owner["backend_fault"] == owner["faults"] == 0 and
                       owner["error"] == "NONE")
            _require(driver or stopped, "POSITION unexpected busy terminal owner")
        else:
            _require(link["phase"] == 8 and link["triggers"] == link["ready"] == 16 and
                     link["completed"] == 15 and counter["positions"] == 2 and
                     counter["history_total"] == counter["history_retained"] == 16 and
                     owner["state"] == "IDLE" and owner["accepted"] == owner["completed"] == 15 and
                     owner["error"] == "NONE" and owner["faults"] == owner["backend_fault"] == 0 and
                     all(owner[k] == 7 for k in ("current_index", "current_state", "completed_index", "completed_state")) and
                     profile["terminal_repeat"] == dict(configured=2, run=2, finished=1),
                     "POSITION two-round accounting mismatch")
            expected_cycles = position.check_history(profile["history"], link, POSITION_THRESHOLD,
                target_ms=POSITION_CYCLE_TARGET_MS)
            _require(profile["position_cycles"] == expected_cycles,
                     "POSITION cycle timing summary mismatch")
        for index in (cycle.ring.RING_ADAPTER_TX_COUNT, cycle.ring.RING_ADAPTER_RX_COUNT):
            _require(cycle.ring.delta(profile["ring_before"], profile["ring_after"], index) > 0,
                     "POSITION lacks physical RJ45 counter growth")
    life = report["lifecycle"]
    _position_configuration(life, repeat_count=0, manual=False, gui_sha256=gui_sha256)
    _position_injection(life, [1, 3, POSITION_THRESHOLD - 4], False)
    rows = {key: _last(life[key], f"lifecycle {key}") for key in
            ("waiting", "initial_partial", "paused", "counting_while_paused", "resumed",
             "position_complete", "restarted")}
    baseline = life["lifecycle_identity"]
    for name, entry in rows.items():
        link, owner, counter = entry["link"], entry["owner"], entry["counter"]
        _require(link["error"] == counter["error"] == 0 and owner["error"] == "NONE" and
                 owner["faults"] == owner["backend_fault"] == 0 and
                 (owner["run_id"], owner["generation"]) == (link["run"], link["generation"]) and
                 all(link[k] == baseline[k] for k in cycle.IDENTITY_FIELDS if name != "restarted" or k != "run"),
                 "POSITION lifecycle identity/fault mismatch")
    waiting, initial, paused, counted, resumed, done, restarted = (rows[k] for k in rows)
    _require(waiting["link"]["phase"] == 9 and waiting["counter"]["events"] == 0 and
             waiting["counter"]["positions"] == waiting["link"]["triggers"] == 0,
             "POSITION did not wait for first position")
    _require(initial["link"]["phase"] == 9 and initial["counter"]["events"] == 1 and
             initial["counter"]["positions"] == initial["link"]["triggers"] == 0,
             "POSITION initial simulated partial pulse mismatch")
    _require(paused["owner"]["state"] == counted["owner"]["state"] == "PAUSED" and
             paused["link"]["phase"] == 6 and counted["counter"]["events"] >= paused["counter"]["events"] + 3 and
             counted["counter"]["positions"] == counted["link"]["triggers"] == 0 and
             resumed["owner"]["state"] == "READY" and resumed["link"]["phase"] == 9 and
             resumed["counter"]["events"] >= counted["counter"]["events"],
             "POSITION PAUSE/CONT counter preservation mismatch")
    _require(position.position_finished_observed(done) and done["link"]["repeat"] == 0 and
             done["link"]["triggers"] == done["link"]["ready"] == 8 and
             done["owner"]["accepted"] == done["owner"]["completed"] == 7 and
             all(done["owner"][k] == 7 for k in ("current_index", "current_state", "completed_index", "completed_state")),
             "POSITION lifecycle round not complete")
    expected_cycles = position.check_history(life["history"], done["link"], POSITION_THRESHOLD,
        positions=1, target_ms=POSITION_CYCLE_TARGET_MS)
    _require(life["position_cycles"] == expected_cycles,
             "POSITION lifecycle timing summary mismatch")
    _require(restarted["owner"]["state"] == "READY" and restarted["link"]["phase"] == 9 and
             restarted["link"]["run"] > baseline["run"] and
             restarted["counter"]["positions"] == restarted["counter"]["history_total"] == 0 and
             restarted["counter"]["events"] < POSITION_THRESHOLD, "POSITION restart retained old run")
    for key in ("stop", "restart_stop"):
        owner = life[key]
        _require(owner["state"] == "IDLE" and owner["error"] == "NONE" and
                 owner["faults"] == owner["backend_fault"] == 0, "POSITION lifecycle STOP fault")
        _zero_io(life[key + "_io"])
    _require(report["stopped"]["state"] == "IDLE", "POSITION cleanup owner not IDLE")
    _zero_io(report["stopped_io"])
    _ring_stopped(report["ring_stopped_samples"])


def _check_external_timing(record: dict, clock_hz: int) -> None:
    """Require versioned TIMER1 stage offsets that reconcile with the retained history."""
    timing = record.get("timing")
    _require(record.get("outcome_flags") == 7 and isinstance(timing, dict) and
             timing.get("version") == 1 and timing.get("flags") == 63 and
             timing.get("clock_hz") == clock_hz and
             all(timing.get(key) == record.get(key) for key in
                 ("ordinal", "run", "generation", "binding_epoch", "exchange_id",
                  "position", "sequence_index")),
             "external turntable per-state timing identity is invalid")
    ticks = [timing.get(key) for key in TIMING_TICK_FIELDS]
    _require(all(isinstance(value, int) for value in ticks) and ticks == sorted(ticks) and
             timing["done_ticks"] * 1000 // timing["clock_hz"] == record.get("cycle_elapsed_ms"),
             "external turntable per-state timing evidence is invalid")


def validate_turntable_external_report(report: dict, *, serial_number: str, build_id: str,
                                       tool_sha256: str, source_hz: float) -> None:
    """Validate the turntable profile driven only by the external physical IN1 source.

    Every SCPI counter or READY injection is rejected: this stage exists to prove
    the physical source can complete the fixed 30-position profile without the
    software-injected path that the two-round POSITION stage uses.
    """
    settings = _common_report(report, EXTERNAL_POSITION_SCOPE, serial_number, build_id,
        tool_sha256, ("waveform_verified", "independent_input_count_verified",
                      "rf_path_verified", "multi_board_verified", "p3_receipt"))
    _require(report.get("software_input_simulation_verified") is False,
             "external turntable report claims software input simulation")
    profile = report.get("two_positions")
    _require(isinstance(profile, dict) and profile.get("passed") is True and
             profile.get("probe_mode") == "post_run_vector",
             "external turntable profile is not the quiet post-run vector PASS")
    _require(settings["external_input"] is True and
             settings["counter_input"] == INDEPENDENT_SOURCE_INPUT and
             settings["positions"] == POSITION_EXTERNAL_POSITIONS and
             settings["threshold"] == POSITION_THRESHOLD and
             settings["quiet_capture"] is True and
             settings["timing_evidence"] is True and
             settings["schedule_evidence"] is True and
             settings["angle_scan"] is False and settings["lifecycle"] is False and
             settings["source_hz"] == source_hz and
             settings["duration"] == external_position_duration(source_hz) and
             settings["gateway_timeout_ms"] == 10000 and
             settings["position_cycle_target_ms"] == POSITION_CYCLE_TARGET_MS,
             "external turntable fixed profile or declared frequency mismatch")
    _require(profile.get("input_simulation") == EXTERNAL_POSITION_INPUT_SIMULATION and
             "injections" not in profile and "ready_injection" not in profile,
             "external turntable report carries SCPI input simulation evidence")
    for row in report.get("transcript", []):
        command = row.get("command", "") if isinstance(row, dict) else ""
        _require(not command.upper().startswith(SCPI_INJECTION_PREFIX),
                 "external turntable report issued a SCPI injection command")
    silence = profile.get("quiet_capture")
    wait_s = external_position_wait_s(source_hz)
    _require(isinstance(silence, dict) and
             silence.get("commands_during_wait") == 0 and
             silence.get("planned_wait_s") == wait_s and
             isinstance(silence.get("transcript_count_after_start"), int) and
             silence.get("transcript_count_before_first_query") ==
             silence.get("transcript_count_after_start") and
             isinstance(silence.get("elapsed_s"), (int, float)) and
             silence["elapsed_s"] >= wait_s and silence.get("start_response") == "1",
             "external turntable quiet window captured host commands")
    measurements = POSITION_EXTERNAL_POSITIONS * 8
    vector = profile.get("history_vector_after")
    drained = profile.get("history_vector_after_drain")
    before = profile.get("history_vector_before")
    _require(isinstance(before, dict) and isinstance(vector, dict) and isinstance(drained, dict) and
             before.get("capacity") == vector.get("capacity") and
             vector["capacity"] >= measurements and
             isinstance(vector.get("clock_hz"), int) and vector["clock_hz"] > 0 and
             before.get("clock_hz") == vector.get("clock_hz") and
             vector.get("run") != before.get("run") and
             vector.get("threshold") == POSITION_THRESHOLD and
             vector.get("total") == vector.get("retained") == measurements and
             vector.get("overwritten") == 0 and
             all(drained.get(key) == vector.get(key) for key in position.HISTORY_STATUS_FIELDS),
             "external turntable history vector evidence is incomplete")
    records = profile.get("history")
    _require(isinstance(records, list) and len(records) == measurements and
             [record.get("ordinal") for record in records] == list(range(1, measurements + 1)) and
             profile.get("history_thresholds_verified") is True,
             "external turntable retained history is incomplete")
    for record in records:
        _check_external_timing(record, vector["clock_hz"])
    cycles = profile.get("position_cycles")
    _require(isinstance(cycles, list) and
             [row.get("position") for row in cycles] ==
             list(range(1, POSITION_EXTERNAL_POSITIONS + 1)) and
             all(row.get("target_ms") == POSITION_CYCLE_TARGET_MS and
                 row.get("threshold_pulse_ordinal") ==
                 row.get("position", 0) * POSITION_THRESHOLD for row in cycles),
             "external turntable per-position cycle summary mismatch")
    _require(profile.get("terminal_repeat") ==
             dict(configured=POSITION_EXTERNAL_POSITIONS, run=POSITION_EXTERNAL_POSITIONS,
                  finished=1) and
             _is_no_error(profile.get("error_before")) and
             _is_no_error(profile.get("error_after")) and
             profile.get("backend_timing") == ["PIO0", "4", "0"],
             "external turntable terminal accounting mismatch")
    owner = _last(profile.get("terminal_owner_samples"), "external turntable terminal owner")
    _require(owner["state"] == "IDLE" and owner["error"] == "NONE" and owner["count"] == 8 and
             owner["faults"] == owner["backend_fault"] == 0 and
             owner["accepted"] == owner["completed"] == measurements - 1,
             "external turntable owner did not finish in IDLE")
    for index in (cycle.ring.RING_ADAPTER_TX_COUNT, cycle.ring.RING_ADAPTER_RX_COUNT):
        _require(cycle.ring.delta(profile["ring_before"], profile["ring_after"], index) > 0,
                 "external turntable lacks physical RJ45 counter growth")
    scheduler = report.get("scheduler")
    record = scheduler.get("before_cleanup") if isinstance(scheduler, dict) else None
    _require(isinstance(record, dict) and _is_no_error(record.get("error_before")) and
             _is_no_error(record.get("error_after")) and
             record.get("strict_realtime_verified") is False and
             isinstance(record.get("clock_hz"), int) and record["clock_hz"] > 0,
             "external turntable scheduler evidence is missing")
    _require(report["stopped"]["state"] == "IDLE",
             "external turntable cleanup owner not IDLE")
    _zero_io(report["stopped_io"])
    _ring_stopped(report["ring_stopped_samples"])


def validate_standalone_sp8t_report(report: dict, *, serial_number: str, build_id: str,
                                    tool_sha256: str) -> None:
    """Validate the standalone SP8T pad check that deliberately owns no sequence resource."""
    _require(report.get("passed") is True and report.get("failure") is None and
             report.get("scope") == STANDALONE_SP8T_SCOPE and
             report.get("tool_sha256") == tool_sha256,
             "standalone SP8T report is not an intact PASS")
    for claim in ("independent_input_count_verified", "vna_role_runtime_verified",
                  "external_waveform_verified", "rf_path_verified"):
        _require(report.get(claim) is False, f"standalone SP8T report overclaims {claim}")
    settings, identity = report.get("settings"), report.get("identity")
    _require(isinstance(settings, dict) and isinstance(identity, dict) and
             settings.get("serial_number") == serial_number and
             str(settings.get("build")) == str(build_id) and
             settings.get("mode") == "standalone-sp8t" and
             isinstance(identity.get("idn"), list) and len(identity["idn"]) == 4 and
             identity["idn"][2] == serial_number and identity.get("build") == build_id,
             "standalone SP8T report board/build identity mismatch")
    phases = report.get("phases")
    phase = phases.get("standalone-sp8t") if isinstance(phases, dict) else None
    _require(isinstance(phase, dict) and phase.get("passed") is True and
             phase.get("failure") is None and phase.get("cleanup_failure") is None,
             "standalone SP8T phase did not pass cleanly")
    positions = phase.get("positions")
    _require(isinstance(positions, list) and
             all(isinstance(row, dict) for row in positions) and
             [row.get("position") for row in positions] == list(STANDALONE_SP8T_POSITIONS),
             "standalone SP8T positions do not cover 1..8 and return to 1")
    for row in positions:
        readback, io, status = row.get("readback"), row.get("io"), row.get("status")
        _require(isinstance(readback, list) and len(readback) == 5 and
                 readback[:2] == ["1", str(row["position"])],
                 "standalone SP8T switch readback mismatch")
        _require(isinstance(io, dict) and io.get("outputs") == row["position"] - 1 and
                 io.get("owned") == io.get("armed") == io.get("busy") == 0,
                 "standalone SP8T pad mismatch or sequence resource acquired")
        _require(isinstance(status, dict) and status.get("state") == "IDLE" and
                 status.get("error") == "NONE" and
                 status.get("faults") == status.get("backend_fault") == 0 and
                 status == positions[0]["status"],
                 "standalone SP8T changed the sequence runtime")
    stopped = report.get("stopped_status")
    _require(isinstance(stopped, dict) and stopped.get("state") == "IDLE",
             "standalone SP8T cleanup owner not IDLE")
    stopped_io = report.get("stopped_io")
    _require(isinstance(stopped_io, list) and len(stopped_io) == 5 and
             all(str(value) == "0" for value in stopped_io),
             "standalone SP8T cleanup did not release the pads")


def validate_cycle_report(report: dict[str, Any], *, profile: str,
                          serial_number: str, build_id: str,
                          tool_sha256: str, physical_ready: bool = False) -> None:
    """Validate one RJ45 DUT/VNA functional cycle report.

    ``physical_ready=False`` is the SCPI-credit profile used by the finite and
    pause/resume stages and keeps its original strictness.  ``physical_ready=True``
    requires the READY credits to come from the cabled OUT4->IN2 edge instead.
    """
    if (report.get("passed") is not True or
            report.get("functional_cycle_verified") is not True or
            report.get("failure") is not None or
            report.get("cleanup_failures") != []):
        raise AcceptanceError(f"{profile} cycle report is not PASS")
    if report.get("scope") != "single_board_rj45_dut_vna_functional_cycle":
        raise AcceptanceError(f"{profile} cycle report has invalid scope")
    for claim in ("tdma_stability_verified", "multi_board_verified",
                  "independent_input_count_verified", "waveform_verified",
                  "rf_path_verified", "p3_receipt"):
        if report.get(claim) is not False:
            raise AcceptanceError(f"{profile} report overclaims {claim}")
    settings = report.get("settings")
    if (not isinstance(settings, dict) or
            settings.get("serial_number") != serial_number or
            str(settings.get("build")) != str(build_id) or
            settings.get("gui_control") is not True or
            settings.get("scpi_next") is not (False if physical_ready else True) or
            settings.get("minimum_events") != MINIMUM_EVENTS):
        raise AcceptanceError(f"{profile} report identity or fixed profile mismatch")
    if physical_ready and settings.get("ready_input") != DUAL_SLOT_READY_INPUT:
        raise AcceptanceError(
            f"{profile} report used the wrong physical READY input pin")
    expected_repeat = FINITE_REPEAT if profile == "finite" else 0
    if (settings.get("repeat") != expected_repeat or
            settings.get("pause_resume") is not (profile == "pause_resume")):
        raise AcceptanceError(f"{profile} report run mode mismatch")
    if report.get("tool_sha256") != tool_sha256:
        raise AcceptanceError(f"{profile} report was produced by another validator")

    diagnostics = report.get("transport_before_cleanup", {})
    transport = diagnostics.get("READ:SEQ:LINK:TRANSPORT?", {})
    parsed = transport.get("parsed", {})
    if (not _is_no_error(transport.get("error_before")) or
            not _is_no_error(transport.get("error_after")) or
            parsed.get("snapshot_quality") not in (1, 2)):
        raise AcceptanceError(
            f"{profile} report lacks a coherent attributed transport snapshot")
    for command, record in diagnostics.items():
        if (not isinstance(record, dict) or
                not _is_no_error(record.get("error_before")) or
                not _is_no_error(record.get("error_after"))):
            raise AcceptanceError(
                f"{profile} cleanup diagnostic is not error-free: {command}")

    for name in ("stopped", "after_stop_quiet"):
        io = report.get(name, {}).get("io", {})
        if any(io.get(field) != 0 for field in ("outputs", "owned", "armed", "busy")):
            raise AcceptanceError(f"{profile} report did not release sequence IO")
    readbacks = report.get("ring_stop_readbacks")
    if not isinstance(readbacks, list) or not readbacks:
        raise AcceptanceError(f"{profile} report lacks TDMA STOP readback")
    last_readback = readbacks[-1]
    if (not isinstance(last_readback, dict) or
            cycle.ring.field(last_readback, cycle.ring.RING_ENABLED) != 0 or
            cycle.ring.field(last_readback, cycle.ring.RING_ADAPTER_STARTED) != 0):
        raise AcceptanceError(f"{profile} report did not stop the TDMA owner")

    samples = report.get("samples")
    if not isinstance(samples, list) or not samples:
        raise AcceptanceError(f"{profile} report has no runtime samples")
    active = [sample.get("link", {}) for sample in samples
              if sample.get("link", {}).get("phase") in (2, 3, 4, 5, 8)]
    if not active or any(row.get("exchange_id", 0) == 0 for row in active):
        raise AcceptanceError(f"{profile} report lacks nonzero exchange identity")
    if physical_ready:
        # A physical-READY run must not contain injected credits at all, and the
        # LINK readback must show the cabled input pin (0 means SCPI credits).
        if "ready_injection" in report:
            raise AcceptanceError(
                f"{profile} physical READY report contains SCPI READY injection evidence")
        link_configuration = report.get("link_configuration")
        if (not isinstance(link_configuration, dict) or
                link_configuration.get("input") != DUAL_SLOT_READY_INDEX):
            raise AcceptanceError(
                f"{profile} LINK readback does not show the physical READY input pin")
    else:
        ready = report.get("ready_injection")
        ready_count = 8 * FINITE_REPEAT if profile == "finite" else cycle.READY_BATCH_MAX
        if (not isinstance(ready, dict) or ready.get("response") != "1" or
                ready.get("count") != ready_count or
                ready.get("command") != f"TRIG:SEQ:INJECT READY,{ready_count}" or
                not isinstance(ready.get("identity"), list) or
                len(ready["identity"]) != 3 or any(value == 0 for value in ready["identity"])):
            raise AcceptanceError(f"{profile} report lacks attributed SCPI READY batch")

    if profile == "finite":
        if report.get("repeat_result") != f"{FINITE_REPEAT},{FINITE_REPEAT},1":
            raise AcceptanceError("finite report did not complete the fixed repeat profile")
    else:
        pause = report.get("pause_resume", {})
        if (pause.get("passed") is not True or
                pause.get("exchange_rotated") is not True):
            raise AcceptanceError("pause/resume report lacks exchange rotation proof")


def validate_dual_slot_physical_report(report: dict[str, Any], *, serial_number: str,
                                       build_id: str, tool_sha256: str) -> None:
    """Require the finite dual-slot cycle to take READY from the cabled IN2 pin."""
    validate_cycle_report(report, profile="finite", serial_number=serial_number,
                          build_id=build_id, tool_sha256=tool_sha256,
                          physical_ready=True)


def validate_ota_summary(summary: dict[str, Any], *, serial_number: str,
                         build_id: str, package: Path, root: Path | None = None) -> None:
    if summary.get("passed") is not True:
        raise AcceptanceError("single-board OTA summary is not PASS")
    matches = []
    for result in summary.get("results", []):
        board = result.get("board", {}) if isinstance(result, dict) else {}
        if board.get("serial_number") == serial_number:
            matches.append(result)
    if len(matches) != 1:
        raise AcceptanceError("OTA summary does not select exactly one requested board")
    result = matches[0]
    if result.get("passed") is not True or result.get("forced_continue") is True:
        raise AcceptanceError("single-board OTA did not pass strictly")
    commit = result.get("commit", {})
    command = commit.get("command", []) if isinstance(commit, dict) else []
    expected = None
    if isinstance(command, list) and "--expected-build" in command:
        index = command.index("--expected-build")
        expected = command[index + 1] if index + 1 < len(command) else None
    if commit.get("passed") is not True or str(expected) != str(build_id):
        raise AcceptanceError("OTA commit is not bound to the requested build")
    send = result.get("send", {})
    send_command = send.get("command", []) if isinstance(send, dict) else []
    package_paths = []
    if isinstance(send_command, list):
        for value in send_command:
            if isinstance(value, str) and value.lower().endswith(".pkg"):
                package_paths.append(Path(value).resolve())
    if send.get("passed") is not True or package.resolve() not in package_paths:
        raise AcceptanceError("OTA send is not bound to the supplied firmware package")
    metadata = summary.get("package", {})
    _require(metadata.get("sha256") == p3.sha256_file(package) and
             metadata.get("size") == package.stat().st_size and metadata.get("build_id") == build_id,
             "OTA package bytes/build lack matching digest evidence; legacy path-only summaries are insufficient for V2")
    from tools.ota_multi_update.ota_multi_update import read_package_build_id
    _require(read_package_build_id(package) == build_id, "OTA package embedded build mismatch")
    _require(send.get("returncode") == commit.get("returncode") == 0,
             "OTA child return code is not zero")
    # New VISA pipeline: independently re-read all child originals and compare
    # the inline copies before evaluating their protocol evidence again.
    if "preflight" in summary:
        from tools.visa_ota_update import visa_ota_update as visa
        evidence_root = root if root is not None else package.parent
        try:
            actual = visa.package_info(package, build_id)
            _require(all(metadata.get(k) == actual[k] for k in ("sha256", "size", "build_id", "crc32")),
                     "OTA VISA package metadata mismatch")
            for step in (send, commit):
                for name in ("stdout", "stderr"):
                    path = _validate_evidence(evidence_root, step.get(name + "_artifact"), "OTA " + name)
                    _require(path.read_bytes().decode("utf-8", errors="replace") == step.get(name),
                             "OTA inline log differs from original artifact")
            path = _validate_evidence(evidence_root, commit.get("summary_artifact"), "OTA commit child summary")
            child = _load_json(path, "OTA commit child summary")
            _require(child == commit.get("summary"), "OTA inline child summary differs from original")
            identity = argparse.Namespace(serial_number=serial_number, expected_build=build_id)
            visa.validate_send(send, identity, metadata)
            visa.validate_commit(child, identity)
            preflight = summary["preflight"]
            _require(preflight.get("passed") is True and preflight.get("serial_number") == serial_number,
                     "OTA preflight identity mismatch")
            tool_hashes = {_normalized(path): digest for path, digest in summary.get("tools", {}).items()}
            expected_tools = ("tools/visa_ota_update/visa_ota_update.py", "tools/visa_ota_send/visa_ota_send.py",
                              "tools/ota_boot_commit/ota_boot_commit.py")
            _require(all(path in tool_hashes for path in expected_tools), "OTA producer fingerprints missing")
            for relative, digest in tool_hashes.items():
                _validate_evidence(root if root is not None else ROOT,
                                   {"path": relative, "sha256": digest}, "OTA producer")
        except AcceptanceError:
            raise
        except (RuntimeError, KeyError, TypeError, ValueError) as exc:
            raise AcceptanceError(f"invalid VISA OTA evidence: {exc}") from exc
    else:
        raise AcceptanceError("legacy multi-update evidence lacks independently hashed child originals; "
                              "V2 requires the VISA artifact pipeline")


def _relative_file(root: Path, path: Path, label: str) -> Path:
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(root.resolve())
    except ValueError as exc:
        raise AcceptanceError(f"{label} must be inside the repository") from exc
    if not resolved.is_file():
        raise AcceptanceError(f"{label} is unavailable: {relative.as_posix()}")
    return relative


def _evidence(root: Path, path: Path, label: str) -> dict[str, str]:
    relative = _relative_file(root, path, label)
    return {"path": relative.as_posix(), "sha256": p3.sha256_file(root / relative)}


def _validate_evidence(root: Path, item: object, label: str) -> Path:
    if not isinstance(item, dict):
        raise AcceptanceError(f"receipt missing {label}")
    relative, expected = item.get("path"), item.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise AcceptanceError(f"receipt has invalid {label}")
    path = _relative_file(root, root / relative, label)
    full = root / path
    if p3.sha256_file(full) != expected:
        raise AcceptanceError(f"{label} evidence is missing or changed")
    return full


def _cycle_args(args: argparse.Namespace, output: Path, *, pause: bool) -> list[str]:
    values = ["--serial-number", args.serial_number, "--build", args.build,
              "--out", str(output), "--gui-control", "--minimum-events",
              str(MINIMUM_EVENTS), "--repeat", "0" if pause else str(FINITE_REPEAT),
              "--scpi-next"]
    if args.port:
        values.extend(("--port", args.port))
    elif args.visa_resource:
        values.extend(("--visa-resource", args.visa_resource))
    if pause:
        values.append("--pause-resume")
    return values


def _profile_args(args: argparse.Namespace, output: Path, profile: str) -> list[str]:
    values = ["--serial-number", args.serial_number, "--build", args.build, "--out", str(output)]
    values += ["--port", args.port] if args.port else ["--visa-resource", args.visa_resource]
    if profile == "repeat":
        values += ["--source", "MANUAL", "--repeat", str(FINITE_REPEAT), "--minimum-events", str(MINIMUM_EVENTS),
                   "--duration", str(max(30, 8 * FINITE_REPEAT / args.source_hz + 15)),
                   "--source-hz", str(args.source_hz)]
    elif profile == "position":
        duration = position_duration(args.source_hz)
        values += ["--threshold", str(POSITION_THRESHOLD), "--lifecycle", "--source-hz", str(args.source_hz),
                   "--counter-input", f"IN{POSITION_COUNTER_INPUT}",
                   "--duration", str(duration), "--gateway-timeout-ms", "10000",
                   "--position-cycle-target-ms", str(POSITION_CYCLE_TARGET_MS)]
    return values


def _calibration_args(args: argparse.Namespace, output_dir: Path) -> list[str]:
    values = ["--serial-number", args.serial_number,
              "--expected-build", args.build,
              "--words", str(CALIBRATION_WORDS),
              "--runs", str(CALIBRATION_RUNS),
              "--duration-s", str(CALIBRATION_DURATION_S),
              "--quiet-capture", "--out-dir", str(output_dir)]
    values += [args.port] if args.port else ["--visa-resource", args.visa_resource]
    return values


def _foundation_transport(args: argparse.Namespace) -> list[str]:
    return ["--port", args.port] if args.port else ["--visa-resource", args.visa_resource]


def _independent_source_args(args: argparse.Namespace, output: Path) -> list[str]:
    """Finite independent SP8T repeat advanced only by the physical IN1 source."""
    return ["--serial-number", args.serial_number, "--build", args.build,
            "--out", str(output), "--source", INDEPENDENT_SOURCE_INPUT,
            "--repeat", str(FINITE_REPEAT), "--minimum-events", str(MINIMUM_EVENTS),
            "--duration", str(INDEPENDENT_SOURCE_DURATION_S),
            "--source-hz", str(args.source_hz), *_foundation_transport(args)]


def _dual_slot_physical_args(args: argparse.Namespace, output: Path) -> list[str]:
    """Dual-slot RJ45 cycle whose READY credits come from the cabled OUT4->IN2 edge."""
    return ["--serial-number", args.serial_number, "--build", args.build,
            "--out", str(output), "--gui-control", "--ready-input", DUAL_SLOT_READY_INPUT,
            "--repeat", str(FINITE_REPEAT), "--minimum-events", str(MINIMUM_EVENTS),
            *_foundation_transport(args)]


def _turntable_external_args(args: argparse.Namespace, output: Path) -> list[str]:
    """Fixed 30-position quiet-capture turntable profile on the physical IN1 source."""
    return ["--serial-number", args.serial_number, "--build", args.build,
            "--out", str(output), "--external-input",
            "--positions", str(POSITION_EXTERNAL_POSITIONS),
            "--counter-input", INDEPENDENT_SOURCE_INPUT,
            "--threshold", str(POSITION_THRESHOLD),
            "--quiet-capture", "--timing-evidence", "--schedule-evidence",
            "--gateway-timeout-ms", "10000",
            "--position-cycle-target-ms", str(POSITION_CYCLE_TARGET_MS),
            "--source-hz", str(args.source_hz),
            "--duration", str(external_position_duration(args.source_hz)),
            *_foundation_transport(args)]


def _standalone_sp8t_args(args: argparse.Namespace, output: Path) -> list[str]:
    """Standalone SP8T pad check that starts no plan and owns no sequence resource."""
    return ["--mode", "standalone-sp8t", "--serial-number", args.serial_number,
            "--build", args.build, "--out", str(output),
            *_foundation_transport(args)]


def _host_command(output_dir: Path, profile: str = PROFILE_THREE_MODE) -> list[str]:
    tests = PROFILE_HOST_TESTS[profile]
    return [sys.executable, "-m", "pytest", *tests, "-p", "no:cacheprovider",
            "--basetemp", str(output_dir / "pytest"), "--junitxml", str(output_dir / "host.xml")]


def _foundation_vdc_command(root: Path, output_dir: Path) -> list[str]:
    executable = shutil.which("pwsh") or shutil.which("powershell")
    if not executable:
        raise AcceptanceError("foundation VDC host gate requires PowerShell")
    try:
        build_dir = (output_dir / "vdc-domain").resolve().relative_to(root.resolve())
    except ValueError as exc:
        raise AcceptanceError("foundation VDC build directory must be inside the repository") from exc
    return [executable, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            "tools/tests/run_vdc_domain_tests.ps1", "-BuildDir", build_dir.as_posix()]


def _run_host_regression(root: Path, output_dir: Path,
                         profile: str = PROFILE_THREE_MODE) -> Path:
    command = _host_command(output_dir, profile)
    process = subprocess.run(command, cwd=root, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", check=False)
    log = output_dir / "host.log"
    log.write_text(process.stdout + "\n--- stderr ---\n" + process.stderr, encoding="utf-8")
    foundation = None
    if profile == PROFILE_RESERVATION_FOUNDATION:
        vdc_command = _foundation_vdc_command(root, output_dir)
        vdc_process = subprocess.run(
            vdc_command, cwd=root, capture_output=True, text=True,
            encoding="utf-8", errors="replace", check=False)
        vdc_log = output_dir / "vdc-domain-host.log"
        vdc_log.write_text(
            vdc_process.stdout + "\n--- stderr ---\n" + vdc_process.stderr,
            encoding="utf-8")
        foundation = {
            "command": vdc_command,
            "returncode": vdc_process.returncode,
            "log": _evidence(root, vdc_log, "VDC domain host log"),
        }
    summary = output_dir / "host.json"
    report = {"profile": profile, "command": command,
              "returncode": process.returncode,
              "log": _evidence(root, log, "host log"),
              "junit": _evidence(root, output_dir / "host.xml", "host JUnit")}
    if foundation is not None:
        report["vdc_domain"] = foundation
    summary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    validate_host_regression(root, summary, profile)
    return summary


def validate_host_regression(root: Path, path: Path,
                             profile: str = PROFILE_THREE_MODE) -> None:
    report = _load_json(path, "host regression")
    command = report.get("command")
    expected = _host_command(path.parent, profile)
    # The interpreter path may differ on a computer checking a copied receipt;
    # the fixed pytest suite and arguments may not.
    _require(report.get("profile") == profile and
             isinstance(command, list) and len(command) == len(expected) and
             isinstance(command[0], str) and bool(command[0]) and command[1:] == expected[1:] and
             report.get("returncode") == 0, "host regression invocation/return code mismatch")
    _validate_evidence(root, report.get("log"), "host log")
    junit = _validate_evidence(root, report.get("junit"), "host JUnit")
    try:
        xml = ET.parse(junit).getroot()
        suites = list(xml.iter("testsuite"))
        cases = list(xml.iter("testcase"))
        _require(bool(cases) and bool(suites) and
                 sum(int(s.attrib["tests"]) for s in suites) == len(cases) and
                 all(int(s.attrib.get(k, "-1")) == 0 for s in suites for k in ("failures", "errors", "skipped")) and
                 not any(list(xml.iter(tag)) for tag in ("failure", "error", "skipped")),
                 "host JUnit contains failed, skipped, empty or inconsistent tests")
        for test in PROFILE_HOST_TESTS[profile]:
            module = test.removesuffix(".py").replace("/", ".")
            _require(any(c.attrib.get("classname", "") == module or
                         c.attrib.get("classname", "").startswith(module + ".") for c in cases),
                     f"host JUnit missing fixed test module: {test}")
    except (ET.ParseError, OSError, ValueError, KeyError) as exc:
        raise AcceptanceError(f"invalid host JUnit evidence: {exc}") from exc
    foundation = report.get("vdc_domain")
    if profile == PROFILE_RESERVATION_FOUNDATION:
        _require(isinstance(foundation, dict),
                 "foundation receipt lacks the VDC domain host gate")
        vdc_command = foundation.get("command")
        expected_tail = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            "tools/tests/run_vdc_domain_tests.ps1", "-BuildDir",
            (path.parent / "vdc-domain").resolve().relative_to(root.resolve()).as_posix()]
        _require(isinstance(vdc_command, list) and len(vdc_command) == 8 and
                 isinstance(vdc_command[0], str) and bool(vdc_command[0]) and
                 vdc_command[1:] == expected_tail and foundation.get("returncode") == 0,
                 "foundation VDC host invocation/return code mismatch")
        vdc_log = _validate_evidence(
            root, foundation.get("log"), "VDC domain host log")
        try:
            output = vdc_log.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            raise AcceptanceError(f"invalid VDC domain host log: {exc}") from exc
        _require("vdc_domain host unit tests passed" in output,
                 "foundation VDC host gate did not execute the unit tests")
    else:
        _require(foundation is None,
                 "three-mode host report contains a foundation-only gate")


def validate_calibration_report(report: dict[str, Any], *, serial_number: str,
                                build_id: str, tool_sha256: str) -> None:
    _require(report.get("schema") == calibration.SCHEMA and
             report.get("scope") == "single_board_physical_rj45_reference_loopback" and
             report.get("tool_sha256") == tool_sha256 and
             report.get("passed") is True and report.get("failure") is None and
             report.get("cleanup_failures") == [],
             "foundation calibration report is not an intact PASS")
    identity, settings = report.get("identity"), report.get("settings")
    _require(isinstance(identity, dict) and
             identity.get("serial_number") == serial_number and
             str(identity.get("build")) == str(build_id) and
             isinstance(settings, dict) and
             settings.get("serial_number") == serial_number and
             str(settings.get("expected_build")) == str(build_id) and
             settings.get("words") == CALIBRATION_WORDS and
             settings.get("runs") == CALIBRATION_RUNS and
             settings.get("duration_s") == CALIBRATION_DURATION_S and
             settings.get("quiet_capture") is True,
             "foundation calibration identity or fixed quiet profile mismatch")
    _require(report.get("runs_requested") == CALIBRATION_RUNS and
             report.get("runs_completed") == CALIBRATION_RUNS and
             report.get("runs_raw_passed") == CALIBRATION_RUNS and
             report.get("raw_measurement_passed") is True,
             "foundation calibration raw measurement set is incomplete")
    _require(report.get("firmware_calibration_passed") is False and
             report.get("active_eligible") is False and
             report.get("formal_calibration_claimed") is False and
             set(report.get("limitations", [])) == set(calibration.LIMITATIONS) and
             "not formal calibration" in report.get("evidence_boundary", ""),
             "foundation diagnostic report overclaims formal calibration")
    guard = report.get("load_guard")
    _require(isinstance(guard, dict) and guard.get("unchanged") is True,
             "foundation calibration did not preserve the TDMA load guard")
    before, after = guard.get("before"), guard.get("after")
    _require(isinstance(before, dict) and isinstance(after, dict) and
             before.get("enabled_mask") == after.get("enabled_mask") and
             before.get("quarantined_mask") == after.get("quarantined_mask"),
             "foundation calibration changed the load or quarantine mask")
    _require(_is_no_error(report.get("final_error")),
             "foundation calibration SCPI error queue is not clean")
    runs = report.get("run_results")
    _require(isinstance(runs, list) and len(runs) == CALIBRATION_RUNS,
             "foundation calibration run evidence is missing")
    records, windows = report.get("records"), report.get("quiet_windows")
    _require(isinstance(records, list) and records and
             all(isinstance(record, dict) and
                 isinstance(record.get("host_monotonic_start_s"), (int, float)) and
                 isinstance(record.get("host_monotonic_end_s"), (int, float)) and
                 record["host_monotonic_start_s"] <= record["host_monotonic_end_s"]
                 for record in records) and
             isinstance(windows, list) and len(windows) == CALIBRATION_RUNS,
             "foundation calibration lacks host monotonic command/window evidence")
    terminal_records = {
        record.get("run"): record for record in records
        if record.get("command") == "READ:CAL:LOOP?" and
        record.get("phase") == "terminal"
    }
    epochs: list[int] = []
    for expected_run, row in enumerate(runs, 1):
        _require(isinstance(row, dict) and row.get("run") == expected_run and
                 row.get("raw_measurement_passed") is True and
                 row.get("firmware_calibration_passed") is False and
                 row.get("active_eligible") is False and
                 row.get("epoch_unique") is True,
                 "foundation calibration run classification mismatch")
        snapshot = row.get("snapshot")
        _require(isinstance(snapshot, dict),
                 "foundation calibration snapshot is missing")
        raw = snapshot.get("raw_fields")
        _require(isinstance(raw, list) and len(raw) == len(calibration.SNAPSHOT_FIELDS) and
                 all(snapshot.get(name) == raw[index]
                     for index, name in enumerate(calibration.SNAPSHOT_FIELDS)),
                 "foundation calibration raw timestamp/flag fields were not retained")
        terminal = terminal_records.get(expected_run)
        try:
            retained_response = calibration.parse_ints(terminal.get("response", ""))
        except (AttributeError, TypeError, ValueError):
            retained_response = None
        _require(retained_response == raw,
                 "foundation calibration original terminal response was not retained")
        epoch = snapshot.get("epoch")
        _require(isinstance(epoch, int) and epoch > 0 and
                 epoch != row.get("baseline_epoch") and epoch not in epochs,
                 "foundation calibration epoch did not advance uniquely")
        epochs.append(epoch)
        flags = snapshot.get("flags")
        _require(isinstance(flags, int) and
                 (flags & calibration.PHYSICAL_FLAGS) == calibration.PHYSICAL_FLAGS and
                 (flags & calibration.DIAGNOSTIC_ONLY_FLAG) == 0 and
                 (int(snapshot.get("edge_mask", 0)) & 0x0f) == 0x0f,
                 "foundation calibration lacks physical PIO/DMA/SYNC edge evidence")
        t1, t2, t3, t4 = (snapshot.get(name) for name in
                           ("t1_clk_tx", "t2_clk_rx", "t3_data_tx", "t4_data_rx"))
        _require(all(isinstance(value, int) for value in (t1, t2, t3, t4)) and
                 0 < t1 <= t2 <= t3 <= t4 and
                 snapshot.get("clk_path_ns") == t2 - t1 > 0 and
                 snapshot.get("data_path_ns") == t4 - t3 > 0 and
                 snapshot.get("computed_residence_ns") == t3 - t2 > 0 and
                 snapshot.get("computed_raw_path_sum_ns") ==
                 (t2 - t1) + (t4 - t3) > 0,
                 "foundation calibration path timestamps/deltas are invalid")
    for expected_run, window in enumerate(windows, 1):
        _require(isinstance(window, dict) and window.get("run") == expected_run,
                 "foundation calibration quiet-window run identity mismatch")
        start_s, end_s = (window.get(name) for name in
                          ("host_monotonic_start_s", "host_monotonic_end_s"))
        before, after_count = (window.get(name) for name in
                               ("records_before", "records_after"))
        _require(isinstance(start_s, (int, float)) and
                 isinstance(end_s, (int, float)) and end_s >= start_s and
                 math.isclose(window.get("elapsed_s", -1), end_s - start_s,
                              rel_tol=0.0, abs_tol=1e-9) and
                 end_s - start_s >= CALIBRATION_DURATION_S and
                 isinstance(before, int) and before == after_count and
                 0 <= before <= len(records) and
                 not any(record["host_monotonic_start_s"] < end_s and
                         record["host_monotonic_end_s"] > start_s
                         for record in records),
                 "foundation calibration quiet window contains host SCPI activity")


def _validate_new_profiles(root: Path, receipt: dict, serial_number: str,
                           build_id: str, profile: str) -> None:
    hashes = {key: p3.sha256_file(root / path) for key, path in VALIDATOR_PATHS.items()}
    _require(receipt.get("validator_hashes") == hashes, "single-board validators changed after the hardware run")
    source_hz = receipt.get("source_hz")
    position_duration(source_hz)
    try:
        for name, validate in (("repeat", validate_repeat_report), ("start", validate_start_report),
                               ("position", validate_position_report)):
            path = _validate_evidence(root, receipt.get(name + "_report"), name + " report")
            kwargs = dict(serial_number=serial_number, build_id=build_id, tool_sha256=hashes[name])
            if name != "start":
                kwargs["source_hz"] = source_hz
            if name == "position":
                kwargs["gui_sha256"] = hashes["gui"]
            validate(_load_json(path, name + " report"), **kwargs)
        path = _validate_evidence(root, receipt.get("host_report"), "host regression")
        validate_host_regression(root, path, profile)
        calibration_item = receipt.get("calibration_report")
        foundation_items = {key: receipt.get(key) for key in FOUNDATION_STAGE_REPORT_KEYS}
        if profile == PROFILE_RESERVATION_FOUNDATION:
            calibration_path = _validate_evidence(
                root, calibration_item, "calibration report")
            validate_calibration_report(
                _load_json(calibration_path, "calibration report"),
                serial_number=serial_number, build_id=build_id,
                tool_sha256=hashes["calibration"])
            _validate_foundation_stages(
                root, foundation_items, serial_number=serial_number,
                build_id=build_id, hashes=hashes, source_hz=source_hz)
        else:
            _require(calibration_item is None,
                     "three-mode receipt contains foundation-only calibration evidence")
            for key, item in foundation_items.items():
                _require(item is None,
                         f"three-mode receipt contains foundation-only stage evidence: {key}")
    except AcceptanceError:
        raise
    except (KeyError, IndexError, TypeError, ValueError, AttributeError, RuntimeError) as exc:
        raise AcceptanceError(f"invalid single-board evidence: {exc}") from exc


def _foundation_stage_validators(hashes: dict, source_hz: float) -> tuple:
    """(receipt key, label, validator, extra keyword arguments) for the new stages."""
    return (
        ("independent_source_report", "independent source report",
         validate_independent_source_report,
         dict(tool_sha256=hashes["repeat"], source_hz=source_hz)),
        ("dual_slot_physical_report", "dual-slot physical report",
         validate_dual_slot_physical_report,
         dict(tool_sha256=hashes["cycle"])),
        ("turntable_external_report", "turntable external report",
         validate_turntable_external_report,
         dict(tool_sha256=hashes["position"], source_hz=source_hz)),
        ("standalone_sp8t_report", "standalone SP8T report",
         validate_standalone_sp8t_report,
         dict(tool_sha256=hashes["feedback"])),
    )


def _validate_foundation_stages(root: Path, items: dict, *, serial_number: str,
                                build_id: str, hashes: dict, source_hz: float) -> None:
    """Re-validate every foundation-only stage report bound into the receipt.

    These stages exist to prove the fixed bench wiring: the physical IN1 source
    drives the independent SP8T repeat and the external turntable profile, OUT4
    cabled to IN2 carries READY, and the standalone SP8T check owns no sequence
    resource.  A receipt that swapped any report for an injected/simulated run
    is rejected here on the run, check-staged and audit paths alike.
    """
    for key, label, validate, kwargs in _foundation_stage_validators(hashes, source_hz):
        path = _validate_evidence(root, items.get(key), label)
        validate(_load_json(path, label), serial_number=serial_number,
                 build_id=build_id, **kwargs)


def run_acceptance(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    position_duration(args.source_hz)
    changed = validate_scope(changed_staged_sources(root))
    profile = acceptance_profile(changed)
    staged_fingerprint = staged_source_fingerprint(root)
    if working_source_fingerprint(root) != staged_fingerprint:
        raise AcceptanceError(
            "working source differs from the staged source; stage or remove every source change")

    package = _relative_file(root, root / args.package, "firmware package")
    ota_path = _relative_file(root, root / args.ota_summary, "OTA summary")
    validate_ota_summary(_load_json(root / ota_path, "OTA summary"),
                         serial_number=args.serial_number, build_id=args.build,
                         package=root / package, root=root)

    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    output_dir = (root / args.out_dir) if args.out_dir else (
        root / "out" / "HardwareAcceptance" /
        datetime.now(timezone.utc).strftime("%Y%m%d") /
        f"sequence-single-board-{stamp}")
    output_dir.mkdir(parents=True, exist_ok=False)
    host_path = _run_host_regression(root, output_dir, profile)
    calibration_evidence = {}
    if profile == PROFILE_RESERVATION_FOUNDATION:
        calibration_dir = output_dir / "calibration-loopback"
        if calibration.main(_calibration_args(args, calibration_dir)) != 0:
            raise AcceptanceError(
                f"foundation physical calibration loopback failed: {calibration_dir}")
        calibration_evidence["calibration_report"] = _evidence(
            root, calibration_dir / "summary.json", "calibration report")
    finite_path = output_dir / "finite.json"
    pause_path = output_dir / "pause-resume.json"
    if cycle.main(_cycle_args(args, finite_path, pause=False)) != 0:
        raise AcceptanceError(f"finite single-board profile failed: {finite_path}")
    if cycle.main(_cycle_args(args, pause_path, pause=True)) != 0:
        raise AcceptanceError(f"pause/resume single-board profile failed: {pause_path}")

    tool_sha256 = p3.sha256_file(root / "tools/hardware_acceptance/sequence_tdma_cycle_validate.py")
    finite = _load_json(finite_path, "finite report")
    pause = _load_json(pause_path, "pause/resume report")
    validate_cycle_report(finite, profile="finite", serial_number=args.serial_number,
                          build_id=args.build, tool_sha256=tool_sha256)
    validate_cycle_report(pause, profile="pause_resume", serial_number=args.serial_number,
                          build_id=args.build, tool_sha256=tool_sha256)
    reports = {}
    for name, module in (("repeat", repeat), ("start", start), ("position", position)):
        path = output_dir / (name + ".json")
        if module.main(_profile_args(args, path, name)) != 0:
            raise AcceptanceError(f"{name} single-board profile failed: {path}")
        reports[name + "_report"] = _evidence(root, path, name + " report")
    foundation_reports = {}
    if profile == PROFILE_RESERVATION_FOUNDATION:
        # Fixed bench wiring only: the physical IN1 source, OUT4 cabled to IN2 for
        # READY, and the RJ45 loopback cable.  Each stage below rejects any report
        # that substituted a software-injected path for a physical input.
        foundation_stages = (
            ("independent_source_report", "independent-source.json", repeat,
             _independent_source_args, "foundation physical IN1 independent repeat"),
            ("dual_slot_physical_report", "dual-slot-physical.json", cycle,
             _dual_slot_physical_args, "foundation dual-slot physical READY cycle"),
            ("turntable_external_report", "turntable-external.json", position,
             _turntable_external_args, "foundation external IN1 turntable profile"),
            ("standalone_sp8t_report", "standalone-sp8t.json", feedback,
             _standalone_sp8t_args, "foundation standalone SP8T bench check"),
        )
        for key, name, module, build_args, label in foundation_stages:
            path = output_dir / name
            if module.main(build_args(args, path)) != 0:
                raise AcceptanceError(f"{label} failed: {path}")
            foundation_reports[key] = _evidence(root, path, key.replace("_", " "))
    if (working_source_fingerprint(root) != staged_fingerprint or
            staged_source_fingerprint(root) != staged_fingerprint):
        raise AcceptanceError("source changed while single-board acceptance was running")

    receipt = {
        "schema": RECEIPT_SCHEMA,
        "passed": True,
        "acceptance_profile": profile,
        "acceptance_scope": PROFILE_SCOPES[profile],
        "limitations": sorted(PROFILE_LIMITATIONS[profile]),
        "created_at": datetime.now(timezone.utc).isoformat(),
        "serial_number": args.serial_number,
        "build_id": args.build,
        "source_tree_sha256": staged_fingerprint[0],
        "source_file_count": staged_fingerprint[1],
        "changed_sources": changed,
        "validator_sha256": tool_sha256,
        "validator_hashes": {key: p3.sha256_file(root / path) for key, path in VALIDATOR_PATHS.items()},
        "source_hz": args.source_hz,
        "host_report": _evidence(root, host_path, "host regression"),
        **calibration_evidence,
        **reports,
        **foundation_reports,
        "firmware_package": _evidence(root, root / package, "firmware package"),
        "ota_summary": _evidence(root, root / ota_path, "OTA summary"),
        "finite_report": _evidence(root, finite_path, "finite report"),
        "pause_resume_report": _evidence(root, pause_path, "pause/resume report"),
    }
    _validate_new_profiles(root, receipt, args.serial_number, args.build, profile)
    receipt_path = root / args.receipt
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(json.dumps(receipt, indent=2, ensure_ascii=True) + "\n",
                            encoding="utf-8")
    print(f"PASS sequence single-board acceptance receipt={args.receipt}")


def check_staged(root: Path, receipt_path: Path) -> None:
    changed = validate_scope(changed_staged_sources(root))
    profile = acceptance_profile(changed)
    staged_fingerprint = staged_source_fingerprint(root)
    if working_source_fingerprint(root) != staged_fingerprint:
        raise AcceptanceError(
            "working source differs from the staged commit; rerun single-board acceptance")
    receipt = p3.read_index_json(root, receipt_path)
    if (receipt.get("schema") != RECEIPT_SCHEMA or
            receipt.get("passed") is not True or
            receipt.get("acceptance_profile") != profile or
            receipt.get("acceptance_scope") != PROFILE_SCOPES[profile]):
        raise AcceptanceError("staged sequence single-board receipt is not PASS")
    if (receipt.get("source_tree_sha256") != staged_fingerprint[0] or
            receipt.get("source_file_count") != staged_fingerprint[1] or
            receipt.get("changed_sources") != changed):
        raise AcceptanceError(
            "staged source has no matching sequence single-board acceptance")
    expected_limitations = PROFILE_LIMITATIONS[profile]
    if set(receipt.get("limitations", [])) != expected_limitations:
        raise AcceptanceError("single-board receipt has invalid claim limitations")

    tool_path = root / "tools/hardware_acceptance/sequence_tdma_cycle_validate.py"
    tool_sha256 = p3.sha256_file(tool_path)
    if receipt.get("validator_sha256") != tool_sha256:
        raise AcceptanceError("single-board validator changed after the hardware run")
    package = _validate_evidence(root, receipt.get("firmware_package"), "firmware package")
    ota_path = _validate_evidence(root, receipt.get("ota_summary"), "OTA summary")
    finite_path = _validate_evidence(root, receipt.get("finite_report"), "finite report")
    pause_path = _validate_evidence(
        root, receipt.get("pause_resume_report"), "pause/resume report")
    serial_number, build_id = receipt.get("serial_number"), receipt.get("build_id")
    if not isinstance(serial_number, str) or not isinstance(build_id, str):
        raise AcceptanceError("single-board receipt has invalid board identity")
    validate_ota_summary(_load_json(ota_path, "OTA summary"),
                         serial_number=serial_number, build_id=build_id,
                         package=package, root=root)
    validate_cycle_report(_load_json(finite_path, "finite report"), profile="finite",
                          serial_number=serial_number, build_id=build_id,
                          tool_sha256=tool_sha256)
    validate_cycle_report(_load_json(pause_path, "pause/resume report"),
                          profile="pause_resume", serial_number=serial_number,
                          build_id=build_id, tool_sha256=tool_sha256)
    _validate_new_profiles(root, receipt, serial_number, build_id, profile)
    print(f"OK   sequence single-board acceptance: profile={profile} "
          f"build={build_id} board={serial_number}")


def audit_reports(args: argparse.Namespace) -> None:
    """Read existing originals for schema/semantic checks; never issue a receipt."""
    root = args.root.resolve()
    position_duration(args.source_hz)
    hashes = {key: p3.sha256_file(root / path) for key, path in VALIDATOR_PATHS.items()}
    identity = dict(serial_number=args.serial_number, build_id=args.build)
    for profile, path in (("finite", args.finite_report), ("pause_resume", args.pause_resume_report)):
        validate_cycle_report(_load_json(root / path, profile), profile=profile,
                              tool_sha256=hashes["cycle"], **identity)
    validate_repeat_report(_load_json(root / args.repeat_report, "repeat"),
                           tool_sha256=hashes["repeat"], source_hz=args.source_hz, **identity)
    validate_start_report(_load_json(root / args.start_report, "start"), tool_sha256=hashes["start"], **identity)
    validate_position_report(_load_json(root / args.position_report, "position"),
        tool_sha256=hashes["position"], gui_sha256=hashes["gui"], source_hz=args.source_hz, **identity)
    foundation = dict(independent_source_report=args.independent_source_report,
                      dual_slot_physical_report=args.dual_slot_physical_report,
                      turntable_external_report=args.turntable_external_report,
                      standalone_sp8t_report=args.standalone_sp8t_report)
    for key, label, validate, kwargs in _foundation_stage_validators(hashes, args.source_hz):
        if foundation[key] is not None:
            # Foundation-only stages are audited with the same fail-closed
            # validators the receipt path uses; a three-mode run leaves them out.
            validate(_load_json(root / foundation[key], label), **identity, **kwargs)
    validate_ota_summary(_load_json(root / args.ota_summary, "OTA"), package=root / args.package, root=root, **identity)
    print("OK   existing report audit only; no receipt created and no staged acceptance claimed")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    run = subparsers.add_parser("run", help="execute the fixed single-board profiles")
    run.add_argument("--root", type=Path, default=ROOT)
    run.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    run.add_argument("--serial-number", required=True)
    run.add_argument("--build", required=True)
    run.add_argument("--package", type=Path, required=True)
    run.add_argument("--ota-summary", type=Path, required=True)
    run.add_argument("--out-dir", type=Path)
    run.add_argument("--source-hz", type=float, required=True,
                     help="user-declared IN1 frequency; sets functional POSITION timeout, not a speed claim")
    transport = run.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")

    check = subparsers.add_parser(
        "check-staged", help="validate the indexed receipt without hardware access")
    check.add_argument("--root", type=Path, default=ROOT)
    check.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    covers = subparsers.add_parser(
        "covers-staged", help="report whether every staged source is in the allowlist")
    covers.add_argument("--root", type=Path, default=ROOT)
    audit = subparsers.add_parser("audit-reports", help="read-only semantic audit; never creates an acceptance receipt")
    audit.add_argument("--root", type=Path, default=ROOT)
    audit.add_argument("--serial-number", required=True)
    audit.add_argument("--build", required=True)
    audit.add_argument("--source-hz", type=float, required=True)
    for name in ("finite-report", "pause-resume-report", "repeat-report", "start-report",
                 "position-report", "ota-summary", "package"):
        audit.add_argument("--" + name, type=Path, required=True)
    for name in FOUNDATION_STAGE_REPORT_KEYS:
        audit.add_argument("--" + name.replace("_", "-"), type=Path, default=None,
                           help="foundation-only stage report; validated when supplied")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "run":
            run_acceptance(args)
        elif args.command == "check-staged":
            check_staged(args.root.resolve(), args.receipt)
        elif args.command == "audit-reports":
            audit_reports(args)
        else:
            changed = changed_staged_sources(args.root.resolve())
            validate_scope(changed)
            profile = acceptance_profile(changed)
            print(f"OK   staged source is covered by single-board profile {profile}")
    except (AcceptanceError, KeyError, IndexError, TypeError, ValueError, AttributeError, RuntimeError, OSError) as exc:
        print(f"FAIL sequence single-board acceptance: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
