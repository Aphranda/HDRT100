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

PIN_REQUIRED = {
    "BOARD_TDMA_TX_CLK_OUT_PIN": 25,
    "BOARD_TDMA_TX_SYNC_OUT_PIN": 26,
    "BOARD_TDMA_TX_DATA_IN_PIN": 24,
    "BOARD_TDMA_RX_CLK_IN_PIN": 28,
    "BOARD_TDMA_RX_SYNC_IN_PIN": 27,
    "BOARD_TDMA_RX_DATA_OUT_PIN": 29,
}


def macros(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    pending: dict[str, str] = {}
    for name, value in re.findall(
        r"^#define\s+(BOARD_TDMA_[A-Z0-9_]+)\s+([^/\r\n]+?)\s*$",
        text, re.MULTILINE,
    ):
        pending[name] = value.strip()
    # Resolve numeric literals and board aliases without duplicating pin-map
    # facts in this tool.  Only simple integer expressions are accepted.
    for _ in range(len(pending) + 1):
        progress = False
        for name, expr in pending.items():
            if name in values:
                continue
            candidate = expr.rstrip("uU").strip()
            if candidate.isdigit():
                values[name] = int(candidate)
                progress = True
                continue
            alias = re.fullmatch(r"([A-Z0-9_]+)", candidate)
            if alias and alias.group(1) in values:
                values[name] = values[alias.group(1)]
                progress = True
        if not progress:
            break
    # Board pin aliases are intentionally outside the BOARD_TDMA_* namespace;
    # resolve them from the same header so the contract follows board_config.
    board_aliases = {
        name: int(value.rstrip("uU"))
        for name, value in re.findall(
            r"^#define\s+([A-Z][A-Z0-9_]*)\s+([0-9]+)u?\s*$",
            text, re.MULTILINE,
        )
    }
    for name, expr in pending.items():
        if name not in values:
            candidate = expr.rstrip("uU").strip()
            if candidate in board_aliases:
                values[name] = board_aliases[candidate]
    return values


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
    for name, expected in PIN_REQUIRED.items():
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
        tx = program_body(pio_text, "tdma_pio_spi_directional_tx")
        rx = program_body(pio_text, "tdma_pio_spi_directional_rx")
    except ValueError as exc:
        failures.append(str(exc))
    else:
        for name, body in (("TX", tx), ("RX", rx)):
            if not re.search(r"\bin\s+pins\b", body):
                failures.append(f"directional {name} is missing in pins")
            if not re.search(r"\bout\s+pins\b", body):
                failures.append(f"directional {name} is missing out pins")
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
