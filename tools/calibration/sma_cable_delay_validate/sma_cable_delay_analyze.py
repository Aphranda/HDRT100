#!/usr/bin/env python3
"""Create coarse equal-cable calibration or fit stepped-frequency phase CSV."""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path

CHANNEL_COUNT = 4
FULL_TURN_MDEG = 360_000
HALF_TURN_MDEG = 180_000


@dataclass(frozen=True)
class PhasePoint:
    frequency_hz: int
    phase_mdeg: int


def _wrap_delta(delta: int) -> int:
    while delta > HALF_TURN_MDEG:
        delta -= FULL_TURN_MDEG
    while delta <= -HALF_TURN_MDEG:
        delta += FULL_TURN_MDEG
    return delta


def fit_phase(points: list[PhasePoint]) -> dict[str, int | bool]:
    if len(points) < 3:
        raise ValueError("at least three phase points are required")
    if any(points[index].frequency_hz <= points[index - 1].frequency_hz
           for index in range(1, len(points))):
        raise ValueError("frequencies must be strictly increasing")

    unwrapped = [points[0].phase_mdeg]
    max_step_hz = 0
    for previous, current in zip(points, points[1:]):
        step_hz = current.frequency_hz - previous.frequency_hz
        max_step_hz = max(max_step_hz, step_hz)
        unwrapped.append(unwrapped[-1] +
                         _wrap_delta(current.phase_mdeg - previous.phase_mdeg))

    frequencies = [point.frequency_hz for point in points]
    mean_frequency = sum(frequencies) / len(frequencies)
    mean_phase = sum(unwrapped) / len(unwrapped)
    variance = sum((frequency - mean_frequency) ** 2
                   for frequency in frequencies)
    if variance == 0:
        raise ValueError("frequency variance is zero")
    covariance = sum((frequency - mean_frequency) * (phase - mean_phase)
                     for frequency, phase in zip(frequencies, unwrapped))
    slope = covariance / variance
    intercept = mean_phase - slope * mean_frequency
    residuals = [phase - (intercept + slope * frequency)
                 for frequency, phase in zip(frequencies, unwrapped)]
    return {
        "valid": True,
        "total_delay_ps": round(-slope * 1_000_000_000_000 / FULL_TURN_MDEG),
        "intercept_mdeg": round(intercept),
        "phase_rms_mdeg": round(math.sqrt(sum(value * value
                                                for value in residuals) /
                                             len(residuals))),
        "max_unambiguous_delay_ps": 500_000_000_000 // max_step_hz,
        "point_count": len(points),
    }


def load_phase_csv(path: Path) -> dict[int, list[PhasePoint]]:
    channels: dict[int, list[PhasePoint]] = {index: []
                                                   for index in range(CHANNEL_COUNT)}
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            channel = int(row["channel"])
            if channel < 0 or channel >= CHANNEL_COUNT:
                raise ValueError(f"channel out of range: {channel}")
            channels[channel].append(PhasePoint(int(row["frequency_hz"]),
                                                int(row["phase_mdeg"])))
    return channels


def write_svg(path: Path, fits: list[dict[str, int | bool]],
              title: str) -> None:
    width, height = 920, 360
    delays = [int(fit["total_delay_ps"]) / 1000.0 for fit in fits]
    minimum = min(delays)
    maximum = max(delays)
    span = max(maximum - minimum, 1.0)
    plot_left, plot_right = 90, 870
    colors = ["#3b82f6", "#10b981", "#f59e0b", "#ef4444"]
    bars = []
    for channel, delay_ns in enumerate(delays):
        x = plot_left + channel * 190
        bar_height = 40 + (delay_ns - minimum) / span * 180
        y = 290 - bar_height
        bars.append(
            f'<rect x="{x}" y="{y:.1f}" width="110" height="{bar_height:.1f}" '
            f'fill="{colors[channel]}" rx="5"/>'
            f'<text x="{x + 55}" y="315" text-anchor="middle">NO{channel + 1}</text>'
            f'<text x="{x + 55}" y="{y - 8:.1f}" text-anchor="middle">'
            f'{delay_ns:.3f} ns</text>')
    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">'
        '<rect width="100%" height="100%" fill="#ffffff"/>'
        f'<text x="460" y="32" text-anchor="middle" font-size="20">{title}</text>'
        '<line x1="70" y1="290" x2="890" y2="290" stroke="#334155"/>'
        + "".join(bars) + '</svg>')
    path.write_text(svg, encoding="utf-8")


def parse_residuals(value: str) -> list[int]:
    parts = [item.strip() for item in value.split(",")]
    if len(parts) != CHANNEL_COUNT:
        raise argparse.ArgumentTypeError("exactly four comma-separated values required")
    return [round(float(item) * 1000.0) for item in parts]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path,
                        help="CSV columns: channel,frequency_hz,phase_mdeg")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--coarse-equal", action="store_true")
    parser.add_argument("--common-delay-ns", type=float)
    parser.add_argument("--relative-delay-ns", type=parse_residuals,
                        default=[0, 0, 0, 0])
    parser.add_argument("--velocity-factor", type=float)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    output: dict[str, object]
    if args.coarse_equal:
        common_valid = args.common_delay_ns is not None
        velocity_valid = args.velocity_factor is not None
        if args.velocity_factor is not None and not 0.0 < args.velocity_factor <= 1.0:
            raise ValueError("velocity factor must be in (0, 1]")
        output = {
            "schema": "sma-cable-delay/coarse-v1",
            "mode": "equal-cable-relative-baseline",
            "common_cable_delay_ps": (round(args.common_delay_ns * 1000.0)
                                      if common_valid else 0),
            "common_cable_delay_valid": common_valid,
            "velocity_factor_ppm": (round(args.velocity_factor * 1_000_000)
                                    if velocity_valid else 0),
            "velocity_factor_valid": velocity_valid,
            "channel_relative_delay_ps": args.relative_delay_ns,
            "relative_only": not common_valid,
            "usage": "validator-only; never load into TDMA loop/link offset matrix",
        }
    else:
        if args.input is None:
            parser.error("--input is required unless --coarse-equal is used")
        channels = load_phase_csv(args.input)
        fits = [fit_phase(channels[channel]) for channel in range(CHANNEL_COUNT)]
        reference_ps = int(fits[0]["total_delay_ps"])
        output = {
            "schema": "sma-cable-delay/phase-slope-v1",
            "mode": "measured-path-delay",
            "channels": [dict(fit,
                              channel=channel,
                              relative_delay_ps=int(fit["total_delay_ps"]) -
                                                reference_ps)
                         for channel, fit in enumerate(fits)],
            "measurement_boundary": (
                "source output phase + cable + validator input channel; "
                "cable-only delay requires self-loop fixed-channel subtraction"),
        }
        write_svg(args.output_dir / "sma_cable_delay.svg", fits,
                  "SMA measured path delay")

    result_path = args.output_dir / "sma_cable_delay.json"
    result_path.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n",
                           encoding="utf-8")
    print(result_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
