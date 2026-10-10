#!/usr/bin/env python3
"""Capture four-board DPLL pulses from a Rigol HDO scope.

The board-side DPLL/TDMA state is intentionally untouched.  This tool only
configures the oscilloscope trigger and reads waveform bytes over VISA, then
stores a compact JSON/SVG evidence pair with per-channel edge timing.
"""

from __future__ import annotations

import argparse
import json
import math
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_RESOURCE = "USB0::0x1AB1::0x0610::HDO4A244301137::INSTR"


def _resource(resource_name: str, timeout_ms: int):
    try:
        import pyvisa
    except ImportError as exc:  # pragma: no cover - exercised on operator host
        raise SystemExit("pyvisa is required: python -m pip install pyvisa") from exc
    scope = pyvisa.ResourceManager().open_resource(resource_name)
    scope.timeout = timeout_ms
    return scope


def _read(scope) -> bytes:
    return scope.read_raw()


def _query(scope, command: str) -> str:
    scope.write(command)
    return _read(scope).decode("ascii", errors="replace").strip()


def _decode_block(raw: bytes) -> bytes:
    if not raw.startswith(b"#") or len(raw) < 2:
        raise ValueError(f"invalid SCPI binary block header: {raw[:16]!r}")
    digits = int(raw[1:2])
    if digits == 0 or len(raw) < 2 + digits:
        raise ValueError("invalid SCPI binary block length")
    length = int(raw[2:2 + digits])
    start = 2 + digits
    if len(raw) < start + length:
        raise ValueError(f"truncated waveform block {len(raw) - start}/{length}")
    return raw[start:start + length]


def _preamble(scope) -> list[float]:
    fields = [item.strip() for item in _query(scope, ":WAV:PRE?").split(",")]
    if len(fields) < 10:
        raise ValueError(f"unexpected waveform preamble: {fields!r}")
    return [float(item) for item in fields]


def _capture_channel(scope, channel: int, threshold_v: float) -> dict[str, Any]:
    scope.write(f":WAV:SOUR CHAN{channel}")
    scope.write(":WAV:MODE NORM")
    scope.write(":WAV:FORM BYTE")
    scope.write(":WAV:POIN:MODE NORM")
    preamble = _preamble(scope)
    scope.write(":WAV:DATA?")
    samples = list(_decode_block(_read(scope)))
    x_increment = preamble[4]
    x_origin = preamble[5]
    y_increment = preamble[7]
    y_origin = preamble[8]
    y_reference = preamble[9]
    volts = [
        (sample - y_reference) * y_increment + y_origin
        for sample in samples
    ]
    rising_edges: list[float] = []
    for index in range(1, len(volts)):
        if volts[index - 1] < threshold_v <= volts[index]:
            rising_edges.append(x_origin + index * x_increment)
    minimum = min(volts) if volts else math.nan
    maximum = max(volts) if volts else math.nan
    return {
        "channel": channel,
        "sample_count": len(samples),
        "sample_interval_ns": x_increment * 1e9,
        "voltage_min_v": minimum,
        "voltage_max_v": maximum,
        "voltage_pp_v": maximum - minimum if volts else math.nan,
        "rising_edges_ns": [edge * 1e9 for edge in rising_edges],
        "rising_edge_count": len(rising_edges),
        "samples_v": volts,
    }


