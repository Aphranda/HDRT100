import json
from pathlib import Path

import pytest

from tools.hardware_acceptance.p3_hardware_acceptance import (
    AcceptanceError,
    is_acceptance_source,
    parse_schedule,
    validate_ota,
    validate_p3,
    validate_schedule_isolation,
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


def test_receipt_uses_frozen_schema() -> None:
    receipt = json.loads((ROOT / "config" / "hardware_acceptance" /
                          "p3_acceptance_receipt.json").read_text(
                              encoding="utf-8"))
    assert receipt["schema"] == "HAOFV_P3_HARDWARE_ACCEPTANCE_RECEIPT_V1"
    assert isinstance(receipt["passed"], bool)
