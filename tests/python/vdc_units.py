"""Body views of the split VDC units for the host harnesses."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/vdc_dpll_manager/src"


def body(name: str) -> str:
    """Unit code without its include prologue."""
    text = (SRC / f"{name}.c").read_text(encoding="utf-8")
    marker = '#include "vdc_dpll_manager_internal.h"'
    idx = text.rfind(marker)
    return text[idx + len(marker):] if idx >= 0 else text
