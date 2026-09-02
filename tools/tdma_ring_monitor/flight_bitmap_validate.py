#!/usr/bin/env python3
"""Read-only multi-board TDMA flight bitmap validation by *IDN? address."""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass
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

from scpi_common.board_identity import parse_idn_response  # noqa: E402
from scpi_common.scpi_serial import read_scpi_response  # noqa: E402


PROCESS_FIELDS = (
    "version", "configured", "active", "local_slot", "map_crc32",
    "map_generation", "payload_size", "local_segment_count",
    "map_apply_count", "input_bytes", "output_bytes",
    "tx_stale_reuse_count", "map_reject_count", "length_reject_count",
    "tx_unavailable_count", "rx_bitmap_scan_count", "rx_bitmap_hit_count",
    "rx_bitmap_duplicate_count", "rx_bitmap_present_count",
    "rx_bitmap_incomplete_count", "receive_version", "receive_configured",
    "receive_state", "receive_last_reason", "receive_last_transport_result",
    "receive_quality_flags", "receive_accepted_count",
    "receive_rejected_count", "receive_missing_count",
    "receive_consecutive_failure_count", "receive_image_generation",
    "receive_accepted_sequence", "receive_accepted_identity_crc32",
    "receive_accepted_schedule_crc32", "receive_accepted_profile_crc32",
    "receive_accepted_map_generation", "receive_accepted_segment_mask",
    "receive_expected_segment_mask", "receive_accepted_wkc",
    "receive_expected_wkc", "receive_accepted_payload_size",
    "receive_last_accept_timestamp_ns",
    "receive_last_observation_timestamp_ns", "receive_stale_age_ns",
    "receive_last_rejected_reason",
    "receive_last_rejected_transport_result",
    "receive_last_rejected_sequence",
    "receive_last_rejected_observed_segment_mask",
    "receive_last_rejected_expected_segment_mask",
    "receive_last_rejected_quality_flags",
    "receive_last_rejected_timestamp_ns",
    "last_rx_origin_slot_id", "last_rx_hop_count", "last_rx_hop_limit",
    "last_rx_flags", "last_rx_sequence", "last_rx_identity_crc32",
    "resident_feedback_condition_mask", "resident_feedback_all_mask",
    "comm_fsm_state", "resident_seeded", "resident_return_ready",
    "resident_bootstrap_retry_count",
    "resident_overlay_bootstrap_prepared",
    "resident_overlay_target_sequence",
    "comm_fsm_timed_out_window_count", "resident_stale_cycle_count",
    "comm_fsm_window_sequence", "comm_fsm_completed_window_count",
    "resident_last_completed_cycle", "resident_last_completed_segment_mask",
    "comm_fsm_last_error", "resident_reseed_count",
    "resident_last_reseed_reason",
)

FIFO_FIELDS = (
    "version", "tx_publish_count", "tx_publish_reject_count",
    "tx_acquire_count", "tx_image_stale_count", "tx_reuse_count",
    "tx_release_count", "tx_ready_count", "tx_active_slot",
    "tx_active_generation", "rx_publish_count", "rx_mirror_drop_count",
    "rx_publish_drop_count", "rx_acquire_count", "rx_release_count",
    "rx_queued_count", "rx_parse_count",
)

REFMEM_FIELDS = (
    "enabled", "local_slot", "node_count", "active_mask", "reference_slot",
    "remote_slot", "payload_size", "mailbox_size", "publish_interval_ms",
    "next_seq32", "tx_publish_count", "tx_reject_count",
    "rx_acquire_count", "rx_empty_count", "rx_accept_count",
    "rx_reject_count", "rx_duplicate_skip_count", "rx_bad_mailbox_count",
    "last_rx_result", "last_frame_type", "last_source_slot", "last_seq32",
    "last_value_u32", "last_error",
    "wire_layout_version", "last_vdc_phase_offset_ns",
    "last_vdc_rate_adjust_ppb", "last_vdc_lock_state", "last_vdc_quality",
    "last_ack_seq16", "last_ack_flags", "last_control_opcode",
    "last_control_seq8", "last_optional_diagnostic", "last_mailbox_crc16",
)


@dataclass(frozen=True)
class Board:
    port: str
    address: str
    idn: str
    build: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--board-id", action="append", required=True,
        help="exact *IDN? third-field address; repeat for 2..8 boards")
    parser.add_argument("--expected-build")
    parser.add_argument("--window-s", type=float, default=10.0)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def command(ser: serial.Serial, text: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout_s, require_match=True)


