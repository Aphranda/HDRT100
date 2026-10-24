#!/usr/bin/env python3
"""Query the four P3 boards and report whether they run one consistent build.

Exit code 0 when every board reports the same non-empty ``SYSTem:FW:BUILD?``
value (the precondition for skipping a rebuild + OTA in the P3 workflow).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "hardware_acceptance"))
import p3_hardware_acceptance as p3  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "config" / "hardware_acceptance" / "p3_bench.json",
    )
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    config = p3.load_bench_config(args.config)
    board_ids = list(config["p3_board_ids_in_physical_order"])
    builds = p3.read_online_builds(board_ids, p3.DEFAULT_ACCEPTANCE_TIMING)
    ids = set(builds.values())
    consistent = len(ids) == 1 and "" not in ids
    result = {
        "consistent": consistent,
        "build_id": next(iter(ids)) if consistent else None,
        "boards": builds,
    }
    print(json.dumps(result, ensure_ascii=False))
    return 0 if consistent else 1


if __name__ == "__main__":
    raise SystemExit(main())
