#!/usr/bin/env python3
"""Host acceptance workflow: numbered steps, one run directory, one credential.

This mirrors ``tools/hardware_acceptance/hardware_acceptance_workflow.py`` for
changes that need no bench: the credential is only written after every required
step ran, in order, in the same run directory, against the same staged host
fingerprint.

    python tools/acceptance/host_acceptance_workflow.py --profile quick
    python tools/acceptance/host_acceptance_workflow.py --run out/host/<run>
    python tools/acceptance/host_acceptance_workflow.py --profile full

``quick`` runs the staged host test files (the ones a commit carries); ``full``
runs the whole ``tests/python`` suite as a diagnostic. A receipt is written only
by the ``receipt`` step and is verified by the ``selfcheck`` step and by
``host_acceptance.py check-staged``.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "acceptance"))

import credential_scope  # noqa: E402
import host_acceptance  # noqa: E402

STATE_SCHEMA = "HAOFV_HOST_WORKFLOW_STATE_V1"
RECORD_SCHEMA = "HAOFV_HOST_WORKFLOW_RECORD_V1"
STEPS = (
    ("prepare", "prepare run directory, profile and path base"),
    ("scope", "staged host fingerprint and file inventory"),
    ("select", "select the test files and prove they are staged"),
    ("execute", "run pytest; all selected tests must pass"),
    ("receipt", "write the host credential bound to the staged fingerprint"),
    ("selfcheck", "recompute the staged fingerprint with check-staged"),
)
PROFILES = ("quick", "full")
SUMMARY = re.compile(
    r"(?P<passed>\d+) passed(?:, (?P<failed>\d+) failed)?"
    r"(?:, (?P<errors>\d+) errors?)?(?:, (?P<skipped>\d+) skipped)?"
    r"(?:, (?P<xfailed>\d+) xfailed)?")


class WorkflowError(RuntimeError):
    pass


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _log(run_dir: Path, message: str) -> None:
    line = f"[{datetime.now().strftime('%H:%M:%S')}] {message}"
    print(line)
    (run_dir / "workflow.log").open("a", encoding="utf-8").write(line + "\n")


def selected_paths(profile: str, explicit: list[str] | None) -> list[str]:
    if explicit:
        return list(explicit)
    if profile == "full":
        return sorted(p.relative_to(ROOT).as_posix()
                      for p in (ROOT / "tests" / "python").glob("test_*.py"))
    staged = host_acceptance.staged_test_paths()
    return staged or list(host_acceptance.DEFAULT_PATHS)


def execute(paths: list[str]) -> dict:
    """Run pytest once and return the parsed result (no receipt side effect)."""
    dirty = [path for path in paths if host_acceptance._unstaged(path)]
    command = [sys.executable, "-m", "pytest", *paths, "-q", "-p", "no:cacheprovider"]
    started = time.monotonic()
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    elapsed = time.monotonic() - started
    tail = (result.stdout + result.stderr).strip().splitlines()[-25:]
    summary_line = next((line for line in reversed(tail) if SUMMARY.search(line)), "")
    match = SUMMARY.search(summary_line)
    counts = {name: int(match.group(name) or 0) for name in
              ("passed", "failed", "errors", "skipped", "xfailed")} if match else {}
    return {
        "passed": result.returncode == 0 and not counts.get("failed") and
                  not counts.get("errors") and counts.get("passed", 0) > 0 and not dirty,
        "unstaged_tested_files": dirty,
        "command": command,
        "paths": paths,
        "duration_s": round(elapsed, 3),
        "returncode": result.returncode,
        "summary": counts,
        "summary_line": summary_line,
        "tail": tail,
    }


def _state_path(run_dir: Path) -> Path:
    return run_dir / "workflow-state.json"


def load_state(run_dir: Path) -> dict:
    path = _state_path(run_dir)
    if not path.is_file():
        raise WorkflowError(f"{path} is missing; start a new run without --run")
    state = json.loads(path.read_text(encoding="utf-8"))
    if state.get("schema") != STATE_SCHEMA:
        raise WorkflowError("unknown workflow state schema")
    return state


def save_state(run_dir: Path, state: dict) -> None:
    _state_path(run_dir).write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")


def _record_step(run_dir: Path, state: dict, name: str, detail: str,
                 duration: float, status: str = "OK") -> None:
    state.setdefault("steps", []).append(
        {"name": name, "status": status, "detail": detail,
         "duration_s": round(duration, 3), "finished_at_utc": _now()})
    save_state(run_dir, state)


def run_steps(run_dir: Path, profile: str, paths: list[str] | None,
              resume: bool) -> dict:
    if resume:
        state = load_state(run_dir)
        if state.get("profile") != profile:
            raise WorkflowError(
                f"run was started as profile={state.get('profile')}, not {profile}")
        completed = {step["name"] for step in state.get("steps", [])
                     if step.get("status") == "OK"}
    else:
        run_dir.mkdir(parents=True, exist_ok=True)
        state = {"schema": STATE_SCHEMA, "profile": profile, "paths": list(paths or []),
                 "started_at_utc": _now(), "steps": []}
        save_state(run_dir, state)
        completed = set()

    fingerprint = None
    file_count = 0
    execution: dict | None = None

    for name, title in STEPS:
        if name in completed:
            _log(run_dir, f"=== Step {name} SKIP (already OK) {title}")
            continue
        started = time.monotonic()
        _log(run_dir, f"=== Step {name} START {title}")
        try:
            if name == "prepare":
                run_dir.mkdir(parents=True, exist_ok=True)
                probe = subprocess.run([sys.executable, "-m", "pytest", "--version"],
                                       cwd=ROOT, capture_output=True, text=True)
                if probe.returncode != 0:
                    raise WorkflowError("pytest is not importable in this environment")
                detail = f"profile={profile} pytest={probe.stdout.strip()}"
            elif name == "scope":
                fingerprint, file_count = credential_scope.staged_scope_fingerprint("host", ROOT)
                if not fingerprint:
                    raise WorkflowError("no staged host files: nothing to certify")
                state["fingerprint"] = fingerprint
                state["file_count"] = file_count
                save_state(run_dir, state)
                detail = f"files={file_count} fingerprint={fingerprint[:12]}"
            elif name == "select":
                chosen = selected_paths(profile, paths or state.get("paths") or None)
                if not chosen:
                    raise WorkflowError("no test files selected")
                missing = [path for path in chosen if not (ROOT / path).is_file()]
                if missing:
                    raise WorkflowError(f"selected test files are missing: {missing}")
                dirty = [path for path in chosen if host_acceptance._unstaged(path)]
                if dirty:
                    raise WorkflowError(f"unstaged tested files: {dirty}")
                state["paths"] = chosen
                save_state(run_dir, state)
                collect = subprocess.run(
                    [sys.executable, "-m", "pytest", *chosen, "--collect-only", "-q",
                     "-p", "no:cacheprovider"], cwd=ROOT, capture_output=True, text=True)
                if collect.returncode != 0:
                    raise WorkflowError(
                        "collection failed:\n" + (collect.stdout + collect.stderr)[-2000:])
                detail = f"tests={len(chosen)} collect=OK"
            elif name == "execute":
                execution = execute(list(state.get("paths") or []))
                (run_dir / "execute.json").write_text(
                    json.dumps(execution, indent=2) + "\n", encoding="utf-8")
                detail = execution["summary_line"]
                if not execution["passed"]:
                    raise WorkflowError("host tests failed: " + execution["summary_line"])
            elif name == "receipt":
                if execution is None:
                    raise WorkflowError("no execution result in this run directory")
                if not execution["passed"]:
                    raise WorkflowError("refusing to certify a failed execution")
                current, count = credential_scope.staged_scope_fingerprint("host", ROOT)
                if current != state.get("fingerprint"):
                    raise WorkflowError(
                        "staged host fingerprint changed during the run; start over")
                receipt = dict(execution)
                receipt.update({
                    "schema": host_acceptance.SCHEMA,
                    "fingerprint": current,
                    "file_count": count,
                    "working_fingerprint": credential_scope.scope_fingerprint("host", ROOT)[0],
                    "started_at_utc": _now(),
                    "workflow_run": run_dir.name,
                })
                host_acceptance.RECEIPT.write_text(
                    json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
                detail = f"receipt={host_acceptance.RECEIPT.relative_to(ROOT).as_posix()}"
            elif name == "selfcheck":
                host_acceptance.check_staged()
                detail = "check-staged=OK"
            else:  # pragma: no cover - registry is fixed
                raise WorkflowError(f"unknown step {name}")
        except (WorkflowError, host_acceptance.HostAcceptanceError) as exc:
            elapsed = time.monotonic() - started
            _record_step(run_dir, state, name, str(exc), elapsed, status="FAIL")
            _log(run_dir, f"=== Step {name} FAIL  {elapsed:.1f}s  {exc}")
            raise
        elapsed = time.monotonic() - started
        _record_step(run_dir, state, name, detail, elapsed)
        _log(run_dir, f"=== Step {name} END    OK  {elapsed:.1f}s  {detail}")

    state["finished_at_utc"] = _now()
    save_state(run_dir, state)
    return state


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=PROFILES, default="quick")
    parser.add_argument("--paths", nargs="*", default=None)
    parser.add_argument("--run", default=None, help="resume an existing run directory")
    args = parser.parse_args(argv)
    run_dir = (Path(args.run) if args.run else
               ROOT / "out" / "host_acceptance" /
               datetime.now().strftime("%Y%m%d") /
               f"hawf-host-{datetime.now().strftime('%H%M%S')}")
    try:
        state = run_steps(run_dir, args.profile, args.paths, resume=bool(args.run))
    except (WorkflowError, host_acceptance.HostAcceptanceError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    receipt = json.loads(host_acceptance.RECEIPT.read_text(encoding="utf-8"))
    print(f"PASS host acceptance files={receipt['file_count']} "
          f"{receipt.get('summary_line')} run={run_dir.relative_to(ROOT).as_posix()}")
    (run_dir / "workflow-record.json").write_text(
        json.dumps({"schema": RECORD_SCHEMA, "profile": args.profile,
                    "run": run_dir.relative_to(ROOT).as_posix(),
                    "receipt": host_acceptance.RECEIPT.relative_to(ROOT).as_posix(),
                    "steps": state.get("steps", [])}, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
