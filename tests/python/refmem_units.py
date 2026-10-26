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
    """Production text of every RefMem unit plus the split private headers.

    The domain has several translation units now (core planes, cmd-reply,
    position ACK, VDC feedback, action probe, priority health).  A text
    extractor that names one file goes stale on every split, so this helper
    concatenates the whole source directory in a stable order.
    """
    files = sorted(path for path in SRC.glob("*.c"))
    headers = [SRC / name for name in
               ("distributed_refmem_internal.h", "distributed_refmem_split_internal.h",
                "refmem_le.h")]
    ordered = files + [path for path in headers if path.is_file()]
    return "\n".join(path.read_text(encoding="utf-8") for path in ordered)
