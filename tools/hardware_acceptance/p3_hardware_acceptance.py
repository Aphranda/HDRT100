#!/usr/bin/env python3
"""Compatibility shim for the acceptance step library.

The implementation moved to ``workflow_steps.py`` (M7.3).  When imported, this
module replaces itself with that module object so existing
``import p3_hardware_acceptance as p3`` users (and their monkeypatches) share
exactly the same module the workflow imports.  When executed as a script it
forwards to the canonical CLI.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

if __package__:
    from . import workflow_steps as _impl
else:  # top-level import (tools/hardware_acceptance on sys.path) or __main__
    import workflow_steps as _impl

if __name__ == "__main__":
    raise SystemExit(_impl.main())

# Importers (including `from p3_hardware_acceptance import X`) receive the
# canonical module object, so p3 and workflow_steps can never drift apart.
sys.modules[__name__] = _impl
