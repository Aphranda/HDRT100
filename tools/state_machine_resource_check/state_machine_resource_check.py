#!/usr/bin/env python3
"""Validate the static PIO/SM/DMA direction contract.

This is a source-level gate for the first state-machine migration step.  It
does not claim that the firmware is HIL validated; it rejects resource overlap
and accidental reuse of the full-duplex composite DATA programs for the
directional flight persona.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REQUIRED = {
    "BOARD_TDMA_TX_PIO_BLOCK_ID": 1,
    "BOARD_TDMA_RX_PIO_BLOCK_ID": 2,
    "BOARD_TDMA_SMA_PIO_BLOCK_ID": 0,
    "BOARD_TDMA_TX_CLK_OUT_SM": 0,
    "BOARD_TDMA_TX_SYNC_OUT_SM": 1,
    "BOARD_TDMA_TX_DATA_IN_FORWARD_SM": 2,
    "BOARD_TDMA_TX_DATA_IN_CAPTURE_SM": 3,
    "BOARD_TDMA_RX_CLK_IN_SM": 0,
    "BOARD_TDMA_RX_SYNC_IN_SM": 1,
    "BOARD_TDMA_RX_DATA_OUT_SM": 2,
    "BOARD_TDMA_RX_EVIDENCE_IN_SM": 3,
    "BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL": 4,
    "BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL": 5,
    "BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL": 6,
    "BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL": 7,
}


def macros(text: str) -> dict[str, int]:
    return {name: int(value) for name, value in re.findall(
        r"^#define\s+(BOARD_TDMA_[A-Z0-9_]+)\s+([0-9]+)u?\s*$",
        text, re.MULTILINE,
    )}


def program_body(text: str, name: str) -> str:
    match = re.search(
        rf"\.program\s+{re.escape(name)}\b(?P<body>.*?)(?=^\.program\s+|^% c-sdk|\Z)",
        text, re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise ValueError(f"missing PIO program {name}")
    return match.group("body")


def check(board: Path, pio: Path) -> list[str]:
    failures: list[str] = []
    values = macros(board.read_text(encoding="utf-8", errors="ignore"))
    for name, expected in REQUIRED.items():
        if values.get(name) != expected:
            failures.append(f"{name}: expected {expected}, got {values.get(name)!r}")

    for group in (
        ("TX logical-port SM", "BOARD_TDMA_TX_CLK_OUT_SM", "BOARD_TDMA_TX_SYNC_OUT_SM",
         "BOARD_TDMA_TX_DATA_IN_FORWARD_SM", "BOARD_TDMA_TX_DATA_IN_CAPTURE_SM"),
        ("RX logical-port SM", "BOARD_TDMA_RX_CLK_IN_SM", "BOARD_TDMA_RX_SYNC_IN_SM",
         "BOARD_TDMA_RX_DATA_OUT_SM", "BOARD_TDMA_RX_EVIDENCE_IN_SM"),
        ("DMA", "BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL",
         "BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL",
         "BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL", "BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL"),
    ):
        label, *names = group
        seen: dict[int, str] = {}
        for name in names:
            if name not in values:
                continue
            if values[name] in seen:
                failures.append(f"{label} overlap: {name} == {seen[values[name]]}")
            seen[values[name]] = name
    if values.get("BOARD_TDMA_TX_PIO_BLOCK_ID") == values.get("BOARD_TDMA_RX_PIO_BLOCK_ID"):
        failures.append("TX and RX PIO blocks overlap")
    if values.get("BOARD_TDMA_SMA_PIO_BLOCK_ID") in {
        values.get("BOARD_TDMA_TX_PIO_BLOCK_ID"), values.get("BOARD_TDMA_RX_PIO_BLOCK_ID")
    }:
        failures.append("SMA PIO overlaps TDMA PIO")

    try:
        pio_text = pio.read_text(encoding="utf-8", errors="ignore")
        tx = program_body(pio_text, "tdma_pio_spi_directional_data_tx")
        rx = program_body(pio_text, "tdma_pio_spi_directional_data_rx")
        ctl_tx = program_body(pio_text, "tdma_pio_spi_directional_control_tx")
        ctl_rx = program_body(pio_text, "tdma_pio_spi_directional_control_rx")
    except ValueError as exc:
        failures.append(str(exc))
    else:
        if re.search(r"\bin\s+pins\b", tx):
            failures.append("directional DATA TX contains in pins")
        if re.search(r"\bout\s+pins\b", rx):
            failures.append("directional DATA RX contains out pins")
        if re.search(r"\bin\s+pins\b", ctl_tx):
            failures.append("directional SYNC/CLK TX contains in pins")
        if re.search(r"\bout\s+pins\b", ctl_rx):
            failures.append("directional SYNC/CLK RX contains out pins")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", type=Path,
                        default=Path("boards/rp2350_trig/inc/board_config.h"))
    parser.add_argument("--pio", type=Path,
                        default=Path("components/tdma/src/tdma_pio_spi.pio"))
    args = parser.parse_args()
    failures = check(args.board, args.pio)
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"SUMMARY FAIL failures={len(failures)}")
        return 1
    print("OK   directional PIO/SM/DMA resource contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
