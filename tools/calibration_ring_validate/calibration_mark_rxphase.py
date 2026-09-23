#!/usr/bin/env python3
"""Acquire independent CS26 -> CS27 receiver phase; no production matrix apply."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from calibration_ring_validate.calibration_rxphase import MARK_PROTOCOL, main


if __name__ == "__main__":
    raise SystemExit(main(MARK_PROTOCOL))
