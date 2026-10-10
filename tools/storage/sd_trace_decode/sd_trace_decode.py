#!/usr/bin/env python3
"""Compatibility entrypoint for the classified SD trace decoder."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.domains.storage.sd_trace_decode.sd_trace_decode import *  # noqa: E402,F403
from tools.domains.storage.sd_trace_decode.sd_trace_decode import main  # noqa: E402,F401


if __name__ == "__main__":
    raise SystemExit(main())
