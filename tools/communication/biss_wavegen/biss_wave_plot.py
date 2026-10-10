#!/usr/bin/env python3
"""Render BiSS-C TAP CLK/DATA CSV vectors as a self-contained SVG."""

from __future__ import annotations

import argparse
import csv
import html
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Sample:
    time_ns: int
    clk: int
    data: int
    frame_index: int
    bit_index: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, nargs="?", default=Path("build/biss_wavegen/biss_1mhz.csv"))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=520)
    parser.add_argument("--title", default="BiSS-C TAP CSV Waveform")
    parser.add_argument("--max-label-bits", type=int, default=96)
    return parser.parse_args()


def read_samples(path: Path) -> list[Sample]:
    samples: list[Sample] = []
    with path.open("r", encoding="utf-8", newline="") as input_file:
        reader = csv.DictReader(input_file)
        for row in reader:
            samples.append(
                Sample(
                    time_ns=int(row["time_ns"]),
                    clk=int(row["clk"]),
                    data=int(row["data"]),
                    frame_index=int(row["frame_index"]),
                    bit_index=int(row["bit_index"]),
                )
            )
    return samples


def x_for_time(time_ns: int, start_ns: int, span_ns: int, left: int, plot_width: int) -> float:
    if span_ns <= 0:
        return float(left)
    return left + ((time_ns - start_ns) / span_ns) * plot_width


def y_for_level(level: int, base_y: int, amplitude: int) -> int:
    return base_y - amplitude if level else base_y


def step_path(samples: list[Sample],
              signal: str,
              start_ns: int,
              span_ns: int,
              left: int,
              plot_width: int,
              base_y: int,
              amplitude: int) -> str:
    if not samples:
        return ""
    value = getattr(samples[0], signal)
    x = x_for_time(samples[0].time_ns, start_ns, span_ns, left, plot_width)
    y = y_for_level(value, base_y, amplitude)
    parts = [f"M {x:.2f} {y}"]
    for sample in samples[1:]:
        next_value = getattr(sample, signal)
        next_x = x_for_time(sample.time_ns, start_ns, span_ns, left, plot_width)
        parts.append(f"L {next_x:.2f} {y}")
        y = y_for_level(next_value, base_y, amplitude)
        parts.append(f"L {next_x:.2f} {y}")
    return " ".join(parts)


def frame_ranges(samples: list[Sample]) -> list[tuple[int, int, int]]:
    ranges: dict[int, list[int]] = {}
    for sample in samples:
        if sample.frame_index < 0:
            continue
        values = ranges.setdefault(sample.frame_index, [sample.time_ns, sample.time_ns])
        values[0] = min(values[0], sample.time_ns)
        values[1] = max(values[1], sample.time_ns)
    return [(frame, values[0], values[1]) for frame, values in sorted(ranges.items())]


