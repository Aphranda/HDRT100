#!/usr/bin/env python3
"""Validate the static PIO/SM/DMA flight-persona contract."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RESOURCES = ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
DEFAULT_PHYS = ROOT / "components/tdma/src/tdma_pio_spi_phys.c"
DEFAULT_FLIGHT_IO = ROOT / "components/tdma/src/tdma_pio_spi_phys_flight_io.inc"


REQUIRED = {
    "BOARD_TDMA_TX_PIO_BLOCK_ID": 1,
    "BOARD_TDMA_RX_PIO_BLOCK_ID": 2,
    "BOARD_TDMA_SMA_PIO_BLOCK_ID": 0,
    "BOARD_TDMA_TX_CONTROL_OUT_SM": 0,
    "BOARD_TDMA_TX_RTT_EVIDENCE_SM": 1,
    "BOARD_TDMA_TX_CLOCK_LATCH_SM": 2,
    "BOARD_TDMA_TX_DATA_CAPTURE_SM": 3,
    "BOARD_TDMA_RX_RESERVED_CONTROL_SM": 0,
    "BOARD_TDMA_RX_RESERVED_EVIDENCE_SM": 1,
    "BOARD_TDMA_RX_DATA_FLIGHT_SM": 2,
    "BOARD_TDMA_RX_CLOCK_LATCH_SM": 3,
    # Deprecated directional names remain checked as compatibility slots.
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
    # Deprecated runtime aliases stay on the old PIO2 numbering until the
    # crossed-PIO implementation consumes the directional roles directly.
    "BOARD_TDMA_TX_CONTROL_SM": 0,
    "BOARD_TDMA_TX_DATA_SM": 1,
    "BOARD_TDMA_TX_EDGE_SM": 2,
    "BOARD_TDMA_TX_RECOVERY_SM": 3,
    "BOARD_TDMA_RX_FORWARD_SM": 0,
    "BOARD_TDMA_RX_CAPTURE_SM": 1,
    "BOARD_TDMA_RX_CONTROL_SM": 2,
    "BOARD_TDMA_RX_EVIDENCE_SM": 3,
    "BOARD_TDMA_TX_PAYLOAD_DMA_CHANNEL": 5,
    "BOARD_TDMA_RX_CAPTURE_DMA_CHANNEL": 4,
    "BOARD_TDMA_RX_FORWARD_DMA_CHANNEL": 6,
    "BOARD_TDMA_TX_EDGE_DMA_CHANNEL": 7,
}


def macros(text: str) -> dict[str, int]:
    return {
        name: int(value)
        for name, value in re.findall(
            r"^#define\s+(BOARD_TDMA_[A-Z0-9_]+)\s+([0-9]+)u?\s*$",
            text,
            re.MULTILINE,
        )
    }


def program_body(text: str, name: str) -> str:
    match = re.search(
        rf"\.program\s+{re.escape(name)}\b(?P<body>.*?)(?=^\.program\s+|^% c-sdk|\Z)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise ValueError(f"missing PIO program {name}")
    return match.group("body")


def instruction_text(body: str) -> str:
    return "\n".join(line.split(";", 1)[0] for line in body.splitlines())


def c_function_body(text: str, name: str) -> str:
    marker = re.search(rf"\b{re.escape(name)}\s*\(", text)
    if marker is None:
        raise ValueError(f"missing C function {name}")
    opening = text.find("{", marker.end())
    if opening < 0:
        raise ValueError(f"missing C function body {name}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise ValueError(f"unterminated C function body {name}")


def check_rx_endpoint_declaration(text: str) -> list[str]:
    failures: list[str] = []
    required = (
        "tdma_state_machine_fifo_endpoint_t",
        "tdma_state_machine_rx_endpoint_contract_t",
        "tdma_state_machine_rx_endpoint_contract_valid",
        ".rx_endpoints = tdma_state_machine_rx_endpoint_contract()",
    )
    for token in required:
        if token not in text:
            failures.append(f"RX endpoint contract is missing {token}")
    try:
        contract = c_function_body(
            text, "tdma_state_machine_rx_endpoint_contract")
    except ValueError as exc:
        failures.append(str(exc))
        return failures

    if contract.count(".pio = BOARD_TDMA_RX_PIO") != 3:
        failures.append("all RX endpoints must remain on the RX PIO")
    if contract.count(".sm = BOARD_TDMA_RX_DATA_FLIGHT_SM") != 2:
        failures.append("DATA output/unload must share the declared RX PIO SM")
    if contract.count(".sm = BOARD_TDMA_RX_CLOCK_LATCH_SM") != 1:
        failures.append("clock evidence must use its dedicated RX PIO SM")
    if ".business_rx_consumer_count = 1u" not in contract:
        failures.append("RX DATA business FIFO must have exactly one consumer")
    for token, expected, message in (
        (".fifo_direction = TDMA_STATE_MACHINE_FIFO_TX",
         1,
         "DATA output endpoint must declare its TX FIFO"),
        (".fifo_direction = TDMA_STATE_MACHINE_FIFO_RX",
         2,
         "DATA unload/evidence endpoints must declare RX FIFOs"),
        (".dreq_direction = TDMA_STATE_MACHINE_DREQ_TX",
         1,
         "DATA output endpoint must declare the TX DREQ"),
        (".dreq_direction = TDMA_STATE_MACHINE_DREQ_RX",
         1,
         "DATA unload endpoint must declare the RX DREQ"),
        (".dreq_direction = TDMA_STATE_MACHINE_DREQ_NONE",
         1,
         "clock evidence endpoint must not declare a DMA DREQ"),
        (".owner = TDMA_STATE_MACHINE_ENDPOINT_OWNER_DMA",
         2,
         "DATA output/unload endpoints must be owned by DMA"),
        (".owner = TDMA_STATE_MACHINE_ENDPOINT_OWNER_CORE1",
         1,
         "clock evidence FIFO must be owned by core1"),
        (".dma_channel = BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL",
         1,
         "DATA output endpoint must use the output DMA channel"),
        (".dma_channel = BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL",
         1,
         "DATA unload endpoint must use the capture DMA channel"),
        (".dma_channel = TDMA_STATE_MACHINE_DMA_CHANNEL_NONE",
         1,
         "clock evidence endpoint must not claim a DMA channel"),
    ):
        if contract.count(token) != expected:
            failures.append(message)
    return failures


def check_rx_endpoint_runtime(phys_text: str, flight_io_text: str) -> list[str]:
    failures: list[str] = []
    for token in (
        "flight_resources.rx_endpoints.data_output.pio",
        "flight_resources.rx_endpoints.data_output.sm",
        "flight_resources.rx_endpoints.data_unload.pio",
        "flight_resources.rx_endpoints.data_unload.sm",
        "flight_resources.rx_endpoints.clock_evidence.pio",
        "flight_resources.rx_endpoints.clock_evidence.sm",
        "tdma_state_machine_rx_endpoint_contract_valid",
    ):
        if token not in phys_text:
            failures.append(f"flight runtime does not consume {token}")

    try:
        rx_arm = c_function_body(phys_text, "tdma_pio_spi_phys_rx_arm")
        overlay = c_function_body(
            phys_text, "tdma_pio_spi_phys_start_overlay_script")
        clock_evidence = c_function_body(
            phys_text, "tdma_pio_spi_phys_clock_latch_read_and_rearm")
        origin_tx = c_function_body(
            flight_io_text, "tdma_pio_spi_phys_flight_origin_tx")
    except ValueError as exc:
        failures.append(str(exc))
        return failures

    if not re.search(
        r"pio_get_dreq\s*\(\s*capture_pio\s*,\s*capture_sm\s*,\s*false\s*\)",
        rx_arm,
    ):
        failures.append("business RX DMA must use the RX DREQ")
    if not re.search(
        r"&\s*capture_pio->rxf\s*\[\s*capture_sm\s*\]", rx_arm
    ):
        failures.append("business RX DMA must read the declared RX FIFO")
    if len(re.findall(r"->rxf\s*\[", rx_arm)) != 1:
        failures.append("business RX FIFO must have one DMA consumer")

    if not re.search(
        r"pio_get_dreq\s*\(.*?\bdata_pio\b.*?\bdata_sm\b.*?true\s*\)",
        origin_tx,
        re.DOTALL,
    ):
        failures.append("origin DATA output must use the TX DREQ")
    if not re.search(
        r"&\s*data_pio->txf\s*\[\s*data_sm\s*\]", origin_tx
    ):
        failures.append("origin DATA output must write the declared TX FIFO")
    if not re.search(
        r"pio_get_dreq\s*\(.*?tdma_pio_spi_phys_data_pio\s*\(\s*phys\s*\)"
        r".*?tdma_pio_spi_phys_data_sm\s*\(\s*phys\s*\).*?true\s*\)",
        overlay,
        re.DOTALL,
    ):
        failures.append("follower overlay output must use the TX DREQ")
    if not re.search(
        r"&\s*tdma_pio_spi_phys_data_pio\s*\(\s*phys\s*\)->txf\s*\["
        r"\s*tdma_pio_spi_phys_data_sm\s*\(\s*phys\s*\)\s*\]",
        overlay,
    ):
        failures.append(
            "follower overlay output must write the declared TX FIFO")

    if not re.search(
        r"pio_sm_get\s*\(\s*evidence_pio\s*,\s*latch_sm\s*\)",
        clock_evidence,
    ):
        failures.append("clock evidence must be read from its dedicated RX FIFO")
    return failures


def check(
    board: Path,
    pio: Path,
    *,
    resources: Path = DEFAULT_RESOURCES,
    phys: Path = DEFAULT_PHYS,
    flight_io: Path = DEFAULT_FLIGHT_IO,
) -> list[str]:
    failures: list[str] = []
    values = macros(board.read_text(encoding="utf-8", errors="ignore"))
    for name, expected in REQUIRED.items():
        if values.get(name) != expected:
            failures.append(f"{name}: expected {expected}, got {values.get(name)!r}")

    for label, names in (
        ("TX PIO persona SM", (
            "BOARD_TDMA_TX_CONTROL_OUT_SM", "BOARD_TDMA_TX_RTT_EVIDENCE_SM",
            "BOARD_TDMA_TX_CLOCK_LATCH_SM", "BOARD_TDMA_TX_DATA_CAPTURE_SM",
        )),
        ("RX PIO persona SM", (
            "BOARD_TDMA_RX_RESERVED_CONTROL_SM", "BOARD_TDMA_RX_RESERVED_EVIDENCE_SM",
            "BOARD_TDMA_RX_DATA_FLIGHT_SM", "BOARD_TDMA_RX_CLOCK_LATCH_SM",
        )),
        ("DMA", (
            "BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL",
            "BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL",
            "BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL",
            "BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL",
        )),
    ):
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
        values.get("BOARD_TDMA_TX_PIO_BLOCK_ID"),
        values.get("BOARD_TDMA_RX_PIO_BLOCK_ID"),
    }:
        failures.append("SMA PIO overlaps TDMA PIO")

    try:
        pio_text = pio.read_text(encoding="utf-8", errors="ignore")
        tx = program_body(pio_text, "tdma_pio_spi_directional_data_tx")
        rx = program_body(pio_text, "tdma_pio_spi_directional_data_rx")
        ctl_tx = program_body(pio_text, "tdma_pio_spi_directional_control_tx")
        ctl_rx = program_body(pio_text, "tdma_pio_spi_directional_control_rx")
        flight_data = program_body(pio_text, "tdma_pio_spi_flight_data_follower")
        process_data = program_body(
            pio_text, "tdma_pio_spi_flight_process_follower")
        origin_data_tx = program_body(
            pio_text, "tdma_pio_spi_flight_origin_data_tx")
        origin_data_capture = program_body(
            pio_text, "tdma_pio_spi_flight_origin_data_capture")
        clock_latch = program_body(
            pio_text, "tdma_pio_spi_flight_clock_latch")
        raw_follower_init = c_function_body(
            pio_text, "tdma_pio_spi_flight_data_follower_program_init")
        process_follower_init = c_function_body(
            pio_text, "tdma_pio_spi_flight_process_follower_program_init")
    except ValueError as exc:
        failures.append(str(exc))
    else:
        if re.search(r"\bin\s+pins\b", tx):
            failures.append("directional DATA TX contains in pins")
        if re.search(r"\bout\s+pins\b", rx):
            failures.append("directional DATA RX contains out pins")
        if re.search(r"\bin\s+pins\b", ctl_tx):
            failures.append("directional control TX contains in pins")
        if re.search(r"\bout\s+pins\b", ctl_rx):
            failures.append("directional control RX contains out pins")
        for name, body in (
            ("flight DATA follower", flight_data),
            ("process-image DATA follower", process_data),
        ):
            if not re.search(r"\bin\s+pins\b", body):
                failures.append(f"{name} does not sample incoming DATA")
            if not re.search(r"\bout\s+pins\b", body):
                failures.append(f"{name} does not forward outgoing DATA")
            if not re.search(
                r"^\s*push\s+noblock\b",
                instruction_text(body),
                re.MULTILINE | re.IGNORECASE,
            ):
                failures.append(
                    f"{name} is missing business RX unload push")
        if re.search(r"\bin\s+pins\b", origin_data_tx):
            failures.append("origin DATA TX contains in pins")
        if not re.search(r"\bout\s+pins\b", origin_data_tx):
            failures.append("origin DATA TX does not drive outgoing DATA")
        if re.search(r"\bout\s+pins\b", origin_data_capture):
            failures.append("origin DATA capture contains out pins")
        if not re.search(r"\bin\s+pins\b", origin_data_capture):
            failures.append("origin DATA capture does not sample returned DATA")
        if "PIO_FIFO_JOIN_RX" not in raw_follower_init:
            failures.append("raw DATA follower must dedicate its FIFO to unload")
        if "sm_config_set_fifo_join" in process_follower_init:
            failures.append(
                "process-image DATA follower must keep independent TX/RX FIFOs")
        clock_instructions = instruction_text(clock_latch)
        if not re.search(r"^\s*push\s+noblock\b", clock_instructions,
                         re.MULTILINE | re.IGNORECASE):
            failures.append("clock evidence latch is missing its RX FIFO push")
        if re.search(r"\b(?:in|out)\s+pins\b", clock_instructions):
            failures.append("clock evidence latch must not consume or drive DATA")

    failures.extend(check_rx_endpoint_declaration(
        resources.read_text(encoding="utf-8", errors="ignore")))
    failures.extend(check_rx_endpoint_runtime(
        phys.read_text(encoding="utf-8", errors="ignore"),
        flight_io.read_text(encoding="utf-8", errors="ignore"),
    ))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", type=Path,
                        default=Path("boards/rp2350_trig/inc/board_config.h"))
    parser.add_argument("--pio", type=Path,
                        default=Path("components/tdma/src/tdma_pio_spi.pio"))
    parser.add_argument("--resources", type=Path, default=DEFAULT_RESOURCES)
    parser.add_argument("--phys", type=Path, default=DEFAULT_PHYS)
    parser.add_argument("--flight-io", type=Path, default=DEFAULT_FLIGHT_IO)
    args = parser.parse_args()
    failures = check(args.board, args.pio, resources=args.resources,
                     phys=args.phys, flight_io=args.flight_io)
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"SUMMARY FAIL failures={len(failures)}")
        return 1
    print("OK   PIO/SM/DMA flight-persona resource contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
