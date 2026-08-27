#!/usr/bin/env python3
"""Validate and render the Core1 TDMA schedule in clk_sys cycles."""

from __future__ import annotations

import argparse
import ast
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATHS = (
    Path("config/project_config.h"),
    Path("boards/rp2350_trig/inc/board_config.h"),
    Path("components/tdma/inc/tdma_transport_frame.h"),
    Path("components/tdma/inc/tdma_pio_spi_phys.h"),
)
DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_]\w*)\s+(.+?)\s*$")
PHASE_RE = re.compile(
    r"^\s*#define\s+PROJECT_CORE1_PHASE_([A-Z0-9_]+)_START_CYCLE\b",
    re.MULTILINE,
)


@dataclass(frozen=True)
class TdmaCyclePhase:
    name: str
    start_cycle: int
    end_cycle: int
    wcet_cycles: int
    observed_cycles: int | None = None

    @property
    def window_cycles(self) -> int:
        return self.end_cycle - self.start_cycle


@dataclass(frozen=True)
class TdmaCycleSchedule:
    sys_clock_hz: int
    cycle_cycles: int
    spi_baud_hz: int
    spi_cycles_per_bit: int
    wire_max_bytes: int
    wire_max_cycles: int
    phases: tuple[TdmaCyclePhase, ...]


def _join_continuations(text: str) -> str:
    return re.sub(r"\\\r?\n\s*", " ", text)


def _load_defines(root: Path) -> tuple[dict[str, str], str]:
    definitions: dict[str, str] = {}
    project_text = ""
    for relative in CONFIG_PATHS:
        text = (root / relative).read_text(encoding="utf-8")
        if relative == CONFIG_PATHS[0]:
            project_text = text
        for line in _join_continuations(text).splitlines():
            match = DEFINE_RE.match(line)
            if match and "(" not in match.group(1):
                value = match.group(2).split("/*", 1)[0].strip()
                definitions[match.group(1)] = value
    return definitions, project_text


def _normalize_integer_suffixes(expression: str) -> str:
    return re.sub(r"(?<=\d)[uUlL]+\b", "", expression)


def _evaluate(name: str, definitions: dict[str, str], stack: tuple[str, ...] = ()) -> int:
    if name in stack:
        raise ValueError(f"recursive define: {' -> '.join(stack + (name,))}")
    if name not in definitions:
        raise ValueError(f"missing define: {name}")
    expression = _normalize_integer_suffixes(definitions[name])
    tree = ast.parse(expression, mode="eval")

    def visit(node: ast.AST) -> int:
        if isinstance(node, ast.Expression):
            return visit(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.Name):
            return _evaluate(node.id, definitions, stack + (name,))
        if isinstance(node, ast.BinOp):
            left, right = visit(node.left), visit(node.right)
            operations = {
                ast.Add: lambda: left + right,
                ast.Sub: lambda: left - right,
                ast.Mult: lambda: left * right,
                ast.FloorDiv: lambda: left // right,
                ast.Div: lambda: left // right,
                ast.LShift: lambda: left << right,
                ast.RShift: lambda: left >> right,
                ast.BitOr: lambda: left | right,
                ast.BitAnd: lambda: left & right,
            }
            operation = operations.get(type(node.op))
            if operation is not None:
                return operation()
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -visit(node.operand)
        raise ValueError(f"unsupported define expression for {name}: {expression}")

    return visit(tree)


def load_schedule(
    root: Path = ROOT,
    observed: dict[str, int] | None = None,
) -> TdmaCycleSchedule:
    definitions, project_text = _load_defines(root)
    observed = observed or {}
    phase_names = PHASE_RE.findall(project_text)
    phases = tuple(
        TdmaCyclePhase(
            name=phase_name,
            start_cycle=_evaluate(
                f"PROJECT_CORE1_PHASE_{phase_name}_START_CYCLE", definitions),
            end_cycle=_evaluate(
                f"PROJECT_CORE1_PHASE_{phase_name}_END_CYCLE", definitions),
            wcet_cycles=_evaluate(
                f"PROJECT_CORE1_PHASE_{phase_name}_WCET_CYCLES", definitions),
            observed_cycles=observed.get(phase_name),
        )
        for phase_name in phase_names
    )
    sys_clock_hz = _evaluate("BOARD_SYS_CLOCK_HZ", definitions)
    spi_baud_hz = _evaluate("BOARD_TDMA_SPI_BAUD_HZ", definitions)
    wire_max_bytes = (
        _evaluate("TDMA_PIO_SPI_PACKET_HEADER_SIZE", definitions)
        + _evaluate("TDMA_TRANSPORT_SHORT_PACKET_MAX", definitions)
        + _evaluate("TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES", definitions)
    )
    spi_cycles_per_bit = sys_clock_hz // spi_baud_hz
    return TdmaCycleSchedule(
        sys_clock_hz=sys_clock_hz,
        cycle_cycles=_evaluate("PROJECT_CORE1_CYCLE_CYCLES", definitions),
        spi_baud_hz=spi_baud_hz,
        spi_cycles_per_bit=spi_cycles_per_bit,
        wire_max_bytes=wire_max_bytes,
        wire_max_cycles=wire_max_bytes * 8 * spi_cycles_per_bit,
        phases=phases,
    )


