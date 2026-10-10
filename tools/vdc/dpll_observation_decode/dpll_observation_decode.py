#!/usr/bin/env python3
"""Compatibility entrypoint for the classified DPLL observation decoder."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

CANONICAL_ENTRYPOINT = (
    ROOT / "tools" / "domains" / "vdc" / "dpll_observation_decode" /
    "dpll_observation_decode.py")


if __name__ == "__main__":
    runpy.run_path(str(CANONICAL_ENTRYPOINT), run_name="__main__")
else:
    from tools.domains.vdc.dpll_observation_decode.dpll_observation_decode import *  # noqa: E402,F403
