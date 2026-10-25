"""Guards for the owner hooks the manager used to inject around .inc fragments.

2026-10-25 regression (P3 run `hawf-171855`): splitting the DPLL fragments into
real translation units moved ``model_feedback_end_core1()`` into
``vdc_dpll_feedback.c``, but the phase-commit hook stayed defined in
``vdc_dpll_manager.c``.  The unit's ``#ifdef`` therefore compiled the owner
receipt out of the firmware, ``PHASE:STATus?`` reported ``applied=0`` on every
follower, and step 30 rejected all three endpoints as "invalid".

These tests pin each hook to the unit that uses it, so the same silent drop
cannot come back after the next split.
"""
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/vdc_dpll_manager/src"
HEADERS = [*SRC.glob("*.h"), *(ROOT / "components/vdc_dpll_manager/inc").glob("*.h")]
INCLUDE_DIRS = [
    SRC,
    *(sorted((ROOT / "components").glob("*/inc"))),
    *(sorted((ROOT / "components").glob("*/src"))),
    *(sorted((ROOT / "boards").glob("*/inc"))),
    ROOT / "platform/inc",
    ROOT / "osal/inc",
    ROOT / "config",
]

HOOK_NAME = r"VDC_[A-Z0-9_]+_HOOK"
HOOK_CALL = re.compile(rf"\b(?P<name>{HOOK_NAME})\s*\(")
HOOK_DEFINE = re.compile(
    rf"#\s*define\s+(?P<name>{HOOK_NAME})\((?P<args>[^)]*)\)\s*(?P<body>[^\n]*)")
HOOK_CONDITION = re.compile(
    rf"#\s*(?P<kind>ifdef|ifndef|if\s+defined\s*\(?)\s*(?P<name>{HOOK_NAME})")
INCLUDE = re.compile(r'#\s*include\s+"(?P<path>[^"]+)"')

# unit, hook, owner symbol the hook must expand to
BINDINGS = [
    ("vdc_dpll_feedback.c", "VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK",
     "phase_model_committed_core1"),
    ("vdc_priority_follow.c", "VDC_PRIORITY_TRACE_PHASE_HOOK",
     "priority_trace_phase_core1"),
    ("vdc_priority_follow.c", "VDC_PRIORITY_TRACE_DECISION_HOOK",
     "priority_trace_decision_core1"),
    ("vdc_priority_tx.c", "VDC_PRIORITY_TRACE_ORIGIN_HOOK",
     "priority_trace_origin_core1"),
]


def units():
    return sorted(SRC.glob("*.c"))


def header_definitions(unit):
    """Hook names defined by project headers the unit includes."""
    text = unit.read_text(encoding="utf-8")
    names = set()
    for include in INCLUDE.finditer(text):
        for directory in (unit.parent, *INCLUDE_DIRS):
            candidate = directory / include.group("path")
            if candidate.is_file():
                if candidate.suffix == ".h":
                    names |= {m.group("name") for m in
                              HOOK_DEFINE.finditer(candidate.read_text(encoding="utf-8"))}
                break
    return names


@pytest.mark.parametrize("unit", units(), ids=lambda path: path.name)
def test_every_hook_a_unit_uses_is_bound_in_that_unit(unit):
    text = unit.read_text(encoding="utf-8")
    used = {m.group("name") for m in HOOK_CALL.finditer(text)}
    used |= {m.group("name") for m in HOOK_CONDITION.finditer(text)}
    available = {m.group("name") for m in HOOK_DEFINE.finditer(text)}
    available |= header_definitions(unit)
    missing = sorted(used - available)
    assert not missing, (
        f"{unit.name} uses {missing} without binding it: a split unit must "
        "define its own hook, otherwise the #ifdef fallback silently drops "
        "the call (P3 step 30: applied=0)")


@pytest.mark.parametrize("unit_name,hook,target", BINDINGS, ids=[row[1] for row in BINDINGS])
def test_hook_binding_targets_a_declared_owner(unit_name, hook, target):
    text = (SRC / unit_name).read_text(encoding="utf-8")
    bindings = {}
    for match in HOOK_DEFINE.finditer(text):
        bindings.setdefault(match.group("name"), []).append(match.group("body"))
    assert hook in bindings, f"{unit_name} must bind {hook}"
    assert any(re.search(rf"\b{re.escape(target)}\s*\(", body)
               for body in bindings[hook]), (
        f"{hook} must expand to {target}, got: {bindings[hook]}")
    headers = "\n".join(path.read_text(encoding="utf-8") for path in HEADERS)
    assert re.search(rf"\b{re.escape(target)}\s*\(", headers), (
        f"{target} is not declared in a DPLL manager header; host harnesses "
        "and the split units would not share one owner symbol")


@pytest.mark.parametrize("unit", units(), ids=lambda path: path.name)
def test_hook_calls_are_never_conditionally_compiled(unit):
    text = unit.read_text(encoding="utf-8")
    guarded = [m.group(0).strip() for m in HOOK_CONDITION.finditer(text)
               if m.group("kind") != "ifndef"]
    assert not guarded, (
        f"{unit.name} guards a hook with {guarded}: `#ifdef` compiles the call "
        "out whenever the macro is not visible, which is exactly the "
        "hawf-171855 regression; bind the hook unconditionally instead")


def test_phase_receipt_call_is_unconditional():
    text = (SRC / "vdc_dpll_feedback.c").read_text(encoding="utf-8")
    assert "VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK(&model);" in text
    assert "#ifdef VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK" not in text
