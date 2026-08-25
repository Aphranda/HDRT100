#!/usr/bin/env python3
"""SCPI-select and HIL-test frozen TDMA operating levels by ``*IDN?``."""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from scpi_common.board_identity import parse_idn_response  # noqa: E402
from scpi_common.scpi_serial import read_scpi_response  # noqa: E402
from tdma_field_parse import FIELDS as TDMA_FIELDS  # noqa: E402

PHYS_FIELDS = (
    "armed", "role", "baud_hz", "tx_count", "rx_count", "rx_bad_count",
    "tx_busy_count", "rx_partial_count", "rx_stall_count",
    "tx_timeout_count", "last_error", "last_rx_size", "tx_sck_pin",
    "tx_pin", "rx_sck_pin", "rx_pin", "last_bad_header0",
    "last_bad_header1", "last_bad_header2", "last_bad_header3",
    "last_bad_words", "rx_busy_count", "rx_magic_fail_count",
    "rx_busy_word0", "rx_busy_word1", "rx_busy_word2", "rx_busy_word3",
    "rx_busy_moved", "rx_magic_at_zero", "rx_magic_at_shift",
    "tx_csn_pin", "rx_csn_pin", "rx_ring_overrun_count",
    "rx_dma_produced_words", "rx_scan_produced_words", "rx_dma_write_index",
    "rx_dma_channel", "tx_edge_count", "rx_edge_count",
    "last_tx_edge_timestamp_ns_lo", "last_tx_edge_timestamp_ns_hi",
    "last_tx_done_timestamp_ns_lo", "last_tx_done_timestamp_ns_hi",
    "last_rx_edge_timestamp_ns_lo", "last_rx_edge_timestamp_ns_hi",
    "last_rx_extract_timestamp_ns_lo", "last_rx_extract_timestamp_ns_hi",
    "program_persona", "program_switch_count", "program_switch_fail_count",
    "flight_marker_offset_sample_count", "flight_sck_offset_sample_count",
    "flight_data_offset_sample_count", "flight_sck_phase_delay_cycles",
    "flight_data_phase_delay_cycles",
    "pio_irq_flags", "pio_fdebug", "tx_sm_pc", "rx_sm_pc",
    "tx_sm_tx_fifo_level", "tx_sm_rx_fifo_level",
    "rx_sm_tx_fifo_level", "rx_sm_rx_fifo_level", "gpio_input_levels",
    "origin_done_irq_count", "origin_done_txstall_count",
    "origin_clock_timeout_count", "origin_data_timeout_count",
    "origin_recovery_count",
    "overlay_prepare_count", "overlay_prepare_fail_count",
    "overlay_replacement_byte_count", "overlay_alignment_byte_shift",
    "overlay_alignment_bit_shift", "overlay_physical_byte_count",
    "overlay_last_error", "overlay_tx_dma_remaining",
    "overlay_tx_dma_busy", "overlay_tx_fifo_level_at_fail",
    "overlay_prepare_wait_us",
    "overlay_program_offset", "overlay_tx_dma_read_index",
    "overlay_tx_dma_ctrl", "overlay_sm_shiftctrl", "overlay_sm_execctrl",
    "overlay_sm_pc_at_fail", "overlay_pio_ctrl_at_fail",
    "overlay_pio_fstat_at_fail", "overlay_pio_fdebug_at_fail",
)

PROFILE_FIELD_COUNT = 6
SCORE_VERSION = 1


@dataclass(frozen=True)
class Board:
    port: str
    address: str
    idn: str
    build: str


@dataclass(frozen=True)
class Profile:
    level: int
    baud_hz: int
    cycle_period_ns: int
    train_cycles: int
    flags: int
    profile_crc32: int


def command(ser: serial.Serial, text: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout_s, require_match=False).strip()


