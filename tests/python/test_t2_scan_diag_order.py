"""Contract test for the TRIGger:REServation:T2:SCAN? diagnostic page.

The rolling-window investigation (docs/trigger/t2/TRIGGER_T2_TODO.md A.3.2 - A.3.12)
spent a dozen rounds mis-reading T2? because readers assumed field positions that the
page had outgrown. T2:SCAN? exists so the window's own facts can be read without any
positional assumption, which only holds while its field order is pinned. This test pins
it across the three places that must agree:

  * the SCPI handler's emission order (what a reader actually receives),
  * the handler's call into the runtime getter (argument order),
  * the getter's definition and the public prototype.

A silent reorder in any one of them would hand out values under the wrong names, which
is worse than no reading at all.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HANDLER = ROOT / "middleware" / "scpi_port" / "src" / "scpi_trigger_t2_runtime_commands.inc"
HANDLER_HEADER = ROOT / "middleware" / "scpi_port" / "inc" / "scpi_system_snapshot_commands.h"
RUNTIME = ROOT / "components" / "sync_trigger" / "src" / "trigger_t2_runtime.c"
RUNTIME_HEADER = ROOT / "components" / "sync_trigger" / "inc" / "trigger_t2_runtime.h"

COMMAND = "TRIGger:REServation:T2:SCAN?"
GETTER = "trigger_t2_runtime_scan_diagnostics"
HANDLER_FN = "scpi_cmd_trigger_t2_scan_q"

# The page a reader receives, in order. Keep this list and the docs in step.
FIELDS = (
    "step_mdeg",
    "branch_entered",
    "scan_result",
    "window_step_mdeg",
    "positions",
    "step_pulses",
    "window_entered",
    "window_announced",
    "window_overwrote",
    "announce_mismatch",
)


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _handler_body() -> str:
    source = _read(HANDLER)
    start = source.index(f"scpi_result_t {HANDLER_FN}(scpi_t *context)")
    end = source.index("\n}", start)
    return source[start:end]


def test_command_is_registered() -> None:
    table = _read(HANDLER_HEADER)
    assert f'.pattern = "{COMMAND}", .callback = {HANDLER_FN}' in table
    assert f"scpi_result_t {HANDLER_FN}(scpi_t *context);" in table


def test_handler_emits_the_documented_fields_in_order() -> None:
    emitted = re.findall(r"SCPI_ResultUInt32\(context,\s*([A-Za-z_][A-Za-z0-9_]*)\)", _handler_body())
    assert tuple(emitted) == FIELDS, (
        "T2:SCAN? field order drifted from the documented order; a reader would label "
        f"values wrongly. expected={FIELDS} emitted={tuple(emitted)}"
    )


def test_handler_passes_the_same_order_to_the_getter() -> None:
    body = _handler_body()
    call = re.search(rf"{GETTER}\((.*?)\);", body, re.S)
    assert call is not None, "the handler no longer calls the runtime getter"
    passed = re.findall(r"&([A-Za-z_][A-Za-z0-9_]*)", call.group(1))
    assert tuple(passed) == FIELDS, (
        f"getter argument order drifted: expected={FIELDS} passed={tuple(passed)}"
    )
    # ... and each local is declared in the handler, so a rename cannot slip through.
    for name in FIELDS:
        assert re.search(rf"uint32_t [^;]*\b{name}\b", body), f"{name} is not declared"


def test_getter_definition_and_prototype_agree() -> None:
    body = _read(RUNTIME)
    start = body.index(f"void {GETTER}(")
    params = body[body.index("(", start) + 1 : body.index(")", start)]
    names = re.findall(r"\*\s*([A-Za-z_][A-Za-z0-9_]*)", params)
    assert tuple(names) == FIELDS, f"getter parameters drifted: {tuple(names)}"

    proto = _read(RUNTIME_HEADER)
    pstart = proto.index(f"void {GETTER}(")
    pparams = proto[proto.index("(", pstart) + 1 : proto.index(")", pstart)]
    pnames = re.findall(r"\*\s*([A-Za-z_][A-Za-z0-9_]*)", pparams)
    assert tuple(pnames) == FIELDS, f"prototype parameters drifted: {tuple(pnames)}"


def test_getter_writes_each_field_once() -> None:
    body = _read(RUNTIME)
    start = body.index(f"void {GETTER}(")
    end = body.index("\n}", start)
    for name in FIELDS:
        writes = re.findall(rf"if \({name}\) \*{name} =", body[start:end])
        assert len(writes) == 1, f"{name} must be written exactly once, found {len(writes)}"
