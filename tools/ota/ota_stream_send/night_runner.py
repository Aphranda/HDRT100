#!/usr/bin/env python3
"""Night-time TDMA OTA executor (OTA-TDMA-013).

Runs an external transfer command repeatedly inside a wall-clock budget, one
directory per attempt, and records a machine-readable summary that the night
runbook can attach to evidence.  Recovery hooks run between failed attempts;
they are injected so the executor stays host-testable without a bench.
"""

from __future__ import annotations

import json
import subprocess
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Sequence


@dataclass(frozen=True)
class NightPlan:
    attempts: int
    wall_clock_s: float
    out_dir: Path
    command: Sequence[str] = ()

    def __post_init__(self) -> None:
        if self.attempts < 1:
            raise ValueError("attempts must be >= 1")
        if self.wall_clock_s <= 0:
            raise ValueError("wall clock budget must be positive")


@dataclass
class AttemptResult:
    index: int
    started_utc: str
    duration_s: float
    exit_code: int
    ok: bool
    recovered: bool = False
    error: str = ""

    def as_dict(self) -> dict:
        return {
            "index": self.index,
            "started_utc": self.started_utc,
            "duration_s": round(self.duration_s, 3),
            "exit_code": self.exit_code,
            "ok": self.ok,
            "recovered": self.recovered,
            "error": self.error,
        }


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def default_command_runner(command: Sequence[str], attempt_dir: Path) -> int:
    """Runs one attempt with the given command, capturing its logs."""
    stdout_path = attempt_dir / "stdout.log"
    stderr_path = attempt_dir / "stderr.log"
    with stdout_path.open("w", encoding="utf-8") as out, \
            stderr_path.open("w", encoding="utf-8") as err:
        completed = subprocess.run(list(command), stdout=out, stderr=err,
                                   check=False)
    return int(completed.returncode)


def run_night(
    plan: NightPlan,
    runner: Callable[[Path], int] | None = None,
    recover: Callable[[Path], None] | None = None,
    clock: Callable[[], float] = time.monotonic,
    write_summary: bool = True,
) -> dict:
    """Executes the plan; returns the summary dict (also written to disk)."""
    plan.out_dir.mkdir(parents=True, exist_ok=True)
    runner = runner or (lambda directory: default_command_runner(plan.command,
                                                                directory))
    started = clock()
    results: list[AttemptResult] = []
    summary_path = plan.out_dir / "summary.json"
    ok = False

    for index in range(1, plan.attempts + 1):
        elapsed = clock() - started
        if elapsed >= plan.wall_clock_s:
            break
        attempt_dir = plan.out_dir / f"attempt-{index:02d}"
        attempt_dir.mkdir(parents=True, exist_ok=True)
        recovered = False
        if index > 1 and results and not results[-1].ok and recover is not None:
            try:
                recover(attempt_dir)
                recovered = True
            except Exception as exc:  # recovery failure must not mask the run
                recovered = False
                (attempt_dir / "recover-error.txt").write_text(
                    f"{type(exc).__name__}: {exc}\n", encoding="utf-8")
        attempt_start = clock()
        started_utc = _utc_now()
        error = ""
        try:
            exit_code = int(runner(attempt_dir))
        except Exception as exc:
            exit_code = -1
            error = f"{type(exc).__name__}: {exc}"
        duration = clock() - attempt_start
        result = AttemptResult(index=index, started_utc=started_utc,
                               duration_s=duration, exit_code=exit_code,
                               ok=exit_code == 0, recovered=recovered,
                               error=error)
        results.append(result)
        (attempt_dir / "result.json").write_text(
            json.dumps(result.as_dict(), indent=2), encoding="utf-8")
        if result.ok:
            ok = True
            break

    summary = _summary(plan, results, clock() - started)
    summary["passed"] = ok
    if write_summary:
        summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return summary


def _summary(plan: NightPlan, results: list[AttemptResult],
             elapsed: float) -> dict:
    return {
        "schema": "OTA_TDMA_NIGHT_RUN_V1",
        "attempts_requested": plan.attempts,
        "wall_clock_s": plan.wall_clock_s,
        "elapsed_s": round(elapsed, 3),
        "attempts_run": len(results),
        "attempts": [result.as_dict() for result in results],
        "command": list(plan.command),
        "out_dir": str(plan.out_dir),
    }


def _recovery_command(command: Sequence[str]) -> Callable[[Path], None]:
    def recover(directory: Path) -> None:
        with (directory / "recover.log").open("w", encoding="utf-8") as log:
            completed = subprocess.run(list(command), stdout=log, stderr=log,
                                       check=False)
        if completed.returncode != 0:
            raise RuntimeError(f"recovery exited {completed.returncode}")
    return recover


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempts", type=int, default=1)
    parser.add_argument("--wall-clock-s", type=float, default=600.0)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--recover", nargs=argparse.REMAINDER, default=None,
                        help="recovery command run before a retry (argv tail)")
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="transfer command (argv tail)")
    args = parser.parse_args(argv)
    command = list(args.command)
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a transfer command is required")
    recover_argv = list(args.recover or [])
    if recover_argv and recover_argv[0] == "--":
        recover_argv = recover_argv[1:]
    plan = NightPlan(attempts=args.attempts, wall_clock_s=args.wall_clock_s,
                     out_dir=args.out_dir, command=command)
    summary = run_night(
        plan,
        recover=(_recovery_command(recover_argv) if recover_argv else None),
    )
    print(f"night passed={summary['passed']} attempts={summary['attempts_run']} "
          f"summary={plan.out_dir / 'summary.json'}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
