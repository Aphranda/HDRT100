#!/usr/bin/env python3
"""Audit TDMA PIO SCK frequency and duty cycle from repository sources.

This is a static timing gate.  It parses the active PIO loop, the divider's
declared cycles-per-bit constant, and BOARD_SYS_CLOCK_HZ, then reports the
waveform produced after the RP2350 PIO 16.8 divider quantization.  Electrical
rise/fall time still requires an oscilloscope.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, asdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PIO_SOURCE = ROOT / "components" / "tdma" / "src" / "tdma_pio_spi.pio"
BOARD_CONFIG = ROOT / "boards" / "rp2350_trig" / "inc" / "board_config.h"


@dataclass(frozen=True)
class Timing:
    target_hz: int
    requested_divider: float
    programmed_divider: float
    actual_hz: float
    frequency_error_percent: float
    high_ns: float
    low_ns: float
    duty_percent: float
    frequency_ok: bool
    duty_ok: bool


@dataclass(frozen=True)
class BurstTiming:
    target_hz: int
    requested_divider: float
    programmed_divider: float
    actual_hz: float
    frequency_error_percent: float
    high_ns: float
    low_ns: float
    duty_percent: float
    frequency_ok: bool
    duty_ok: bool


def parse_define_u32(text: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+(\d+)(?:[uUlL]*)\s*$",
        text, re.MULTILINE)
    if match is None:
        raise ValueError(f"missing integer define {name}")
    return int(match.group(1))


def program_body(text: str, program: str) -> str:
    match = re.search(
        rf"^\.program\s+{re.escape(program)}\s*$([\s\S]*?)^\.wrap\s*$",
        text, re.MULTILINE)
    if match is None:
        raise ValueError(f"PIO program {program} not found")
    return match.group(1)


def parse_sideset_loop_cycles(body: str, label: str) -> tuple[int, int]:
    match = re.search(
        rf"^{re.escape(label)}:\s*$([\s\S]*)", body, re.MULTILINE)
    if match is None:
        raise ValueError(f"PIO loop label {label} not found")
    high_cycles = 0
    low_cycles = 0
    instruction_count = 0
    for raw_line in match.group(1).splitlines():
        line = raw_line.split(";", maxsplit=1)[0].strip()
        if not line or line.endswith(":"):
            continue
        side = re.search(r"\bside\s+([01])(?:\s+\[(\d+)\])?\s*$", line)
        if side is None:
            continue
        cycles = 1 + int(side.group(2) or 0)
        if side.group(1) == "1":
            high_cycles += cycles
        else:
            low_cycles += cycles
        instruction_count += 1
        if line.startswith("jmp"):
            break
    if instruction_count == 0 or high_cycles == 0 or low_cycles == 0:
        raise ValueError("PIO sideset loop is incomplete")
    return high_cycles, low_cycles


def parse_declared_bit_cycles(text: str) -> int:
    match = re.search(r"const\s+uint32_t\s+bit_cycles\s*=\s*(\d+)u?\s*;", text)
    if match is None:
        raise ValueError("bit_cycles divider declaration not found")
    return int(match.group(1))


def parse_burst_loop_cycles(body: str, label: str = "clk_burst_loop") -> tuple[int, int]:
    """Return high/low PIO cycles for the coarse reflection burst loop."""
    return parse_sideset_loop_cycles(body, label)


def quantize_pio_divider(divider: float) -> float:
    # pico-sdk 2.2 defaults PICO_PIO_CLKDIV_ROUND_NEAREST through
    # PICO_CLKDIV_ROUND_NEAREST, so model its +0.5 LSB conversion exactly.
    fixed = int(divider * 256.0 + 0.5)
    return fixed / 256.0


def calculate_timing(sys_clock_hz: int, target_hz: int,
                     declared_cycles: int, high_cycles: int, low_cycles: int,
                     expected_duty_percent: float,
                     frequency_tolerance_percent: float,
                     duty_tolerance_percent: float) -> Timing:
    actual_cycles = high_cycles + low_cycles
    requested_divider = sys_clock_hz / (target_hz * declared_cycles)
    programmed_divider = quantize_pio_divider(requested_divider)
    actual_hz = sys_clock_hz / (programmed_divider * actual_cycles)
    instruction_ns = programmed_divider * 1e9 / sys_clock_hz
    duty_percent = 100.0 * high_cycles / actual_cycles
    frequency_error_percent = 100.0 * (actual_hz - target_hz) / target_hz
    return Timing(
        target_hz=target_hz,
        requested_divider=requested_divider,
        programmed_divider=programmed_divider,
        actual_hz=actual_hz,
        frequency_error_percent=frequency_error_percent,
        high_ns=high_cycles * instruction_ns,
        low_ns=low_cycles * instruction_ns,
        duty_percent=duty_percent,
        frequency_ok=abs(frequency_error_percent) <= frequency_tolerance_percent,
        duty_ok=abs(duty_percent - expected_duty_percent) <= duty_tolerance_percent,
    )


def calculate_burst_timing(sys_clock_hz: int, target_hz: int,
                           declared_cycles: int, high_cycles: int,
                           low_cycles: int, expected_duty_percent: float,
                           frequency_tolerance_percent: float,
                           duty_tolerance_percent: float) -> BurstTiming:
    actual_cycles = high_cycles + low_cycles
    requested_divider = sys_clock_hz / (target_hz * declared_cycles)
    programmed_divider = quantize_pio_divider(requested_divider)
    actual_hz = sys_clock_hz / (programmed_divider * actual_cycles)
    instruction_ns = programmed_divider * 1e9 / sys_clock_hz
    duty_percent = 100.0 * high_cycles / actual_cycles
    frequency_error_percent = 100.0 * (actual_hz - target_hz) / target_hz
    return BurstTiming(
        target_hz=target_hz,
        requested_divider=requested_divider,
        programmed_divider=programmed_divider,
        actual_hz=actual_hz,
        frequency_error_percent=frequency_error_percent,
        high_ns=high_cycles * instruction_ns,
        low_ns=low_cycles * instruction_ns,
        duty_percent=duty_percent,
        frequency_ok=abs(frequency_error_percent) <= frequency_tolerance_percent,
        duty_ok=abs(duty_percent - expected_duty_percent) <= duty_tolerance_percent,
    )


def audit(targets_hz: list[int], expected_duty_percent: float,
          frequency_tolerance_percent: float,
          duty_tolerance_percent: float) -> dict[str, object]:
    pio_text = PIO_SOURCE.read_text(encoding="utf-8")
    board_text = BOARD_CONFIG.read_text(encoding="utf-8")
    sys_clock_hz = parse_define_u32(board_text, "BOARD_SYS_CLOCK_HZ")
    body = program_body(pio_text, "tdma_pio_spi_tx_byte")
    high_cycles, low_cycles = parse_sideset_loop_cycles(body, "bitloop")
    declared_cycles = parse_declared_bit_cycles(pio_text)
    burst_body = program_body(pio_text, "tdma_pio_spi_clk_burst")
    burst_high_cycles, burst_low_cycles = parse_burst_loop_cycles(burst_body)
    burst_rows = [calculate_burst_timing(
        sys_clock_hz, target_hz, 4, burst_high_cycles, burst_low_cycles,
        expected_duty_percent, frequency_tolerance_percent,
        duty_tolerance_percent) for target_hz in targets_hz]
    forward_body = program_body(pio_text, "tdma_pio_spi_clk_forward")
    forward_has_edge_regeneration = (
        "wait 1 gpio 0" in forward_body and
        "set pins, 1" in forward_body and
        "wait 0 gpio 0" in forward_body and
        "set pins, 0" in forward_body)
    rows = [calculate_timing(
        sys_clock_hz, target_hz, declared_cycles, high_cycles, low_cycles,
        expected_duty_percent, frequency_tolerance_percent,
        duty_tolerance_percent) for target_hz in targets_hz]
    return {
        "source": str(PIO_SOURCE.relative_to(ROOT)),
        "sys_clock_hz": sys_clock_hz,
        "divider_declared_cycles_per_bit": declared_cycles,
        "pio_actual_cycles_per_bit": high_cycles + low_cycles,
        "pio_high_cycles": high_cycles,
        "pio_low_cycles": low_cycles,
        "expected_duty_percent": expected_duty_percent,
        "frequency_tolerance_percent": frequency_tolerance_percent,
        "duty_tolerance_percent": duty_tolerance_percent,
        "coded_persona_baud_dependent": False,
        "profiles": [asdict(row) for row in rows],
        "reflection_calibration": {
            "burst_program": "tdma_pio_spi_clk_burst",
            "burst_cycles_per_period": burst_high_cycles + burst_low_cycles,
            "burst_high_cycles": burst_high_cycles,
            "burst_low_cycles": burst_low_cycles,
            "burst_profiles": [asdict(row) for row in burst_rows],
            "forward_program": "tdma_pio_spi_clk_forward",
            "forward_edge_regeneration": forward_has_edge_regeneration,
            "forward_frequency_source": "upstream_rx_clock",
            "forward_local_divider": 1.0,
            "electrical_rise_fall_requires_scope": True,
        },
        "passed": (declared_cycles == high_cycles + low_cycles and
                   all(row.frequency_ok and row.duty_ok for row in rows) and
                   burst_high_cycles + burst_low_cycles == 4 and
                   all(row.frequency_ok and row.duty_ok for row in burst_rows) and
                   forward_has_edge_regeneration),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frequency-mhz", action="append", type=int,
                        default=None)
    parser.add_argument("--expected-duty-percent", type=float, default=50.0)
    parser.add_argument("--frequency-tolerance-percent", type=float, default=1.0)
    parser.add_argument("--duty-tolerance-percent", type=float, default=1.0)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    frequencies = args.frequency_mhz or [10, 25, 30]
    report = audit(
        [value * 1_000_000 for value in frequencies],
        args.expected_duty_percent,
        args.frequency_tolerance_percent,
        args.duty_tolerance_percent)
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    print(rendered)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered + "\n", encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