def validate_schedule(schedule: TdmaCycleSchedule) -> list[str]:
    errors: list[str] = []
    if schedule.sys_clock_hz % schedule.spi_baud_hz:
        errors.append("SPI baud is not an integer clk_sys cycle divisor")
    if not schedule.phases:
        return errors + ["schedule has no phases"]
    if schedule.phases[0].start_cycle != 0:
        errors.append("first phase must start at cycle 0")
    previous_end = 0
    for phase in schedule.phases:
        if phase.start_cycle != previous_end:
            errors.append(
                f"{phase.name}: start {phase.start_cycle} does not equal "
                f"previous end {previous_end}")
        if phase.end_cycle <= phase.start_cycle:
            errors.append(f"{phase.name}: empty or reversed window")
        if phase.wcet_cycles > phase.window_cycles:
            errors.append(f"{phase.name}: WCET exceeds its own window")
        if (phase.observed_cycles is not None and
                phase.observed_cycles > phase.wcet_cycles):
            errors.append(f"{phase.name}: observed runtime exceeds WCET")
        previous_end = phase.end_cycle
    if previous_end != schedule.cycle_cycles:
        errors.append(
            f"last phase ends at {previous_end}, cycle is {schedule.cycle_cycles}")
    tdma = next((phase for phase in schedule.phases
                 if phase.name == "TDMA"), None)
    if tdma is None:
        errors.append("TDMA phase missing")
    elif schedule.wire_max_cycles > tdma.wcet_cycles:
        errors.append("maximum wire serialization exceeds TDMA WCET")
    return errors


def _cycles_to_us(cycles: int, sys_clock_hz: int) -> float:
    return cycles * 1_000_000.0 / sys_clock_hz


def render_markdown(schedule: TdmaCycleSchedule) -> str:
    errors = validate_schedule(schedule)
    lines = [
        f"cycle={schedule.cycle_cycles} cycles; clk_sys={schedule.sys_clock_hz} Hz; "
        f"wire_max={schedule.wire_max_cycles} cycles; "
        f"status={'FAIL' if errors else 'PASS'}",
        "",
        "| phase | start_cycle | end_cycle | window_cycles | WCET_cycles | "
        "observed_cycles | derived window |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for phase in schedule.phases:
        observed = "-" if phase.observed_cycles is None else str(phase.observed_cycles)
        lines.append(
            f"| {phase.name} | {phase.start_cycle} | {phase.end_cycle} | "
            f"{phase.window_cycles} | {phase.wcet_cycles} | {observed} | "
            f"{_cycles_to_us(phase.window_cycles, schedule.sys_clock_hz):.3f} us |")
    if errors:
        lines.extend(["", *[f"- FAIL: {error}" for error in errors]])
    return "\n".join(lines) + "\n"


def render_svg(schedule: TdmaCycleSchedule) -> str:
    errors = validate_schedule(schedule)
    width, left, right, row_h = 1280, 210, 40, 44
    plot_width = width - left - right
    height = 120 + row_h * len(schedule.phases)
    colors = ("#2563eb", "#0891b2", "#7c3aed", "#c026d3",
              "#db2777", "#ea580c", "#ca8a04", "#16a34a",
              "#0f766e", "#64748b")
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<style>text{font-family:ui-monospace,Consolas,monospace;fill:#172033}'
        '.small{font-size:12px}.label{font-size:14px;font-weight:600}</style>',
        f'<text x="24" y="30" class="label">Core1 TDMA cycle schedule — '
        f'{schedule.cycle_cycles} clk_sys cycles</text>',
        f'<text x="24" y="52" class="small">clk_sys={schedule.sys_clock_hz} Hz; '
        f'max wire={schedule.wire_max_cycles} cycles; '
        f'status={"FAIL" if errors else "PASS"}</text>',
    ]
    for index, phase in enumerate(schedule.phases):
        y = 78 + index * row_h
        x = left + round(phase.start_cycle * plot_width / schedule.cycle_cycles)
        phase_width = max(
            1, round(phase.window_cycles * plot_width / schedule.cycle_cycles))
        wcet_width = max(
            0, round(phase.wcet_cycles * plot_width / schedule.cycle_cycles))
        color = colors[index % len(colors)]
        chunks.extend([
            f'<text x="24" y="{y + 19}" class="label">{escape(phase.name)}</text>',
            f'<rect x="{x}" y="{y}" width="{phase_width}" height="26" '
            f'fill="{color}" opacity="0.22" stroke="{color}"/>',
            f'<rect x="{x}" y="{y}" width="{wcet_width}" height="26" '
            f'fill="{color}" opacity="0.82"/>',
            f'<text x="{x + 5}" y="{y + 18}" class="small">'
            f'{phase.start_cycle}..{phase.end_cycle} / WCET {phase.wcet_cycles}</text>',
        ])
    chunks.append('</svg>')
    return "\n".join(chunks) + "\n"


def _parse_observed(values: list[str]) -> dict[str, int]:
    observed: dict[str, int] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"expected PHASE=CYCLES: {value}")
        name, cycles = value.split("=", 1)
        observed[name.strip().upper()] = int(cycles, 0)
    return observed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--observed", action="append", default=[],
                        metavar="PHASE=CYCLES")
    parser.add_argument("--format", choices=("markdown", "json", "svg"),
                        default="markdown")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        schedule = load_schedule(args.root, _parse_observed(args.observed))
    except (OSError, SyntaxError, ValueError) as error:
        parser.error(str(error))
    if args.format == "json":
        output = json.dumps({
            **asdict(schedule),
            "errors": validate_schedule(schedule),
        }, indent=2) + "\n"
    elif args.format == "svg":
        output = render_svg(schedule)
    else:
        output = render_markdown(schedule)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, encoding="utf-8")
        print(f"out={args.out.resolve()}")
    else:
        print(output, end="")
    return 1 if validate_schedule(schedule) else 0


if __name__ == "__main__":
    raise SystemExit(main())
