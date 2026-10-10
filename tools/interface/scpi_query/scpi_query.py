#!/usr/bin/env python3
"""Compatibility entrypoint for the classified SCPI query tool."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.domains.interface.scpi_query.scpi_query import *  # noqa: E402,F403
from tools.domains.interface.scpi_query.scpi_query import main  # noqa: E402,F401


if __name__ == "__main__":
    raise SystemExit(main())