def parse_snapshot(raw: str, fields: tuple[str, ...], address: str) -> dict[str, int]:
    try:
        values = [int(value.strip().strip('"'), 0) for value in raw.split(",")]
    except ValueError as exc:
        raise RuntimeError(f"{address}: non-integer snapshot: {raw}") from exc
    if len(values) != len(fields):
        raise RuntimeError(
            f"{address}: field count {len(values)}, expected {len(fields)}: {raw}")
    return dict(zip(fields, values))


def probe(port: str, wanted: set[str], args: argparse.Namespace) -> Board | None:
    try:
        with serial.Serial(port, args.baud, timeout=0.1,
                           write_timeout=args.timeout) as ser:
            time.sleep(args.settle)
            identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
            if identity.address not in wanted:
                return None
            build = command(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
            return Board(port, identity.address, identity.idn, build)
    except (OSError, serial.SerialException, ValueError):
        return None


def discover(wanted: set[str], args: argparse.Namespace) -> dict[str, Board]:
    found: dict[str, Board] = {}
    for item in list_ports.comports():
        board = probe(item.device, wanted, args)
        if board is not None:
            if board.address in found:
                raise RuntimeError(f"duplicate *IDN? address: {board.address}")
            found[board.address] = board
    return found


def sample(board: Board, args: argparse.Namespace) -> dict[str, dict[str, int]]:
    with serial.Serial(board.port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        identity = parse_idn_response(command(ser, "*IDN?", args.timeout))
        if identity.address != board.address:
            raise RuntimeError(
                f"{board.port}: identity changed to {identity.address}, "
                f"expected {board.address}")
        process = parse_snapshot(
            command(ser, "SYSTem:TDMA:FLIGHT:PROCess?", args.timeout),
            PROCESS_FIELDS, board.address)
        fifo = parse_snapshot(
            command(ser, "SYSTem:TDMA:FLIGHT:FIFO?", args.timeout),
            FIFO_FIELDS, board.address)
        refmem = parse_snapshot(
            command(ser, "SYSTem:REFMEM:SYNC:FLIGHT?", args.timeout),
            REFMEM_FIELDS, board.address)
    return {"process": process, "fifo": fifo, "refmem": refmem}


def sample_all(boards: dict[str, Board], args: argparse.Namespace) -> dict[str, dict]:
    with ThreadPoolExecutor(max_workers=len(boards)) as pool:
        futures = {address: pool.submit(sample, board, args)
                   for address, board in boards.items()}
        return {address: future.result() for address, future in futures.items()}


def counter_delta(before: dict, after: dict, group: str, field: str) -> int:
    return after[group][field] - before[group][field]


def validate_board(before: dict, after: dict) -> list[str]:
    errors: list[str] = []
    process = after["process"]
    fifo = after["fifo"]
    refmem = after["refmem"]
    is_reference = refmem["local_slot"] == refmem["reference_slot"]
    checks = (
        (process["version"] >= 2, "flight engine version < 2"),
        (process["configured"] == 1, "flight map not configured"),
        (process["active"] == 1, "flight map not active"),
        (process["payload_size"] == 256, "process payload is not 256 B"),
        (process["local_segment_count"] == 1, "local segment count is not 1"),
        (fifo["version"] >= 2, "flight FIFO version < 2"),
        (refmem["enabled"] == 1, "RefMem flight sync disabled"),
        (2 <= refmem["node_count"] <= 8, "active node count outside 2..8"),
        (refmem["payload_size"] == 256, "RefMem payload is not 256 B"),
        (refmem["mailbox_size"] == 32, "RefMem mailbox is not 32 B"),
        (process["local_slot"] == refmem["local_slot"], "local slot mismatch"),
        (process["receive_version"] >= 1, "receive-health version < 1"),
        (process["receive_configured"] == 1,
         "receive-health not configured"),
        (process["receive_state"] == 1,
         "accepted process image is not VALID"),
        (counter_delta(before, after, "process",
                       "receive_accepted_count") > 0,
         "no process image accepted"),
        (counter_delta(before, after, "process",
                       "receive_rejected_count") == 0,
         "receive-health reject count grew"),
        (counter_delta(before, after, "process",
                       "receive_missing_count") == 0,
         "receive-health missing count grew"),
        (process["receive_accepted_segment_mask"] ==
         process["receive_expected_segment_mask"],
         "accepted segment bitmap incomplete"),
        (process["receive_accepted_wkc"] == process["receive_expected_wkc"],
         "accepted WKC incomplete"),
        (counter_delta(before, after, "process", "rx_bitmap_scan_count") > 0,
         "no remote mailbox header scanned"),
        (counter_delta(before, after, "process", "rx_bitmap_hit_count") > 0,
         "no bitmap candidate delivered"),
        (counter_delta(before, after, "refmem", "tx_publish_count") > 0,
         "no local mailbox published"),
        (counter_delta(before, after, "refmem", "rx_accept_count") > 0,
         "core0 accepted no remote mailbox"),
        (counter_delta(before, after, "refmem", "rx_reject_count") == 0,
         "RefMem RX reject count grew"),
        (counter_delta(before, after, "refmem", "rx_bad_mailbox_count") == 0,
         "bad mailbox count grew"),
        (counter_delta(before, after, "fifo", "rx_mirror_drop_count") == 0,
         "RX FIFO mirror drop count grew"),
    )
    for passed, message in checks:
        if not passed:
            errors.append(message)
    # The reference terminates the returned ring frame and only performs
    # classify_input + commit_input.  In-flight apply/patch is a forwarding
    # node responsibility, so only non-reference nodes must grow this count.
    if (not is_reference and
            counter_delta(before, after, "process", "map_apply_count") <= 0):
        errors.append("no flight frame applied")
    if process["rx_bitmap_scan_count"] < process["rx_bitmap_hit_count"]:
        errors.append("bitmap hit count exceeds scan count")
    return errors


def main() -> int:
    args = parse_args()
    wanted = set(args.board_id)
    if len(wanted) != len(args.board_id):
        raise SystemExit("board-id values must be unique")
    if not 2 <= len(wanted) <= 8:
        raise SystemExit("repeat --board-id for 2..8 unique boards")
    if args.window_s <= 0.0:
        raise SystemExit("window-s must be positive")

    boards = discover(wanted, args)
    missing = wanted - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    if args.expected_build:
        wrong = {address: board.build for address, board in boards.items()
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(
                f"build mismatch: expected {args.expected_build}, found {wrong}")

    before = sample_all(boards, args)
    started = time.monotonic()
    time.sleep(args.window_s)
    after = sample_all(boards, args)
    elapsed_s = time.monotonic() - started
    errors = {address: validate_board(before[address], after[address])
              for address in sorted(boards)}
    passed = all(not board_errors for board_errors in errors.values())
    deltas: dict[str, dict[str, int]] = {}
    for address in sorted(boards):
        deltas[address] = {
            "map_apply": counter_delta(before[address], after[address],
                                       "process", "map_apply_count"),
            "bitmap_scan": counter_delta(before[address], after[address],
                                         "process", "rx_bitmap_scan_count"),
            "bitmap_hit": counter_delta(before[address], after[address],
                                        "process", "rx_bitmap_hit_count"),
            "bitmap_duplicate": counter_delta(
                before[address], after[address], "process",
                "rx_bitmap_duplicate_count"),
            "fifo_rx_drop": counter_delta(before[address], after[address],
                                          "fifo", "rx_mirror_drop_count"),
            "refmem_tx_publish": counter_delta(before[address], after[address],
                                               "refmem", "tx_publish_count"),
            "refmem_tx_reject": counter_delta(before[address], after[address],
                                              "refmem", "tx_reject_count"),
            "refmem_rx_accept": counter_delta(before[address], after[address],
                                              "refmem", "rx_accept_count"),
            "refmem_rx_reject": counter_delta(before[address], after[address],
                                              "refmem", "rx_reject_count"),
            "refmem_rx_bad": counter_delta(before[address], after[address],
                                           "refmem", "rx_bad_mailbox_count"),
        }

    result = {
        "passed": passed,
        "elapsed_s": elapsed_s,
        "boards": {address: asdict(board) for address, board in boards.items()},
        "deltas": deltas,
        "errors": errors,
        "before": before,
        "after": after,
    }
    out_dir = args.out_dir or (
        ROOT / "build-validation" /
        f"flight_bitmap_validate_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    print(f"out_dir={out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
