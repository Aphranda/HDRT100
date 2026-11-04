#!/usr/bin/env python3
"""Archived compatibility shim for the acceptance step library (M7.5).

The implementation lives at ``tools/hardware_acceptance/workflow_steps.py``.
This file is kept only as the historical path evidence and is no longer used by
the repository: the workflow, tests and ``.githooks/pre-commit`` all import
``workflow_steps`` directly.  See ``README.md`` in this directory for the
migration and archive record.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance import workflow_steps as _impl  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(_impl.main())

# A stale importer of the old path still receives the canonical module object.
sys.modules[__name__] = _impl
