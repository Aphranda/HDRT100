#!/usr/bin/env python3
"""Compatibility entrypoint for the classified VDC sync metrology tool."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.domains.vdc.sync_metrology.vdc_sync_metrology import *  # noqa: E402,F403
from tools.domains.vdc.sync_metrology.vdc_sync_metrology import main  # noqa: E402,F401


if __name__ == "__main__":
    raise SystemExit(main())