def _write_svg(path: Path, channels: list[dict[str, Any]], trigger_level: float) -> None:
    width, height = 1100, 180 + 150 * len(channels)
    all_values = [value for row in channels for value in row["samples_v"]]
    lo = min(all_values) if all_values else -1.0
    hi = max(all_values) if all_values else 1.0
    span = max(hi - lo, 1e-9)
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{width}" height="{height}">',
        '<style>text{font-family:sans-serif;font-size:12px;fill:#222}'
        'polyline{fill:none;stroke-width:1.5}</style>',
        '<text x="20" y="24" style="font-size:17px;font-weight:bold">'
        'DPLL scope capture</text>',
        f'<text x="20" y="44">trigger level={trigger_level:g} V; '
        f'channels={len(channels)}</text>',
    ]
    colors = ["#1565c0", "#2e7d32", "#ef6c00", "#6a1b9a"]
    for row, channel in enumerate(channels):
        top = 65 + 150 * row
        plot_x, plot_y, plot_w, plot_h = 70, top + 20, 980, 100
        lines.append(f'<text x="20" y="{top + 35}">CH{channel["channel"]} '
                     f'{channel["voltage_pp_v"]:.3f} Vpp, '
                     f'{channel["rising_edge_count"]} rising edge(s)</text>')
        lines.append(f'<rect x="{plot_x}" y="{plot_y}" width="{plot_w}" '
                     f'height="{plot_h}" fill="#f7f7f7" stroke="#bbb"/>')
        points = []
        values = channel["samples_v"]
        for index, value in enumerate(values):
            x = plot_x + plot_w * index / max(1, len(values) - 1)
            y = plot_y + plot_h * (hi - value) / span
            points.append(f"{x:.2f},{y:.2f}")
        if points:
            lines.append(f'<polyline points="{" ".join(points)}" '
                         f'stroke="{colors[row % len(colors)]}"/>')
        threshold_y = plot_y + plot_h * (hi - trigger_level) / span
        lines.append(f'<line x1="{plot_x}" y1="{threshold_y:.2f}" '
                     f'x2="{plot_x + plot_w}" y2="{threshold_y:.2f}" '
                     'stroke="#c62828" stroke-dasharray="4 3"/>')
    lines.append(f'<text x="20" y="{height - 12}" font-size="10">'
                 f'generated={datetime.now(timezone.utc).isoformat()}</text>')
    lines.append("</svg>")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resource", default=DEFAULT_RESOURCE)
    parser.add_argument("--channels", nargs="+", type=int, default=[1, 2, 3, 4])
    parser.add_argument("--trigger-channel", type=int, default=1)
    parser.add_argument("--trigger-level-v", type=float, default=2.5)
    parser.add_argument("--scale-v-per-div", type=float, default=2.0)
    parser.add_argument("--time-scale-s-per-div", type=float, default=1e-6)
    parser.add_argument("--timeout-ms", type=int, default=10000)
    parser.add_argument("--settle-s", type=float, default=0.5)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--single", action="store_true",
                        help="use one normal-trigger acquisition; default is continuous RUN")
    return parser.parse_args()


def run(args: argparse.Namespace) -> dict[str, Any]:
    if not args.channels or any(channel < 1 or channel > 4 for channel in args.channels):
        raise ValueError("channels must be in the range 1..4")
    if args.trigger_channel not in args.channels:
        raise ValueError("trigger channel must be included in --channels")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    scope = _resource(args.resource, args.timeout_ms)
    try:
        identity = _query(scope, "*IDN?")
        commands = [
            f":CHAN{channel}:DISP 1" for channel in args.channels
        ]
        commands.extend(
            f":CHAN{channel}:SCAL {args.scale_v_per_div:g}"
            for channel in args.channels
        )
        commands.extend([
            f":TIM:SCAL {args.time_scale_s_per_div:g}",
            ":TRIG:MODE EDGE",
            f":TRIG:EDGE:SOUR CHAN{args.trigger_channel}",
            ":TRIG:EDGE:SLOP POS",
            f":TRIG:EDGE:LEV {args.trigger_level_v:g}",
            ":TRIG:SWE NORM",
            ":SING" if args.single else ":RUN",
        ])
        for command in commands:
            scope.write(command)
        time.sleep(args.settle_s)
        trigger_status = _query(scope, ":TRIG:STAT?")
        channels = [
            _capture_channel(scope, channel, args.trigger_level_v)
            for channel in args.channels
        ]
    finally:
        scope.close()
    result = {
        "schema": "HAOFV_SCOPE_DPLL_CAPTURE_V1",
        "resource": args.resource,
        "identity": identity,
        "trigger_status": trigger_status,
        "trigger_channel": args.trigger_channel,
        "trigger_level_v": args.trigger_level_v,
        "scale_v_per_div": args.scale_v_per_div,
        "time_scale_s_per_div": args.time_scale_s_per_div,
        "continuous": not args.single,
        "channels": channels,
        "passed": trigger_status in {"TD", "STOP"} and all(
            channel["sample_count"] > 0 for channel in channels),
    }
    svg_path = args.out_dir / "waveform.svg"
    _write_svg(svg_path, channels, args.trigger_level_v)
    for channel in channels:
        channel.pop("samples_v", None)
    result["waveform_svg"] = str(svg_path)
    (args.out_dir / "summary.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> int:
    args = parse_args()
    result = run(args)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
