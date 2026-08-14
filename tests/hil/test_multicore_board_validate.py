from __future__ import annotations

import importlib

import pytest


pytestmark = pytest.mark.hil


def _import_validator():
    try:
        return importlib.import_module("tools.multicore_board_validate.multicore_board_validate")
    except SystemExit as exc:
        pytest.skip(str(exc))


def test_multicore_board_read_only_smoke(hil_config, hil_serial) -> None:
    validator = _import_validator()

    checks = (
        ("identity", validator.test_identity),
        ("core_heartbeat", validator.test_core_heartbeat),
        ("loop_status", validator.test_loop_status),
        ("vdc_status", validator.test_vdc_status),
        ("dpll_status", validator.test_dpll_status),
        ("refmem_slot_claim_gate", validator.test_refmem_slot_claim_gate),
        ("error_queue", validator.test_error_queue),
    )

    failures: list[str] = []
    for name, check in checks:
        passed, detail = check(hil_serial, hil_config.timeout)
        if not passed:
            failures.append(f"{name}: {detail}")

    assert not failures
