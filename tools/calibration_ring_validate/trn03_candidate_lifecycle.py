#!/usr/bin/env python3
"""Apply the replayable TRN-03C candidate lifecycle offline.

This tool only transforms evidence JSON.  It never talks to a board and never
claims that a returned active package has been accepted by the firmware/VDC
consumer; the board owner must repeat its identity and freshness gates.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.calibration_ring_validate.trn03_candidate import (  # noqa: E402
    activate_candidate,
    rollback_candidate,
    stage_candidate,
)


def load(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("stage", "activate", "rollback"))
    parser.add_argument("--candidate", type=Path,
                        help="inactive TRN-03C active_candidate.json")
    parser.add_argument("--active", type=Path,
                        help="current lifecycle active JSON")
    parser.add_argument("--rollbackable", type=Path,
                        help="previous lifecycle rollbackable JSON")
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.action in ("stage", "activate") and args.candidate is None:
            raise ValueError(f"--candidate is required for {args.action}")
        if args.action == "rollback" and (args.active is None or
                                           args.rollbackable is None):
            raise ValueError("--active and --rollbackable are required for rollback")
        if args.action == "stage":
            result: object = stage_candidate(load(args.candidate))
        elif args.action == "activate":
            active, rollbackable = activate_candidate(
                load(args.candidate), load(args.active) if args.active else None)
            result = {"active": active, "rollbackable": rollbackable}
        else:
            active, rollbackable = rollback_candidate(
                load(args.active), load(args.rollbackable))
            result = {"active": active, "rollbackable": rollbackable}
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8")
    print(json.dumps({"action": args.action, "output": str(args.out)},
                     ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
