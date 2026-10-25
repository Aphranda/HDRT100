"""Text views of the RefMem production units for the host harnesses.

The `.inc` fragments became real translation units. Harnesses that used to
`#include` a fragment (or append its text) must now take the unit's *body*,
without the new include prologue: the harness supplies that context itself,
and several of them deliberately redefine the little-endian helpers.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/distributed_refmem/src"


def body(unit: str, strip_static: bool = False) -> str:
    """Return the code of one RefMem unit, minus its include prologue."""
    text = (SRC / f"{unit}.c").read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    while lines and (not lines[0].strip() or lines[0].lstrip().startswith("#include")):
        lines.pop(0)
    if strip_static:
        lines = [ln[len("static "):] if ln.startswith("static ") else ln for ln in lines]
    return "".join(lines)


def source() -> str:
    """Concatenated production text of the parent unit and its split units."""
    names = (
        "distributed_refmem.c",
        "distributed_refmem_position_ack.c",
        "distributed_refmem_vdc_feedback.c",
        "distributed_action_probe.c",
        "distributed_refmem_priority_health.c",
        "refmem_le.h",
    )
    return "\n".join((SRC / n).read_text(encoding="utf-8")
                     for n in names if (SRC / n).is_file())
