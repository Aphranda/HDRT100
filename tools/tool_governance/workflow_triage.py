#!/usr/bin/env python3
"""Workflow-form triage for repository tools (stdlib only).

Every tool family must be judged once: is it a staged **WORKFLOW** (orchestration
with evidence + resume), a **STEP** invoked by a workflow, a single-purpose
**STANDALONE** tool, or a **LIBRARY** with no entrypoint?

The rule this enforces, from the 2026-11-04 user directive: a script that is
actually used must be judged, and new scripts must be judged before they are
used -- an untriaged ``*_workflow.py`` entrypoint is a failure, not a style nit.
UNASSESSED legacy families stay warnings until they are qualified, so the gate
can be wired into pre-commit without freezing every commit.

    python tools/tool_governance/workflow_triage.py check [--root DIR]

Exit codes: 0 = no errors (warnings allowed), 1 = errors, 2 = bad input.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REGISTRY_REL = Path("tools/tool_registry.json")
TRIAGE_REL = Path("tools/tool_workflow_triage.json")
SCHEMA = "TOOLS_WORKFLOW_TRIAGE_V1"
FORMS = ("WORKFLOW", "STEP", "STANDALONE", "LIBRARY")
# ``--run`` or a ``run`` subparser (``add_parser("run"`` / ``run = subparsers.add_parser(``).
RESUME_RE = re.compile(r'"--run"|--run\b|add_parser\(\s*["\']run["\']|def run\(')


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def has_resume(entry: Path) -> bool:
    text = entry.read_text(encoding="utf-8", errors="ignore")
    return RESUME_RE.search(text) is not None


def check(root: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    registry_path = root / REGISTRY_REL
    triage_path = root / TRIAGE_REL
    if not registry_path.is_file() or not triage_path.is_file():
        return ([f"missing {REGISTRY_REL} or {TRIAGE_REL}"], warnings)
    registry = load(registry_path)
    triage = load(triage_path)
    if triage.get("schema") != SCHEMA:
        errors.append(f"{TRIAGE_REL}: schema must be {SCHEMA}")
    tools = {tool["id"]: tool for tool in registry.get("tools", [])}
    rows = triage.get("entries") or []
    seen: dict[str, dict] = {}
    for index, row in enumerate(rows, 1):
        label = f"{TRIAGE_REL}: entry #{index}"
        if not isinstance(row, dict):
            errors.append(f"{label} is not an object")
            continue
        tool_id = row.get("tool")
        form = row.get("form")
        if not tool_id:
            errors.append(f"{label}: missing tool id")
            continue
        label = f"{TRIAGE_REL}: {tool_id}"
        if tool_id in seen:
            errors.append(f"{label}: duplicate triage row")
            continue
        seen[tool_id] = row
        if tool_id not in tools:
            errors.append(f"{label}: not in {REGISTRY_REL.name}")
        if form not in FORMS:
            errors.append(f"{label}: form {form!r} must be one of {FORMS}")
            continue
        if not row.get("reason"):
            warnings.append(f"{label}: missing reason (judgement should be auditable)")
        if form == "WORKFLOW":
            entry = row.get("entry")
            evidence = row.get("evidence_root") or ""
            if not entry or not (root / entry).is_file():
                errors.append(f"{label}: WORKFLOW entry missing: {entry!r}")
            elif not has_resume(root / entry):
                warnings.append(
                    f"{label}: no in-script resume flag found in {entry} "
                    "(declare it in 'resume' or add --run/run subcommand)")
            if not evidence.startswith("out/"):
                errors.append(f"{label}: WORKFLOW evidence_root must start with out/: {evidence!r}")
            if not row.get("resume"):
                errors.append(f"{label}: WORKFLOW must declare how to resume")
            if entry and "/workflows/" not in entry and not entry.endswith("_workflow.py"):
                warnings.append(
                    f"{label}: WORKFLOW entry is not under tools/workflows/ or *_workflow.py")
        if form == "STEP":
            workflow = row.get("workflow")
            if workflow not in seen or seen.get(workflow, {}).get("form") != "WORKFLOW":
                errors.append(f"{label}: STEP must name an earlier WORKFLOW row ('workflow': id)")
        if form == "LIBRARY" and tool_id in tools and tools[tool_id].get("entrypoints"):
            errors.append(f"{label}: LIBRARY must not declare entrypoints")
    for tool_id, tool in tools.items():
        if tool_id in seen:
            continue
        if tool.get("lifecycle") == "UNASSESSED":
            warnings.append(f"{TRIAGE_REL}: UNASSESSED family not judged yet: {tool_id}")
        else:
            errors.append(
                f"{TRIAGE_REL}: {tool_id} is {tool.get('lifecycle')} but has no triage row")
    for tool_id, tool in tools.items():
        for entry in tool.get("entrypoints", []):
            if entry.endswith("_workflow.py") and seen.get(tool_id, {}).get("form") != "WORKFLOW":
                errors.append(
                    f"{TRIAGE_REL}: {entry} is a workflow entrypoint but {tool_id} is not "
                    "triaged WORKFLOW")
    return errors, warnings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check",))
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--quiet", action="store_true",
                        help="print only the summary and the first few warnings")
    args = parser.parse_args(argv)
    try:
        errors, warnings = check(args.root.resolve())
    except (OSError, json.JSONDecodeError) as exc:
        print(f"FAIL workflow-triage: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2
    shown = warnings[:5] if args.quiet else warnings
    for warning in shown:
        print(f"WARN {warning}")
    if args.quiet and len(warnings) > len(shown):
        print(f"WARN ... {len(warnings) - len(shown)} more warnings (run without --quiet)")
    for error in errors:
        print(f"FAIL {error}")
    if errors:
        print(f"SUMMARY FAIL errors={len(errors)} warnings={len(warnings)}")
        return 1
    print(f"OK   workflow triage: errors=0 warnings={len(warnings)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
