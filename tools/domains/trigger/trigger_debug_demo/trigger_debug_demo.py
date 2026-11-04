#!/usr/bin/env python3
"""Entry point for the single-port REFMEM trigger debug demo."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.domains.trigger.trigger_debug_demo.app import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