def bit_labels(samples: list[Sample],
               max_labels: int) -> list[tuple[int, int, int]]:
    seen: dict[tuple[int, int], int] = {}
    for sample in samples:
        if sample.frame_index < 0 or sample.bit_index < 0:
            continue
        seen.setdefault((sample.frame_index, sample.bit_index), sample.time_ns)
    items = [(frame, bit, time_ns) for (frame, bit), time_ns in sorted(seen.items())]
    if len(items) <= max_labels:
        return items
    step = max(1, len(items) // max_labels)
    return [item for index, item in enumerate(items) if index % step == 0]


def nice_time_label(time_ns: int) -> str:
    if time_ns >= 1_000_000:
        return f"{time_ns / 1_000_000:.3f} ms"
    if time_ns >= 1_000:
        return f"{time_ns / 1_000:.3f} us"
    return f"{time_ns} ns"


def render_svg(samples: list[Sample], args: argparse.Namespace) -> str:
    width = args.width
    height = args.height
    left = 92
    right = 36
    top = 58
    bottom = 54
    plot_width = width - left - right
    plot_height = height - top - bottom
    start_ns = samples[0].time_ns
    end_ns = samples[-1].time_ns
    span_ns = max(1, end_ns - start_ns)
    clk_base = top + 130
    data_base = top + 300
    amplitude = 74
    frame_fill = ("#e8f4ff", "#fff7df")

    elements: list[str] = []
    elements.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">')
    elements.append("<style>")
    elements.append("text{font-family:Segoe UI,Arial,sans-serif;fill:#1f2937} .small{font-size:12px}.label{font-size:14px;font-weight:600}.title{font-size:20px;font-weight:700}.axis{stroke:#9ca3af;stroke-width:1}.grid{stroke:#e5e7eb;stroke-width:1}.wave{fill:none;stroke-linejoin:round;stroke-linecap:round;stroke-width:2.5}.clk{stroke:#2563eb}.data{stroke:#dc2626}")
    elements.append("</style>")
    elements.append('<rect width="100%" height="100%" fill="#ffffff"/>')
    elements.append(f'<text x="{left}" y="30" class="title">{html.escape(args.title)}</text>')
    elements.append(f'<text x="{left}" y="49" class="small">{html.escape(str(args.csv))} | {len(samples)} samples | {nice_time_label(span_ns)} span</text>')

    for frame, start, end in frame_ranges(samples):
        x = x_for_time(start, start_ns, span_ns, left, plot_width)
        x2 = x_for_time(end, start_ns, span_ns, left, plot_width)
        fill = frame_fill[frame % len(frame_fill)]
        elements.append(f'<rect x="{x:.2f}" y="{top}" width="{max(1.0, x2 - x):.2f}" height="{plot_height}" fill="{fill}" opacity="0.55"/>')
        elements.append(f'<text x="{x + 4:.2f}" y="{top + 18}" class="small">frame {frame}</text>')

    for tick in range(11):
        x = left + (plot_width * tick / 10)
        time_ns = start_ns + int(span_ns * tick / 10)
        elements.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{height - bottom}" class="grid"/>')
        elements.append(f'<text x="{x - 18:.2f}" y="{height - 24}" class="small">{nice_time_label(time_ns - start_ns)}</text>')

    for y in (clk_base, clk_base - amplitude, data_base, data_base - amplitude):
        elements.append(f'<line x1="{left}" y1="{y}" x2="{width - right}" y2="{y}" class="axis"/>')

    elements.append(f'<text x="28" y="{clk_base - amplitude / 2:.0f}" class="label">CLK</text>')
    elements.append(f'<text x="28" y="{data_base - amplitude / 2:.0f}" class="label">DATA</text>')
    elements.append(f'<path d="{step_path(samples, "clk", start_ns, span_ns, left, plot_width, clk_base, amplitude)}" class="wave clk"/>')
    elements.append(f'<path d="{step_path(samples, "data", start_ns, span_ns, left, plot_width, data_base, amplitude)}" class="wave data"/>')

    for frame, bit, time_ns in bit_labels(samples, args.max_label_bits):
        x = x_for_time(time_ns, start_ns, span_ns, left, plot_width)
        elements.append(f'<line x1="{x:.2f}" y1="{data_base + 8}" x2="{x:.2f}" y2="{data_base + 18}" stroke="#6b7280" stroke-width="1"/>')
        elements.append(f'<text x="{x - 4:.2f}" y="{data_base + 34}" class="small">{bit}</text>')

    elements.append(f'<text x="{left}" y="{height - 8}" class="small">Bit labels show sampled bit_index positions. Colored bands show frame_index regions.</text>')
    elements.append("</svg>")
    return "\n".join(elements) + "\n"


def main() -> int:
    args = parse_args()
    samples = read_samples(args.csv)
    if not samples:
        raise SystemExit(f"no samples found in {args.csv}")
    out_path = args.out or args.csv.with_suffix(".svg")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(render_svg(samples, args), encoding="utf-8", newline="\n")
    print(f"svg={out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
