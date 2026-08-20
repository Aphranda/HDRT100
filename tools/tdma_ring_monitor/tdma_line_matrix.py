#!/usr/bin/env python3
"""Map product TDMA output-to-input wiring across 2..8 boards by *IDN?."""

from __future__ import annotations

import argparse
import json
import sys
import time
from contextlib import ExitStack
from dataclasses import asdict
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from line_map_check import (  # noqa: E402
    drive_gpio21_24,
    query,
    read_spi_line_mask,
    release_all,
)
from scpi_common.board_identity import parse_idn_response  # noqa: E402
from scpi_common.scpi_serial import open_serial_port  # noqa: E402
from tdma_start_ring import discover  # noqa: E402


REQUIRED_TDMA_BITS = 0xD  # CS, DATA, SCK: GPIO21/23/24 -> GPIO16/18/19


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address; repeat for 2..8 boards")
    parser.add_argument("--expected-build")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.3)
    parser.add_argument("--edge-settle", type=float, default=0.12)
    parser.add_argument("--reboot", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    board_ids = list(args.board_id)
    if len(board_ids) < 2 or len(board_ids) > 8:
        raise SystemExit("board count must be in [2, 8]")
    if len(set(board_ids)) != len(board_ids):
        raise SystemExit("board IDs must be unique")
    args.board_ids = board_ids

    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        wrong = {address: boards[address].build for address in board_ids
                 if boards[address].build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")

    ordered = [boards[address] for address in board_ids]
    matches = {driver.address: {sampler.address: 0 for sampler in ordered
                               if sampler.address != driver.address}
               for driver in ordered}
    samples: list[dict[str, object]] = []

    with ExitStack() as stack:
        serials = {
            board.address: stack.enter_context(open_serial_port(
                board.port, args.baud, args.timeout, args.settle))
            for board in ordered
        }
        for board in ordered:
            identity = parse_idn_response(query(
                serials[board.address], "*IDN?", args.timeout))
            if identity.address != board.address:
                raise RuntimeError(
                    f"{board.port}: identity changed to {identity.address}")
            _ = query(serials[board.address],
                      "SYSTem:TDMA:RING:STOP", min(args.timeout, 1.0))
            release_all(serials[board.address], args.timeout)

        try:
            for driver in ordered:
                driver_ser = serials[driver.address]
                for bit in range(4):
                    drive_gpio21_24(driver_ser, bit, 0, args.timeout)
                    time.sleep(args.edge_settle)
                    low = {
                        sampler.address: read_spi_line_mask(
                            serials[sampler.address], args.timeout)
                        for sampler in ordered if sampler != driver
                    }
                    drive_gpio21_24(driver_ser, bit, 1, args.timeout)
                    time.sleep(args.edge_settle)
                    high = {
                        sampler.address: read_spi_line_mask(
                            serials[sampler.address], args.timeout)
                        for sampler in ordered if sampler != driver
                    }
                    for sampler in ordered:
                        if sampler == driver:
                            continue
                        rising = high[sampler.address] & ~low[sampler.address]
                        if rising & (1 << bit):
                            matches[driver.address][sampler.address] |= 1 << bit
                        samples.append({
                            "driver": driver.address,
                            "driver_gpio": 21 + bit,
                            "sampler": sampler.address,
                            "low_mask": low[sampler.address],
                            "high_mask": high[sampler.address],
                            "rising_mask": rising,
                        })
                    drive_gpio21_24(driver_ser, bit, 0, args.timeout)
        finally:
            for board in ordered:
                release_all(serials[board.address], args.timeout)

        adjacency = {
            driver: [sampler for sampler, mask in row.items()
                     if mask & REQUIRED_TDMA_BITS == REQUIRED_TDMA_BITS]
            for driver, row in matches.items()
        }
        incoming = {address: 0 for address in board_ids}
        for receivers in adjacency.values():
            for receiver in receivers:
                incoming[receiver] += 1
        ring_closed = (
            all(len(adjacency[address]) == 1 for address in board_ids)
            and all(incoming[address] == 1 for address in board_ids)
        )

        if args.reboot:
            for board in ordered:
                ser = serials[board.address]
                ser.reset_input_buffer()
                ser.write(b"SYSTem:BOOT:RESet\n")
                ser.flush()

    result = {
        "passed": ring_closed,
        "required_tdma_bits": REQUIRED_TDMA_BITS,
        "boards": {address: asdict(boards[address]) for address in board_ids},
        "matched_masks": matches,
        "adjacency": adjacency,
        "incoming_count": incoming,
        "samples": samples,
        "reboot_requested": args.reboot,
    }
    out_dir = args.out_dir or (
        ROOT / "build" /
        f"tdma_line_matrix_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if ring_closed else 1


if __name__ == "__main__":
    raise SystemExit(main())