def parse_ints(raw: str) -> list[int]:
    values: list[int] = []
    for field in raw.split(","):
        try:
            values.append(int(field.strip().strip('"'), 0))
        except ValueError:
            values.append(-1)
    return values


def parse_catalog(raw: str) -> list[Profile]:
    values = parse_ints(raw)
    if not values or values[0] < 1:
        raise RuntimeError(f"invalid operating-profile catalog: {raw}")
    count = values[0]
    expected = 1 + count * PROFILE_FIELD_COUNT
    if len(values) != expected or any(value < 0 for value in values):
        raise RuntimeError(
            f"operating-profile catalog fields={len(values)}, expected={expected}")
    profiles = []
    for index in range(count):
        base = 1 + index * PROFILE_FIELD_COUNT
        profiles.append(Profile(*values[base:base + PROFILE_FIELD_COUNT]))
    if [profile.level for profile in profiles] != list(range(count)):
        raise RuntimeError("operating-profile levels must be contiguous from zero")
    return profiles


def discover(wanted: set[str], timeout_s: float, settle_s: float) -> dict[str, Board]:
    found: dict[str, Board] = {}
    for item in list_ports.comports():
        try:
            with serial.Serial(item.device, 115200, timeout=0.1,
                               write_timeout=timeout_s) as ser:
                time.sleep(settle_s)
                identity = parse_idn_response(command(ser, "*IDN?", timeout_s))
                if identity.address not in wanted:
                    continue
                build = command(ser, "SYSTem:FW:BUILD?", timeout_s).strip('"')
                found[identity.address] = Board(item.device, identity.address,
                                                identity.idn, build)
        except (OSError, serial.SerialException, ValueError):
            continue
    missing = wanted - set(found)
    if missing:
        raise RuntimeError(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    return found


def board_command(board: Board, text: str, timeout_s: float) -> str:
    with serial.Serial(board.port, 115200, timeout=0.1,
                       write_timeout=timeout_s) as ser:
        time.sleep(0.15)
        identity = parse_idn_response(command(ser, "*IDN?", timeout_s))
        if identity.address != board.address:
            raise RuntimeError(f"{board.port}: *IDN? changed to {identity.address}")
        action = text.strip().split(maxsplit=1)[0].upper()
        ack_only = {
            "SYSTEM:TDMA:RING:STOP", "SYST:TDMA:RING:STOP",
            "SYSTEM:TDMA:RING:ARM", "SYST:TDMA:RING:ARM",
            "SYSTEM:TDMA:RING:START", "SYST:TDMA:RING:START",
        }
        return command(ser, text,
                       min(timeout_s, 0.8) if action in ack_only else timeout_s)


def board_catalog(board: Board, timeout_s: float) -> list[Profile]:
    return parse_catalog(board_command(
        board, "SYSTem:TDMA:OPMode:CATalog?", timeout_s))


def board_active_profile(board: Board, timeout_s: float) -> Profile:
    values = parse_ints(board_command(board, "SYSTem:TDMA:OPMode?", timeout_s))
    if len(values) < PROFILE_FIELD_COUNT or any(
            value < 0 for value in values[:PROFILE_FIELD_COUNT]):
        raise RuntimeError(f"{board.address}: invalid active operating profile")
    return Profile(*values[:PROFILE_FIELD_COUNT])


def snapshot(board: Board, timeout_s: float) -> dict:
    with serial.Serial(board.port, 115200, timeout=0.1,
                       write_timeout=timeout_s) as ser:
        time.sleep(0.1)
        identity = parse_idn_response(command(ser, "*IDN?", timeout_s))
        if identity.address != board.address:
            raise RuntimeError(f"{board.port}: identity changed to {identity.address}")
        tdma_values = parse_ints(command(
            ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", timeout_s))
        phys_values = parse_ints(command(
            ser, "SYSTem:SYNC:VDC:TDMA:PHYS?", timeout_s))
    tdma = {name: tdma_values[index] if index < len(tdma_values) else -1
            for index, name in enumerate(TDMA_FIELDS)}
    phys = {name: phys_values[index] if index < len(phys_values) else -1
            for index, name in enumerate(PHYS_FIELDS)}
    return {"address": board.address, "port": board.port, "build": board.build,
            "tdma": tdma, "phys": phys}


def snapshot_pair(a: Board, b: Board, timeout_s: float) -> tuple[dict, dict]:
    with ThreadPoolExecutor(max_workers=2) as pool:
        future_a = pool.submit(snapshot, a, timeout_s)
        future_b = pool.submit(snapshot, b, timeout_s)
        return future_a.result(), future_b.result()


def delta(before: dict, after: dict, plane: str, field: str) -> int:
    return after[plane].get(field, -1) - before[plane].get(field, -1)


def score_board(row: dict) -> tuple[int, str]:
    if (row["up_running"] != 1 or row["down_running"] != 1 or
            row["baud_hz"] != row["expected_baud_hz"]):
        return 0, "F"
    score = 100
    score -= min(30, max(0, row["adapter_rx_bad_delta"]) * 15)
    score -= min(30, max(0, row["phys_rx_bad_delta"]) * 10)
    score -= min(40, max(0, row["stall_delta"]) * 20)
    score -= min(50, max(0, row["tx_timeout_delta"]) * 25)
    score -= min(30, max(0, row["ring_overrun_delta"]) * 8)
    tx_rate = max(0.0, row["adapter_tx_rate"])
    rx_rate = max(0.0, row["adapter_rx_rate"])
    if max(tx_rate, rx_rate) <= 0.0:
        score -= 20
    else:
        balance = min(tx_rate, rx_rate) / max(tx_rate, rx_rate)
        if balance < 0.95:
            score -= min(15, round((0.95 - balance) * 100))
    expected_rate = max(0.0, row["expected_loop_rate"])
    if expected_rate > 0.0:
        rate_ratio = min(tx_rate, rx_rate) / expected_rate
        if rate_ratio < 0.90:
            score -= min(30, round((0.90 - max(0.0, rate_ratio)) * 50))
    score = max(0, min(100, score))
    grade = "A" if score >= 95 else "B" if score >= 85 else \
        "C" if score >= 70 else "D" if score >= 50 else "F"
    return score, grade


def render_summary_markdown(results: list[dict]) -> str:
    lines = [
        "| Level | SPI | TDMA period | Target loop rate | Score | Grade | Strict |",
        "|---:|---:|---:|---:|---:|:---:|:---:|",
    ]
    for result in results:
        lines.append(
            f"| {result['level']} | {result['frequency_hz'] / 1_000_000:g} MHz | "
            f"{result['cycle_period_ns'] / 1000:g} us | "
            f"{result.get('expected_loop_rate', 0):g}/s | "
            f"{result.get('score', 0)} | {result.get('grade', 'F')} | "
            f"{'PASS' if result.get('passed', False) else 'FAIL'} |")
    return "\n".join(lines) + "\n"


def run_profile(profile: Profile, args: argparse.Namespace) -> dict:
    boards = discover({args.reference_id, args.forward_id}, args.timeout, 0.2)
    reference = boards[args.reference_id]
    forward = boards[args.forward_id]
    for board in (reference, forward):
        board_command(board, "SYSTem:TDMA:RING:STOP", args.timeout)
    for board in (reference, forward):
        board_command(board, f"SYSTem:TDMA:OPMode:STAGe {profile.level}", args.timeout)
        board_command(board, "SYSTem:TDMA:OPMode:APPLy", args.timeout)
        active = board_active_profile(board, args.timeout)
        if active != profile:
            raise RuntimeError(
                f"{board.address}: active profile {active} != requested {profile}")
    board_command(reference, "SYSTem:TDMA:RING:LOCAL 0", args.timeout)
    board_command(forward, "SYSTem:TDMA:RING:LOCAL 1", args.timeout)
    for board in (forward, reference):
        board_command(board, "SYSTem:TDMA:RING:ARM", args.timeout)
    for board in (forward, reference):
        train_cycles = args.train_cycles or profile.train_cycles
        board_command(board, f"SYSTem:TDMA:RING:TRAIN {train_cycles}", args.timeout)
    for board in (forward, reference):
        board_command(board, "SYSTem:TDMA:RING:START", args.timeout)
    time.sleep(args.start_wait)
    before = snapshot_pair(reference, forward, args.timeout)
    time.sleep(args.window_s)
    after = snapshot_pair(reference, forward, args.timeout)
    elapsed = args.window_s
    expected_loop_rate = 1_000_000_000.0 / (2.0 * profile.cycle_period_ns)
    rows = []
    for item_before, item_after in zip(before, after):
        rows.append({
            "address": item_after["address"],
            "port": item_after["port"],
            "role": "reference" if item_after["address"] == args.reference_id else "forward",
            "baud_hz": item_after["phys"].get("baud_hz", -1),
            "expected_baud_hz": profile.baud_hz,
            "expected_loop_rate": expected_loop_rate,
            "up_running": item_after["tdma"].get("ring_up_running", -1),
            "down_running": item_after["tdma"].get("ring_down_running", -1),
            "last_error": item_after["tdma"].get("ring_last_error", -1),
            "adapter_tx_rate": delta(item_before, item_after, "tdma", "ring_adapter_tx_count") / elapsed,
            "adapter_rx_rate": delta(item_before, item_after, "tdma", "ring_adapter_rx_count") / elapsed,
            "adapter_rx_bad_delta": delta(item_before, item_after, "tdma", "ring_adapter_rx_bad_count"),
            "phys_rx_bad_delta": delta(item_before, item_after, "phys", "rx_bad_count"),
            "magic_fail_delta": delta(item_before, item_after, "phys", "rx_magic_fail_count"),
            "stall_delta": delta(item_before, item_after, "phys", "rx_stall_count"),
            "tx_timeout_delta": delta(item_before, item_after, "phys", "tx_timeout_count"),
            "ring_overrun_delta": delta(item_before, item_after, "phys", "rx_ring_overrun_count"),
        })
        rows[-1]["loop_rate_ratio"] = min(
            rows[-1]["adapter_tx_rate"], rows[-1]["adapter_rx_rate"]) / expected_loop_rate
        rows[-1]["rate_gate_pass"] = rows[-1]["loop_rate_ratio"] >= 0.90
        rows[-1]["score"], rows[-1]["grade"] = score_board(rows[-1])
    passed = all(
        row["up_running"] == 1 and row["down_running"] == 1 and
        row["baud_hz"] == profile.baud_hz and
        row["rate_gate_pass"] and
        row["adapter_rx_bad_delta"] == 0 and row["phys_rx_bad_delta"] == 0 and
        row["stall_delta"] == 0 and
        row["tx_timeout_delta"] == 0 and row["ring_overrun_delta"] == 0
        for row in rows
    )
    score = min(row["score"] for row in rows)
    grade = max((row["grade"] for row in rows),
                key=lambda value: "ABCDF".index(value))
    return {"level": profile.level, "frequency_hz": profile.baud_hz,
            "cycle_period_ns": profile.cycle_period_ns,
            "profile_flags": profile.flags,
            "profile_crc32": profile.profile_crc32,
            "expected_loop_rate": expected_loop_rate,
            "build_ids": sorted({reference.build, forward.build}), "passed": passed,
            "score_version": SCORE_VERSION, "score": score, "grade": grade,
            "window_s": elapsed, "boards": rows}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-id", required=True)
    parser.add_argument("--forward-id", required=True)
    parser.add_argument("--frequency-mhz", action="append", type=int,
                        help=("repeatable frequency filter; default tests the "
                              "validated 10, 25, 30 MHz ladder"))
    parser.add_argument("--level", action="append", type=int,
                        help="repeatable level filter; overrides the default frequency ladder")
    parser.add_argument("--window-s", type=float, default=8.0)
    parser.add_argument("--start-wait", type=float, default=5.0)
    parser.add_argument("--train-cycles", type=int,
                        help="override catalog training cycles")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--continue-on-failure", action="store_true",
                        help="test remaining profiles after a strict failure")
    parser.add_argument("--resume", action="store_true",
                        help="reuse completed per-level JSON files in out-dir")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.reference_id == args.forward_id:
        raise SystemExit("reference-id and forward-id must differ")
    discovered = discover({args.reference_id, args.forward_id}, args.timeout, 0.2)
    reference_catalog = board_catalog(discovered[args.reference_id], args.timeout)
    forward_catalog = board_catalog(discovered[args.forward_id], args.timeout)
    if reference_catalog != forward_catalog:
        raise SystemExit("boards report different operating-profile catalogs")
    profiles = reference_catalog
    if args.level:
        wanted_levels = set(args.level)
        unknown = wanted_levels - {profile.level for profile in profiles}
        if unknown:
            raise SystemExit(f"levels absent from SCPI catalog: {sorted(unknown)}")
        profiles = [profile for profile in profiles if profile.level in wanted_levels]
    wanted_frequency_mhz = args.frequency_mhz or (
        None if args.level else [10, 25, 30])
    if wanted_frequency_mhz:
        wanted_hz = {value * 1_000_000 for value in wanted_frequency_mhz}
        unknown = wanted_hz - {profile.baud_hz for profile in profiles}
        if unknown:
            raise SystemExit(
                f"frequencies absent from selected SCPI profiles: {sorted(unknown)}")
        profiles = [profile for profile in profiles if profile.baud_hz in wanted_hz]
    out_root = args.out_dir or ROOT / "build-validation" / (
        f"tdma_frequency_sweep_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_root.mkdir(parents=True, exist_ok=True)
    results = []
    for profile in profiles:
        frequency_mhz = profile.baud_hz // 1_000_000
        cycle_us = profile.cycle_period_ns // 1000
        result_path = out_root / (
            f"level{profile.level:02d}_{frequency_mhz}MHz_{cycle_us}us.json")
        if args.resume and result_path.exists():
            result = json.loads(result_path.read_text(encoding="utf-8"))
            results.append(result)
            print(f"resume: level {profile.level} score={result.get('score', 0)} "
                  f"grade={result.get('grade', 'F')}", flush=True)
            continue
        print(f"=== TDMA level {profile.level}: {frequency_mhz} MHz / {cycle_us} us ===",
              flush=True)
        try:
            result = run_profile(profile, args)
        except (OSError, RuntimeError, serial.SerialException) as exc:
            result = {"level": profile.level, "frequency_hz": profile.baud_hz,
                      "cycle_period_ns": profile.cycle_period_ns,
                      "passed": False, "score": 0, "grade": "F",
                      "error": str(exc)}
        results.append(result)
        result_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2), flush=True)
        if not result.get("passed", False) and not args.continue_on_failure:
            print(f"stop: level {profile.level} failed; remaining profiles skipped",
                  file=sys.stderr, flush=True)
            break
    summary = {"passed": bool(results) and all(item.get("passed", False)
                                              for item in results),
               "score_version": SCORE_VERSION,
               "tested_count": len(results),
               "ranking": [item["level"] for item in sorted(
                   results, key=lambda item: (-item.get("score", 0), item["level"]))],
               "results": results}
    (out_root / "summary.json").write_text(json.dumps(summary, indent=2),
                                           encoding="utf-8")
    (out_root / "summary.md").write_text(
        render_summary_markdown(results), encoding="utf-8")
    print(f"out_dir={out_root}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
