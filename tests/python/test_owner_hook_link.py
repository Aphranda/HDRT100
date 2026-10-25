"""Owner-hook linkage gate: the linked image must keep every split-unit hook.

The 2026-10-25 regression made ``vdc_dpll_feedback.c`` compile the phase-commit
call out after the fragment split. ``--gc-sections`` then dropped the owner
receipt entirely, so P3 step 30 saw ``applied=0`` on all followers. Step 3 now
checks the linked symbol table before any board is touched.
"""
import pytest

from tools.hardware_acceptance import p3_hardware_acceptance as gate


def test_nm_parser_separates_defined_from_undefined():
    text = "\n".join([
        "100e126c T model_feedback_end_core1",
        "100e3090 t priority_phase_apply_core1.part.0",
        "         U phase_model_committed_core1",
        "         w __aeabi_unwind_cpp_pr0",
        "100e4000 W priority_trace_phase_core1",
    ])
    symbols = gate.nm_symbol_types(text)
    assert symbols["model_feedback_end_core1"] == "T"
    assert symbols["priority_phase_apply_core1.part.0"] == "t"
    assert symbols["phase_model_committed_core1"] == "U"
    assert symbols["__aeabi_unwind_cpp_pr0"] == "U"
    assert symbols["priority_trace_phase_core1"] == "W"


def test_all_defined_owner_hooks_pass():
    symbols = {name: "t" for name in gate.OWNER_HOOK_SYMBOLS}
    assert gate.missing_owner_hook_symbols(symbols) == []


def test_dead_stripped_hook_is_reported_by_name():
    symbols = {name: "t" for name in gate.OWNER_HOOK_SYMBOLS}
    symbols["phase_model_committed_core1"] = "U"
    assert gate.missing_owner_hook_symbols(symbols) == ["phase_model_committed_core1"]


def test_absent_hook_symbol_is_treated_as_lost():
    symbols = {name: "t" for name in gate.OWNER_HOOK_SYMBOLS
               if name != "priority_trace_origin_core1"}
    assert gate.missing_owner_hook_symbols(symbols) == ["priority_trace_origin_core1"]


def test_missing_toolchain_fails_closed(monkeypatch, tmp_path):
    monkeypatch.setattr(gate, "toolchain_nm", lambda: None)
    elf = tmp_path / "DHRT100.elf"
    elf.write_bytes(b"\x7fELF")
    with pytest.raises(gate.AcceptanceError, match="arm-none-eabi-nm"):
        gate.verify_owner_hook_symbols(elf)


def test_nm_failure_fails_closed(monkeypatch, tmp_path):
    monkeypatch.setattr(gate, "toolchain_nm", lambda: "nm")
    monkeypatch.setattr(gate.subprocess, "run", lambda *a, **k: type(
        "R", (), {"returncode": 1, "stdout": "", "stderr": "not an object"})())
    elf = tmp_path / "DHRT100.elf"
    elf.write_bytes(b"\x7fELF")
    with pytest.raises(gate.AcceptanceError, match="nm failed"):
        gate.verify_owner_hook_symbols(elf)
