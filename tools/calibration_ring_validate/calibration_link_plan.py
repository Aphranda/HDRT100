#!/usr/bin/env python3
"""Build and gate the P3 per-link low-to-high calibration plan.

This tool deliberately does not invent measurements.  It discovers boards by
their exact ``*IDN?`` address, validates the cyclic order, and checks the
static PIO frequency/duty report before a future board-to-board P3 persona is
allowed to run.  Until that persona publishes four hardware-latched edges,
the plan remains ``NOT_READY`` rather than treating ring RTT as link delay.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from tdma_start_ring import discover  # noqa: E402
from calibration_ring_validate.calibration_link_frequency_policy import (  # noqa: E402
    validation_frequency_ladder,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical ring order")
    parser.add_argument("--expected-build")
    parser.add_argument("--frequency-mhz", action="append", type=int,
                        default=None)
    parser.add_argument("--timing-report", type=Path,
                        default=ROOT / "build-product-release" /
                        "tdma_pio_timing_check_reflection_20260821.json")
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def make_links(board_ids: list[str]) -> list[dict[str, object]]:
    return [
        {"link_index": index, "source": source, "destination": board_ids[(index + 1) % len(board_ids)]}
        for index, source in enumerate(board_ids)
    ]


def main() -> int:
    args = parse_args()
    if not 2 <= len(args.board_id) <= 8 or len(set(args.board_id)) != len(args.board_id):
        raise SystemExit("board-id count must be unique and in [2, 8]")
    try:
        frequencies = validation_frequency_ladder(args.frequency_mhz)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    args.board_ids = list(args.board_id)
    args.baud = 115200
    args.timeout = 5.0
    args.settle = 0.2
    boards = discover(args)
    missing = sorted(set(args.board_id) - set(boards))
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(missing)}")
    wrong_build = {
        address: boards[address].build for address in args.board_id
        if args.expected_build and boards[address].build != args.expected_build
    }

    timing = json.loads(args.timing_report.read_text(encoding="utf-8"))
    reflection = timing.get("reflection_calibration", {})
    rows = reflection.get("burst_profiles", [])
    by_target = {int(row["target_hz"] // 1_000_000): row for row in rows}
    timing_gate = []
    for frequency in frequencies:
        row = by_target.get(frequency)
        timing_gate.append({
            "frequency_mhz": frequency,
            "present": row is not None,
            "frequency_ok": bool(row and row.get("frequency_ok")),
            "duty_ok": bool(row and row.get("duty_ok")),
            "duty_percent": row.get("duty_percent") if row else None,
            "actual_hz": row.get("actual_hz") if row else None,
        })

    result = {
        "measurement_domain": "calibration",
        "phase": "p3_per_link_plan",
        "status": "NOT_READY",
        "diagnostic_only": True,
        "board_ids_in_physical_order": args.board_id,
        "boards": {
            address: {"port": boards[address].port, "build": boards[address].build,
                      "idn": boards[address].idn}
            for address in args.board_id
        },
        "links": make_links(args.board_id),
        "frequency_ladder_mhz": frequencies,
        "frequency_policy": {
            "stable_required_mhz": [10, 25],
            "limited_rx_mhz": 30,
            "limited_rx_fallback_mhz": 25,
            "limited_rx_required_in_every_validation": True,
        },
        "timing_gate": timing_gate,
        "timing_report": str(args.timing_report),
        "required_evidence": [
            "same train_epoch/sequence",
            "t1/t2/t3/t4 hardware-latched",
            "residence and path_sum",
            "endpoint bias generation",
            "topology/profile freshness",
            "DMA overrun/stall counters",
        ],
        "physical_channel_model": {
            "source_outputs": {"sync": 26, "clock": 25},
            "destination_inputs": {"sync": 27, "clock": 28},
            "destination_return_output": {"data": 29},
            "source_return_input": {"data": 24},
            "fixed_direction": True,
            "interpretation": (
                "Each BiSS segment carries CLK source-to-destination and "
                "DATA destination-to-source. ISO1452 direction is fixed and "
                "already matches the P3 t1/t2/t3/t4 equation."
            ),
        },
        "blockers": [
            "board-to-board P3 persona is not exposed by current firmware",
            "READ:CAL:LINK? is metadata-only and no per-link t1..t4 snapshot exists",
        ],
        "wrong_build": wrong_build,
        "passed": not wrong_build and all(
            row["present"] and row["frequency_ok"] and row["duty_ok"]
            for row in timing_gate
        ),
    }
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    print(f"out_dir={args.out_dir}")
    # Static frequency/duty gate can pass, but P3 remains NOT_READY until the
    # board persona publishes the required hardware evidence.
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
