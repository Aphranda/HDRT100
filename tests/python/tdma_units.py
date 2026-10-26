"""Locate TDMA symbols in the live units after the ``.inc`` archive.

TDMA harnesses used to slice function bodies out of a monolithic
``tdma_pio_spi_phys.c``.  The phys core is three units now, so a harness asks
for the symbol and this module names the unit that actually defines it.
"""
from pathlib import Path

import c_text

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/tdma/src"


def units() -> list[Path]:
    return sorted(SRC.glob("*.c"))


def unit_with(name: str) -> Path:
    """The single live unit that defines ``name``."""
    hits = c_text.definition_units(SRC, name)
    if not hits:
        raise KeyError(f"no live TDMA unit defines {name}")
    if len(hits) > 1:
        names = ", ".join(path.name for path in hits)
        raise KeyError(f"{name} is defined in several units: {names}")
    return hits[0]


def function_body(name: str) -> str:
    """Definition of ``name`` from the unit that defines it."""
    path = unit_with(name)
    return c_text.function_body(path.read_text(encoding="utf-8"), name)
