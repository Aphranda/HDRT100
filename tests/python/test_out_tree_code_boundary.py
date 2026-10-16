"""out/ holds evidence, never maintained code.

Two boundaries are enforced here:

1. The live joint-capture acceptance stack (the code P3 step 29 actually runs)
   must load every repository module from ``tools/``. Nothing under ``out/``
   may appear in its import closure.
2. No maintained tool may load Python code from ``out/``. Archived code is the
   single exception: ``tools/archive/`` keeps historical tooling and the pinned
   compatibility lineage (``tools/archive/vdc/vdc_joint_capture/lineage_v1/``,
   whose sha256 is pinned in
   ``config/tool_workflows/vdc_joint_capture_internal_seal.json``) as evidence,
   and neither archive is ever executed. Archived material is retained, never
   deleted: retired scripts are copied there instead of being removed.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ARCHIVE = "tools/archive/"
MANIFEST = "config/tool_workflows/vdc_joint_capture_internal_seal.json"
OUT_TREE_PY = re.compile(r"out[/\\][A-Za-z0-9_./\\-]*\.py")


def test_live_joint_capture_stack_loads_only_tools_modules():
    """The adapter the P3 workflow builds must not import code from out/."""
    from tools.workflows.vdc_joint_capture import runtime_base
    from tools.workflows.vdc_joint_capture.inputs import load_inputs

    manifest = ROOT / MANIFEST
    adapter = ROOT / (ARCHIVE + "vdc/vdc_joint_capture/lineage_v1/internal_seal.py")
    inputs = load_inputs(manifest, ROOT, adapter)
    external = runtime_base.load_runtime(
        inputs.runtime_entrypoint, list(inputs.board_uids))
    assert external.Probe is not None

    out_tree = []
    for module in list(sys.modules.values()):
        path = getattr(module, "__file__", None)
        if not path:
            continue
        resolved = pathlib.Path(path).resolve()
        if resolved.is_relative_to(ROOT):
            relative = resolved.relative_to(ROOT).as_posix()
            if relative.startswith("out/"):
                out_tree.append(relative)
    assert out_tree == [], f"live stack imported out/-tree code: {sorted(set(out_tree))}"


def test_manifest_declares_a_fully_tracked_runtime():
    """An out/-tree runtime closure would have to be declared, and it is empty."""
    import json

    adapter = json.loads((ROOT / MANIFEST).read_text(encoding="utf-8"))["adapter"]
    assert adapter["runtime_closure"] == []
    assert adapter["runtime_entrypoint"] is None
    for role, declaration in adapter["migrated_sources"].items():
        assert declaration["path"].startswith("tools/"), role


def test_only_archived_code_records_out_tree_loaders():
    """Maintained (git-tracked) tools must not load Python code from out/."""
    import subprocess

    listed = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "tools"],
        check=True, capture_output=True, text=True).stdout.split()
    offenders = {}
    for relative in sorted(listed):
        if not relative.endswith(".py") or relative.startswith(ARCHIVE):
            continue
        path = ROOT / relative
        if not path.is_file():
            continue
        hits = sorted(set(OUT_TREE_PY.findall(
            path.read_text(encoding="utf-8", errors="replace"))))
        if hits:
            offenders[relative] = hits
    assert offenders == {}, f"maintained tools load out/-tree code: {offenders}"
