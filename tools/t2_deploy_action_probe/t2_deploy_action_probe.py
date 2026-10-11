"""Import-safe compatibility wrapper for the archived T2 deploy/action probe.

The tools migration moved the implementation to
`tools/archive/deletable-candidates/t2_deploy_action_probe/`, a path that cannot
be imported because its directory name contains a hyphen. The host model test
`tests/python/test_t2_deploy_action_probe.py` still exercises that model, so this
wrapper loads the archived file by path and re-exports its public names.

Ownership note: the migration's reference audit missed this test import when the
module was classified as a deletable candidate. Retire this wrapper and the test
together once the TOOLS owner confirms the model is no longer wanted.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import sys

_ROOT = Path(__file__).resolve().parents[2]
_TARGET = (_ROOT / "tools" / "archive" / "deletable-candidates" /
           "t2_deploy_action_probe" / "t2_deploy_action_probe.py")

_spec = importlib.util.spec_from_file_location(__name__ + "._archived", _TARGET)
if _spec is None or _spec.loader is None:  # pragma: no cover - environment guard
    raise ImportError(f"archived T2 deploy/action probe not found: {_TARGET}")
_archived = importlib.util.module_from_spec(_spec)
# Dataclasses resolve their defining module through sys.modules, so register the
# dynamically loaded module before executing it.
sys.modules[_spec.name] = _archived
_spec.loader.exec_module(_archived)

# Names consumed by the host model test, re-exported unchanged.
for _name in ("ActionBook", "ActionError", "ActionState", "Completion", "Fence",
              "Intent", "Ready", "SyncReadyStamp", "default_slots",
              "run_positive_demo"):
    globals()[_name] = getattr(_archived, _name)
del _name
