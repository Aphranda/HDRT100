"""Expose the firmware-global include set to host harnesses.

The firmware target passes every component include directory to each
translation unit. Host harnesses under ``tests/python`` assemble their own
``-I`` lists per compile, so a new cross-domain header would otherwise require
editing every harness that compiles a dependent unit. Provide the shared
algorithm header through the compiler's include path instead; the harness
commands stay untouched and the firmware build remains the source of truth for
which directories exist.
"""
from __future__ import annotations

import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ALGO_INCLUDE = ROOT / "components" / "algo" / "inc"


def pytest_configure(config) -> None:  # noqa: ARG001 - pytest hook signature
    existing = [entry for entry in os.environ.get("CPATH", "").split(os.pathsep)
                if entry]
    resolved = str(ALGO_INCLUDE)
    if resolved not in existing:
        os.environ["CPATH"] = os.pathsep.join([resolved, *existing])
