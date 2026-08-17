#!/usr/bin/env python3
"""Read-only TDMA ring runtime monitor (P0.5-6).

Monitors SYSTem:REFMEM:SYNC:TDMA:STATus? on both minimum-system boards for a
long window (default 300 s) and reports whether the resident ring is up
(up_running / down_running / ring_seq / simultaneous_feedback_loop_evidence).

HAOFV boundary: this tool is strictly read-only. It never arms the ring, never
submits TX/RX maintenance frames and never participates in window renewal --
the resident ring is driven entirely by firmware (core1 TDMA service). Only
'?' queries are sent.

Acceptance (docs/tdma/TDMA_DOMAIN_TODO.md P0.5-6):
  up_running=1, down_running=1, simultaneous_feedback_loop_evidence=1 on the
  reference board (local_slot == reference_slot; forward nodes keep evidence 0
  by architecture), ring_last_error stable (no ADAPTER_MISSING / BAD_CONFIG /
  RESOURCE_CONFLICT), no BAD_FRAME growth, WINDOW_BOUND not a final state.
  Outputs summary + SVG under docs/temp/vdc_long_monitor/ by default.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from contextlib import ExitStack
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from scpi_common.scpi_serial import open_serial_port, read_serial_line_idle  # noqa: E402

TDMA_STATUS_FIELD_COUNT = 107

RING_ENABLED = 54
RING_NODE_COUNT = 56
RING_LOCAL_SLOT = 57
RING_REFERENCE_SLOT = 58
RING_UP_RUNNING = 63
RING_DOWN_RUNNING = 64
RING_SEQ = 65
RING_LAST_ERROR = 66
SIMULTANEOUS = 67
RING_FEEDBACK_TIMEOUT_NS = 88
RING_ADAPTER_STARTED = 89
RING_ADAPTER_START_COUNT = 90
RING_ADAPTER_STOP_COUNT = 91
RING_ADAPTER_SERVICE_COUNT = 92
RING_UP_TX_SEQUENCE = 93
RING_DOWN_RX_SEQUENCE = 94
RING_UP_TX_FRAME_CRC = 95
RING_DOWN_RX_FRAME_CRC = 96
RING_TS_RESOLUTION_NS = 97
RING_TS_FLAGS = 98
RING_IDLE_TX = 99
RING_IDLE_RX = 100
RING_ROUND_TRIP_NS = 101
RING_REF_TX_TS_LO = 102
RING_REF_TX_TS_HI = 103
RING_FB_RX_TS_LO = 104
RING_FB_RX_TS_HI = 105
RING_ADAPTER_LAST_ERROR = 106

REASON_NAMES = {
    0: "NONE",
    1: "BAD_CONFIG",
    2: "EVIDENCE_MISSING",
    3: "DIRECTION_CONFLICT",
    4: "ADAPTER_MISSING",
    5: "TIMESTAMP_MISSING",
    6: "PAYLOAD_STARVATION",
    7: "WINDOW_MISSED",
    8: "RESOURCE_CONFLICT",
}

PLOT_ARCHIVE_ROOT = ROOT / "docs" / "temp" / "vdc_long_monitor"
SUMMARY_SVG_NAME = "tdma_ring_quality_summary.svg"


@dataclass
class BoardMonitor:
    name: str
    port: str
    build: str
    samples: list[dict[str, Any]]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--name-a", default="COM5")
    parser.add_argument("--name-b", default="COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--duration-s", type=float, default=300.0)
    parser.add_argument("--poll-interval-s", type=float, default=1.0)
    parser.add_argument("--expected-build")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--plot-archive-dir", type=Path, default=PLOT_ARCHIVE_ROOT)
    parser.add_argument("--no-plot-archive", action="store_true")
    return parser.parse_args()


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def trim_embedded_log(line: str) -> str:
    match = re.search(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+', line)
    return line[:match.start()].strip() if match else line


def strip_leading_ack(line: str) -> str:
    if line in ('"OK"', "OK"):
        return line
    if line.startswith('"OK[') or line.startswith("OK["):
        return ""
    if line.startswith('"OK"['):
        return line[4:].strip()
    if line.startswith('OK"['):
        return line[3:].strip()
    return line


def query(ser, command: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or is_log_line(line):
            continue
        line = strip_leading_ack(trim_embedded_log(line))
        if line:
            return line
    return "<timeout>"


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def tdma_status_fields(response: str) -> list[int]:
    fields = parse_csv_response(response)
    if len(fields) != TDMA_STATUS_FIELD_COUNT:
        raise AssertionError(
            f"field count {len(fields)} != {TDMA_STATUS_FIELD_COUNT}: {response}")
    try:
        return [int(field.strip().strip('"'), 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"non-integer TDMA status: {response}") from exc


def timestamp_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def sample_board(ser, timeout_s: float) -> list[int]:
    return tdma_status_fields(query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s))


def monitor_board(name: str, ser, args: argparse.Namespace) -> BoardMonitor:
    build = query(ser, "SYST:FW:BUILD?", args.timeout)
    if args.expected_build and build != f'"{args.expected_build}"':
        raise AssertionError(f"{name}: build mismatch {build} != {args.expected_build}")

    samples: list[dict[str, Any]] = []
    deadline = time.monotonic() + args.duration_s
    while time.monotonic() < deadline:
        try:
            fields = sample_board(ser, args.timeout)
        except AssertionError:
            fields = []
        sample = {
            "ts_iso": timestamp_iso(),
            "elapsed_s": round(args.duration_s - max(0.0, deadline - time.monotonic()), 3),
            "ok": bool(fields),
            "fields": fields,
        }
        samples.append(sample)
        time.sleep(args.poll_interval_s)
    return BoardMonitor(name=name, port=args.port_a if name == args.name_a else args.port_b,
                        build=build, samples=samples)


def field(sample: dict[str, Any], index: int) -> int:
    if not sample.get("ok") or not sample["fields"]:
        return -1
    if index >= len(sample["fields"]):
        return -1
    return sample["fields"][index]


def last_valid(board: BoardMonitor, index: int) -> int:
    for sample in reversed(board.samples):
        value = field(sample, index)
        if value >= 0:
            return value
    return -1


def is_reference_board(board: BoardMonitor) -> bool:
    local = last_valid(board, RING_LOCAL_SLOT)
    ref = last_valid(board, RING_REFERENCE_SLOT)
    return local >= 0 and local == ref


def board_result(name: str, board: BoardMonitor) -> dict[str, Any]:
    ok_samples = [s for s in board.samples if s["ok"]]
    up = last_valid(board, RING_UP_RUNNING)
    down = last_valid(board, RING_DOWN_RUNNING)
    simultaneous = last_valid(board, SIMULTANEOUS)
    last_error = last_valid(board, RING_LAST_ERROR)
    ring_seq = last_valid(board, RING_SEQ)
    adapter_started = last_valid(board, RING_ADAPTER_STARTED)
    service_count = last_valid(board, RING_ADAPTER_SERVICE_COUNT)
    idle_tx = last_valid(board, RING_IDLE_TX)
    idle_rx = last_valid(board, RING_IDLE_RX)
    round_trip = last_valid(board, RING_ROUND_TRIP_NS)
    resolution = last_valid(board, RING_TS_RESOLUTION_NS)

    # BAD_FRAME-equivalent growth on the ring: adapter last error and reason
    # must not settle into a hard fault; WINDOW_MISSED as a final state is
    # also a failure (P0.5-6: "WINDOW_BOUND 不作为最终态").
    hard_reasons = {1, 3, 4, 8}  # BAD_CONFIG / DIRECTION_CONFLICT / ADAPTER_MISSING / RESOURCE_CONFLICT
    final_reason_ok = last_error in (0, 2, 5) or last_error == 7  # WINDOW_MISSED tolerated only transiently

    reference = is_reference_board(board)
    up_ok = up == 1
    down_ok = down == 1
    simultaneous_ok = (simultaneous == 1) if reference else True
    reason_ok = last_error not in hard_reasons
    seq_advances = ring_seq > 0
    resolution_ok = resolution == 0 or resolution <= 100

    passed = (up_ok and down_ok and simultaneous_ok and reason_ok and
              seq_advances and resolution_ok and final_reason_ok)
    reasons: list[str] = []
    if not up_ok:
        reasons.append("up_running!=1")
    if not down_ok:
        reasons.append("down_running!=1")
    if not simultaneous_ok:
        reasons.append("simultaneous_evidence!=1 (reference board)")
    if not reason_ok:
        reasons.append(f"hard ring reason {REASON_NAMES.get(last_error, last_error)}")
    if not seq_advances:
        reasons.append("ring_seq==0")
    if not resolution_ok:
        reasons.append(f"timestamp resolution {resolution} ns > 100 ns")
    if not final_reason_ok:
        reasons.append("final reason not stable")

    return {
        "name": board.name,
        "port": board.port,
        "build": board.build,
        "reference_board": reference,
        "sample_count": len(board.samples),
        "ok_sample_count": len(ok_samples),
        "up_running": up,
        "down_running": down,
        "simultaneous_feedback_loop_evidence": simultaneous,
        "ring_seq": ring_seq,
        "ring_last_error": last_error,
        "ring_last_error_name": REASON_NAMES.get(last_error, str(last_error)),
        "adapter_started": adapter_started,
        "adapter_service_count": service_count,
        "idle_beacon_tx_count": idle_tx,
        "idle_beacon_rx_count": idle_rx,
        "feedback_round_trip_ns": round_trip,
        "timestamp_resolution_ns": resolution,
        "passed": passed,
        "reasons": reasons,
    }


def write_svg(path: Path, boards: list[BoardMonitor], results: list[dict[str, Any]],
              duration_s: float) -> None:
    width = 900
    height = 300 + 90 * len(boards)
    lines: list[str] = []
    lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">')
    lines.append("<style>text{font-family:sans-serif;font-size:12px;fill:#222}</style>")
    lines.append(f'<text x="20" y="26" style="font-size:16px;font-weight:bold">'
                 f'TDMA ring runtime 5-min monitor</text>')

    for row, (board, result) in enumerate(zip(boards, results)):
        base_y = 60 + row * 90
        ok = result["passed"]
        color = "#1a7f37" if ok else "#c62828"
        lines.append(f'<text x="20" y="{base_y}" style="font-weight:bold">'
                     f'{board.name} ({board.port}) {"PASS" if ok else "FAIL"}</text>')
        if result["reasons"]:
            lines.append(f'<text x="20" y="{base_y + 16}">' +
                         " ; ".join(result["reasons"]) + "</text>")

        plot_x = 20
        plot_y = base_y + 30
        plot_w = width - 40
        plot_h = 40
        lines.append(f'<rect x="{plot_x}" y="{plot_y}" width="{plot_w}" height="{plot_h}" '
                     f'fill="#f4f4f4" stroke="#ccc"/>')

        n = len(board.samples)
        for index, label, color_line in (
                (RING_UP_RUNNING, "up", "#1a7f37"),
                (RING_DOWN_RUNNING, "down", "#1976d2"),
                (SIMULTANEOUS, "evid", "#b8860b")):
            points: list[str] = []
            for i, sample in enumerate(board.samples):
                value = field(sample, index)
                if value < 0:
                    continue
                x = plot_x + (plot_w * i / max(1, n - 1))
                y = plot_y + plot_h - (plot_h * min(1, value))
                points.append(f"{x:.1f},{y:.1f}")
            if points:
                lines.append(f'<polyline points="{" ".join(points)}" fill="none" '
                             f'stroke="{color_line}" stroke-width="1.5"/>')
                lines.append(f'<text x="{plot_x + plot_w + 6}" y="{plot_y + 10}" '
                             f'fill="{color_line}">{label}</text>')

        lines.append(f'<text x="{plot_x}" y="{plot_y + plot_h + 16}">'
                     f'ring_seq={result["ring_seq"]} reason={result["ring_last_error_name"]} '
                     f'beacon_tx={result["idle_beacon_tx_count"]} '
                     f'beacon_rx={result["idle_beacon_rx_count"]} '
                     f'round_trip_ns={result["feedback_round_trip_ns"]} '
                     f'resolution_ns={result["timestamp_resolution_ns"]}</text>')

    lines.append(f'<text x="20" y="{height - 12}" style="font-size:10px">'
                 f'duration_s={duration_s} '
                 f'tool=tdma_ring_monitor read-only '
                 f'ts={timestamp_iso()}</text>')
    lines.append("</svg>")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    out_dir = args.out_dir or (
        ROOT / "build-rtos-multicore-smoke" /
        f"tdma_ring_monitor_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    with ExitStack() as stack:
        ser_a = stack.enter_context(open_serial_port(args.port_a,
                                                     args.baud,
                                                     args.timeout,
                                                     args.settle))
        ser_b = stack.enter_context(open_serial_port(args.port_b,
                                                     args.baud,
                                                     args.timeout,
                                                     args.settle))
        boards = [
            monitor_board(args.name_a, ser_a, args),
            monitor_board(args.name_b, ser_b, args),
        ]

    results = [board_result(board.name, board) for board in boards]
    passed = all(result["passed"] for result in results)

    summary = {
        "passed": passed,
        "duration_s": args.duration_s,
        "poll_interval_s": args.poll_interval_s,
        "ports": {args.name_a: args.port_a, args.name_b: args.port_b},
        "results": results,
        "note": "read-only TDMA ring monitor; firmware owns the resident ring",
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                          encoding="utf-8")

    lines = ["TDMA ring runtime monitor summary",
             f"duration_s={args.duration_s} passed={passed}",
             ""]
    for result in results:
        lines.append(
            f"{'PASS' if result['passed'] else 'FAIL'} {result['name']} {result['port']} "
            f"reference={result['reference_board']} "
            f"up={result['up_running']} down={result['down_running']} "
            f"simultaneous={result['simultaneous_feedback_loop_evidence']} "
            f"ring_seq={result['ring_seq']} reason={result['ring_last_error_name']} "
            f"adapter_started={result['adapter_started']} "
            f"service_count={result['adapter_service_count']} "
            f"beacon_tx={result['idle_beacon_tx_count']} beacon_rx={result['idle_beacon_rx_count']} "
            f"round_trip_ns={result['feedback_round_trip_ns']} "
            f"resolution_ns={result['timestamp_resolution_ns']}")
        if result["reasons"]:
            lines.append(f"    reasons: {'; '.join(result['reasons'])}")
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    with open(out_dir / "samples.csv", "w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(["board", "ts_iso", "elapsed_s", "ok",
                         "ring_enabled", "up_running", "down_running", "ring_seq",
                         "ring_last_error", "simultaneous", "adapter_service_count",
                         "idle_beacon_tx", "idle_beacon_rx", "round_trip_ns",
                         "up_tx_sequence", "down_rx_sequence", "ts_resolution_ns"])
        for board in boards:
            for sample in board.samples:
                writer.writerow([
                    board.name, sample["ts_iso"], sample["elapsed_s"],
                    int(sample["ok"]),
                    field(sample, RING_ENABLED),
                    field(sample, RING_UP_RUNNING),
                    field(sample, RING_DOWN_RUNNING),
                    field(sample, RING_SEQ),
                    field(sample, RING_LAST_ERROR),
                    field(sample, SIMULTANEOUS),
                    field(sample, RING_ADAPTER_SERVICE_COUNT),
                    field(sample, RING_IDLE_TX),
                    field(sample, RING_IDLE_RX),
                    field(sample, RING_ROUND_TRIP_NS),
                    field(sample, RING_UP_TX_SEQUENCE),
                    field(sample, RING_DOWN_RX_SEQUENCE),
                    field(sample, RING_TS_RESOLUTION_NS),
                ])

    svg_path = out_dir / SUMMARY_SVG_NAME
    write_svg(svg_path, boards, results, args.duration_s)
    archive_dir: Path | None = None
    if not args.no_plot_archive:
        # Mirror into a stable docs/temp archive, same layout as the VDC
        # long monitor: docs/temp/vdc_long_monitor/<out_dir.name>/
        archive_dir = args.plot_archive_dir / out_dir.name
        archive_dir.mkdir(parents=True, exist_ok=True)
        (archive_dir / SUMMARY_SVG_NAME).write_text(
            svg_path.read_text(encoding="utf-8"), encoding="utf-8")
        (archive_dir / "summary.json").write_text(
            json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        (archive_dir / "summary.txt").write_text("\n".join(lines) + "\n",
                                                encoding="utf-8")

    print("\n".join(lines))
    if archive_dir is not None:
        print(f"plot_archive_dir={archive_dir}")
    print(f"summary: passed={passed} out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
