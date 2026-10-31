#!/usr/bin/env python3
"""TDMA relay night orchestration (OTA-TDMA-013 + OTA-TDMA-014).

Builds the relay transfer command (`ota_stream_send --relay-node`) and hands it
to the night executor so a bench window can run N attempts with a wall-clock
budget and a recovery hook, one evidence directory per attempt.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path

from tools.ota.ota_stream_send.night_runner import NightPlan, run_night


@dataclass(frozen=True)
class RelayNightPlan:
    port: str
    package: Path
    relay_node: int
    out_dir: Path
    attempts: int = 1
    wall_clock_s: float = 3600.0
    block_size: int = 4096
    session_id: int | None = None
    recover_command: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        if not 0 <= self.relay_node <= 31:
            raise ValueError("relay node must fit 0..31")
        if self.attempts < 1:
            raise ValueError("attempts must be >= 1")
        if self.wall_clock_s <= 0:
            raise ValueError("wall clock budget must be positive")
        if not self.port:
            raise ValueError("port is required")
        if not self.package.name:
            raise ValueError("package path is required")


def build_relay_command(plan: RelayNightPlan) -> list[str]:
    """Command line for one relay attempt (delegates all protocol work)."""
    command = [
        sys.executable, "-m", "tools.ota.ota_stream_send.ota_stream_send",
        str(plan.package), "--package",
        "--port", plan.port,
        "--relay-node", str(plan.relay_node),
        "--block-size", str(plan.block_size),
    ]
    if plan.session_id is not None:
        command.extend(["--session-id", str(plan.session_id)])
    return command


def to_night_plan(plan: RelayNightPlan) -> NightPlan:
    return NightPlan(attempts=plan.attempts, wall_clock_s=plan.wall_clock_s,
                     out_dir=plan.out_dir,
                     command=build_relay_command(plan))


def recovery_hook(plan: RelayNightPlan) -> "callable | None":
    if not plan.recover_command:
        return None

    def recover(directory: Path) -> None:
        from tools.ota.ota_stream_send.night_runner import _recovery_command
        _recovery_command(plan.recover_command)(directory)

    return recover


def run(plan: RelayNightPlan, dry_run: bool = False) -> dict:
    night = to_night_plan(plan)
    if dry_run:
        return {
            "schema": "OTA_TDMA_NIGHT_PLAN_V1",
            "dry_run": True,
            "attempts": night.attempts,
            "wall_clock_s": night.wall_clock_s,
            "out_dir": str(night.out_dir),
            "command": night.command,
            "recover": list(plan.recover_command),
        }
    return run_night(night, recover=recovery_hook(plan))


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--relay-node", type=int, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--attempts", type=int, default=1)
    parser.add_argument("--wall-clock-s", type=float, default=3600.0)
    parser.add_argument("--block-size", type=int, default=4096)
    parser.add_argument("--session-id", type=int, default=None)
    parser.add_argument("--recover", nargs=argparse.REMAINDER, default=[],
                        help="recovery command (argv tail)")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)
    recover = list(args.recover)
    if recover and recover[0] == "--":
        recover = recover[1:]
    plan = RelayNightPlan(port=args.port, package=args.package,
                          relay_node=args.relay_node, out_dir=args.out_dir,
                          attempts=args.attempts,
                          wall_clock_s=args.wall_clock_s,
                          block_size=args.block_size,
                          session_id=args.session_id,
                          recover_command=tuple(recover))
    summary = run(plan, dry_run=args.dry_run)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2),
                                 encoding="utf-8")
    print(f"night passed={summary.get('passed', 'dry-run')} "
          f"attempts={summary.get('attempts_run', 0)} "
          f"command={' '.join(summary.get('command', []))}")
    if args.dry_run:
        return 0
    return 0 if summary.get("passed") else 1


if __name__ == "__main__":
    raise SystemExit(main())
