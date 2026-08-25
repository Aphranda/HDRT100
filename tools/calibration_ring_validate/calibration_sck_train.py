#!/usr/bin/env python3
"""Train per-Node SCK phase from already accepted MARK offsets.

The source emits a PIO-owned MARK and known SCK code.  The destination starts
raw rx_sck capture from that received MARK edge.  Host time never participates
in phase measurement; this tool only loads parameters, downloads SD evidence,
and builds the complete observed SCK offset matrix for 2..8 Nodes.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import statistics
import sys
import time
from collections import Counter
from dataclasses import asdict
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from calibration_ring_validate.calibration_data_train import (  # noqa: E402
    parse_storage_read,
)
from calibration_ring_validate.calibration_marker_train import (  # noqa: E402
    Board,
    board_command,
    discover,
    marker_action_args,
    order_boards_by_board_no,
    prepare_ring,
)


SCK_FIELDS = (
    "tag", "version", "state", "reject_reason", "flags",
    "board_id_lo", "board_id_hi", "build_id_lo", "build_id_hi",
    "source_node", "destination_node", "train_epoch", "train_sequence",
    "sck_codebook_id", "sck_crc32", "observed_crc32",
    "calibration_generation", "topology_generation", "topology_crc32",
    "profile_crc32", "schedule_crc32", "sample_period_ns",
    "marker_to_sck_samples", "source_marker_offset_sample_count",
    "destination_marker_offset_sample_count",
    "configured_sck_offset_sample_count",
    "search_start_offset_sample", "search_end_offset_sample",
    "guard_sample_count", "polarity", "correlation_reject_reason",
    "best_lag_sample", "best_distance", "second_lag_sample",
    "second_distance", "margin", "resolved_offset_sample_count",
    "resolved_offset_ns", "training_window_start_ns",
    "training_window_end_ns", "marker_sck_skew_ns",
    "captured_sample_count", "expected_sample_count",
    "dma_overrun_count", "pio_stall_count", "timeout_count",
    "marker_capture_tick_lo", "marker_capture_tick_hi",
    "sck_capture_tick_lo", "sck_capture_tick_hi",
)

STATE_PREPARED = 1
STATE_ACCEPTED = 3
STATE_REJECTED = 4
FLAG_DIAGNOSTIC_ONLY = 1 << 0
FLAG_HARDWARE_MARKER = 1 << 1
FLAG_HARDWARE_SCK_CAPTURE = 1 << 2
FLAG_DMA_COMPLETE = 1 << 3
FLAG_CRC_VALID = 1 << 4
FLAG_EPOCH_VALID = 1 << 5
FLAG_SEQUENCE_VALID = 1 << 6
FLAG_POLARITY_VALID = 1 << 7
DESTINATION_REQUIRED_FLAGS = (
    FLAG_HARDWARE_MARKER | FLAG_HARDWARE_SCK_CAPTURE |
    FLAG_DMA_COMPLETE | FLAG_CRC_VALID | FLAG_EPOCH_VALID |
    FLAG_SEQUENCE_VALID | FLAG_POLARITY_VALID
)
SCK_CAPTURE_SCHEMA = "HAOFV_SCK_TRAIN_CAPTURE_V1"
SCK_MATRIX_SCHEMA = "HAOFV_SCK_OFFSET_MATRIX_V1"


def _u64(row: dict[str, int | str], low: str, high: str) -> int:
    return int(row[low]) | (int(row[high]) << 32)


def parse_sck_status(raw: str) -> dict[str, int | str]:
    values = next(csv.reader([raw]), [])
    if len(values) != len(SCK_FIELDS):
        raise ValueError(
            f"SCK field count {len(values)}, expected {len(SCK_FIELDS)}")
    result: dict[str, int | str] = {"tag": values[0].strip().strip('"')}
    if result["tag"] != "SCKTRN":
        raise ValueError(f"invalid SCK tag {result['tag']!r}")
    for field, value in zip(SCK_FIELDS[1:], values[1:]):
        result[field] = int(value.strip().strip('"'), 0)
    result["board_unique_id"] = _u64(
        result, "board_id_lo", "board_id_hi")
    result["build_id"] = _u64(result, "build_id_lo", "build_id_hi")
    result["marker_capture_tick"] = _u64(
        result, "marker_capture_tick_lo", "marker_capture_tick_hi")
    result["sck_capture_tick"] = _u64(
        result, "sck_capture_tick_lo", "sck_capture_tick_hi")
    return result


def summarize_sck_capture(capture: object) -> dict[str, object]:
    if (not isinstance(capture, dict) or
            capture.get("schema") != SCK_CAPTURE_SCHEMA):
        raise ValueError(f"capture schema must be {SCK_CAPTURE_SCHEMA}")
    words = capture.get("raw_words")
    sample_count = capture.get("raw_sample_count")
    word_count = capture.get("raw_word_count")
    if (not isinstance(words, list) or not isinstance(sample_count, int) or
            not isinstance(word_count, int) or word_count != len(words) or
            sample_count <= 0 or sample_count > word_count * 32 or
            any(not isinstance(word, int) or word < 0 or word > 0xFFFFFFFF
                for word in words)):
        raise ValueError("invalid SCK capture words/count")
    samples = [
        (words[index >> 5] >> (index & 31)) & 1
        for index in range(sample_count)
    ]
    transitions = [
        index for index in range(1, sample_count)
        if samples[index] != samples[index - 1]
    ]
    return {
        "sample_count": sample_count,
        "high_samples": sum(samples),
        "low_samples": sample_count - sum(samples),
        "transition_count": len(transitions),
        "first_transition_sample": transitions[0] if transitions else None,
        "last_transition_sample": transitions[-1] if transitions else None,
        "constant_level": samples[0] if not transitions else None,
    }


def validate_link(source: dict[str, int | str],
                  destination: dict[str, int | str]) -> dict[str, object]:
    errors: list[str] = []
    common = (
        "source_node", "destination_node", "train_epoch", "train_sequence",
        "sck_codebook_id", "sck_crc32", "calibration_generation",
        "topology_crc32", "profile_crc32",
        "schedule_crc32", "sample_period_ns", "marker_to_sck_samples",
        "source_marker_offset_sample_count",
        "destination_marker_offset_sample_count",
        "configured_sck_offset_sample_count", "search_start_offset_sample",
        "search_end_offset_sample", "guard_sample_count",
    )
    for field in common:
        if int(source[field]) != int(destination[field]):
            errors.append(f"common_{field}")
    if int(source["topology_generation"]) <= 0:
        errors.append("source_topology_generation")
    if int(destination["topology_generation"]) <= 0:
        errors.append("destination_topology_generation")
    if int(source["state"]) != STATE_ACCEPTED:
        errors.append("source_state")
    if int(source["reject_reason"]) != 0:
        errors.append("source_reject_reason")
    source_flags = (FLAG_DIAGNOSTIC_ONLY | FLAG_HARDWARE_MARKER |
                    FLAG_HARDWARE_SCK_CAPTURE | FLAG_DMA_COMPLETE)
    if (int(source["flags"]) & source_flags) != source_flags:
        errors.append("source_flags")
    if int(destination["state"]) != STATE_ACCEPTED:
        errors.append("destination_state")
    if int(destination["reject_reason"]) != 0:
        errors.append("destination_reject_reason")
    if (int(destination["flags"]) & DESTINATION_REQUIRED_FLAGS) != \
            DESTINATION_REQUIRED_FLAGS:
        errors.append("destination_flags")
    if int(destination["sck_crc32"]) != int(destination["observed_crc32"]):
        errors.append("destination_crc")
    if int(destination["correlation_reject_reason"]) != 0:
        errors.append("destination_correlation")
    residual = int(destination["resolved_offset_sample_count"])
    if not (int(destination["search_start_offset_sample"]) <= residual <=
            int(destination["search_end_offset_sample"])):
        errors.append("destination_offset_range")
    for prefix, record in (("source", source), ("destination", destination)):
        for field in ("dma_overrun_count", "pio_stall_count", "timeout_count"):
            if int(record[field]) != 0:
                errors.append(f"{prefix}_{field}")
    calibrated = int(destination["configured_sck_offset_sample_count"]) + residual
    return {
        "phase": "TRN-SCK",
        "passed": not errors,
        "diagnostic_only": True,
        "errors": errors,
        "residual_offset_sample_count": residual,
        "calibrated_sck_offset_sample_count": calibrated,
        "calibrated_sck_offset_ns": (
            calibrated * int(destination["sample_period_ns"])),
        "marker_sck_skew_ns": int(destination["marker_sck_skew_ns"]),
        "source": source,
        "destination": destination,
    }


def build_sck_offset_matrix(trials: list[dict[str, object]],
                            node_count: int,
                            sample_period_ns: int) -> dict[str, object]:
    candidates: list[list[int]] = [[] for _ in range(node_count)]
    for trial in trials:
        if not bool(trial.get("passed")):
            continue
        destination = int(trial["destination_node"])
        value = int(trial["calibrated_sck_offset_sample_count"])
        if value not in candidates[destination]:
            candidates[destination].append(value)
    missing = [node for node, values in enumerate(candidates) if not values]
    if missing:
        return {
            "schema": SCK_MATRIX_SCHEMA,
            "sample_period_ns": sample_period_ns,
            "candidate_values_by_node": candidates,
            "full_matrix_row_count": 0,
            "active_row_id": -1,
            "rows": [],
            "missing_nodes": missing,
        }
    candidates = [sorted(values) for values in candidates]
    selected = [values[len(values) // 2] for values in candidates]
    rows: list[dict[str, object]] = []
    active_row_id = -1
    for row_id, values in enumerate(itertools.product(*candidates)):
        row_values = list(values)
        if row_values == selected:
            active_row_id = row_id
        rows.append({
            "row_id": row_id,
            "sck_offset_sample_counts_by_node": row_values,
            "sck_offset_ns_by_node": [
                value * sample_period_ns for value in row_values],
        })
    return {
        "schema": SCK_MATRIX_SCHEMA,
        "sample_period_ns": sample_period_ns,
        "candidate_values_by_node": candidates,
        "full_matrix_row_count": len(rows),
        "active_row_id": active_row_id,
        "rows": rows,
        "missing_nodes": [],
    }


def summarize_repeat_matrix(trials: list[dict[str, object]], node_count: int,
                            repeats: int, max_offset_span: int,
                            sample_period_ns: int) -> dict[str, object]:
    links = []
    for link in range(node_count):
        rows = [row for row in trials if int(row.get("link", -1)) == link]
        accepted = [row for row in rows if bool(row.get("passed"))]
        offsets = [int(row["calibrated_sck_offset_sample_count"])
                   for row in accepted]
        failures = []
        if len(rows) != repeats:
            failures.append("trial_count")
        if len(accepted) != repeats:
            failures.append("rejected_trial")
        span = max(offsets) - min(offsets) if offsets else None
        if span is None or span > max_offset_span:
            failures.append("offset_span")
        links.append({
            "link": link,
            "source_node": link,
            "destination_node": (link + 1) % node_count,
            "trial_count": len(rows),
            "accepted_count": len(accepted),
            "offset_histogram": {
                str(key): value for key, value in
                sorted(Counter(offsets).items())},
            "offset_min_sample": min(offsets) if offsets else None,
            "offset_max_sample": max(offsets) if offsets else None,
            "offset_mean_sample": statistics.fmean(offsets) if offsets else None,
            "offset_span_sample": span,
            "gate_failures": failures,
            "passed": not failures,
        })
    identity_fields = (
        "calibration_generation", "topology_crc32",
        "profile_crc32", "schedule_crc32", "sample_period_ns",
    )
    identity = {
        field: sorted({int(row["destination"][field]) for row in trials
                       if bool(row.get("passed"))})
        for field in identity_fields
    }
    identity_failures = [field for field, values in identity.items()
                         if len(values) != 1]
    topology_generation_by_node = {
        str(node): sorted({
            int(row["destination"]["topology_generation"])
            for row in trials
            if bool(row.get("passed")) and
            int(row.get("destination_node", -1)) == node
        })
        for node in range(node_count)
    }
    identity["topology_generation_by_node"] = topology_generation_by_node
    identity_failures.extend(
        f"topology_generation_node{node}"
        for node in range(node_count)
        if len(topology_generation_by_node[str(node)]) != 1)
    gate_failures = []
    if len(trials) != node_count * repeats:
        gate_failures.append("matrix_trial_count")
    if any(not bool(row.get("passed")) for row in trials):
        gate_failures.append("matrix_rejected_trial")
    if identity_failures:
        gate_failures.append("mixed_generation_or_profile")
    if any(not bool(link["passed"]) for link in links):
        gate_failures.append("link_repeat_gate")
    offset_matrix = build_sck_offset_matrix(
        trials, node_count, sample_period_ns)
    if offset_matrix["missing_nodes"]:
        gate_failures.append("sck_offset_matrix_incomplete")
    return {
        "expected_trial_count": node_count * repeats,
        "trial_count": len(trials),
        "accepted_count": sum(bool(row.get("passed")) for row in trials),
        "identity": identity,
        "identity_failures": identity_failures,
        "links": links,
        "offset_matrix": offset_matrix,
        "gate_failures": gate_failures,
        "passed": not gate_failures,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical loop order")
    parser.add_argument("--link-index", type=int, default=0)
    parser.add_argument("--reference-node", type=int, default=0)
    parser.add_argument("--codebook", type=int, default=0)
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--sequence", type=int)
    parser.add_argument("--generation", type=int, default=1)
    parser.add_argument("--marker-to-sck-samples", type=int, default=1000)
    parser.add_argument("--sample-period-ns", type=int, default=4)
    parser.add_argument("--node-marker-offset-samples", type=int,
                        action="append", required=True,
                        help="accepted MARK offset for each Node")
    parser.add_argument("--node-sck-offset-samples", type=int,
                        action="append",
                        help="current SCK matrix row; defaults to all zero")
    parser.add_argument("--search-start", type=int, default=-10)
    parser.add_argument("--search-end", type=int, default=10)
    parser.add_argument("--guard-samples", type=int, default=0)
    parser.add_argument("--max-best-distance", type=int, default=512)
    parser.add_argument("--min-margin", type=int, default=0)
    parser.add_argument("--all-links", action="store_true")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--max-offset-span", type=int, default=1)
    parser.add_argument("--level", type=int, default=7)
    parser.add_argument("--reuse-ring-identity", action="store_true")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float, default=0.1)
    parser.add_argument("--marker-timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--topology-retries", type=int, default=3)
    parser.add_argument("--gap", type=float, default=0.2)
    parser.add_argument("--expected-build")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def validate_hil_args(args: argparse.Namespace) -> list[str]:
    board_ids = list(args.board_id)
    count = len(board_ids)
    if not 2 <= count <= 8 or len(set(board_ids)) != count:
        raise SystemExit("board IDs must contain 2..8 unique entries")
    marker_offsets = list(args.node_marker_offset_samples)
    if (len(marker_offsets) != count or
            any(not -10 <= value <= 10 for value in marker_offsets)):
        raise SystemExit(
            "node-marker-offset-samples must provide one -10..+10 value "
            "per Node")
    args.node_marker_offset_samples = marker_offsets
    sck_offsets = list(args.node_sck_offset_samples or [0] * count)
    if (len(sck_offsets) != count or
            any(not -10 <= value <= 10 for value in sck_offsets)):
        raise SystemExit(
            "node-sck-offset-samples must provide one -10..+10 value "
            "per Node")
    args.node_sck_offset_samples = sck_offsets
    if not 0 <= args.link_index < count:
        raise SystemExit("link-index outside physical link range")
    if not 0 <= args.reference_node < count:
        raise SystemExit("reference-node outside physical Node order")
    if not (0 <= args.codebook <= 3 and 1 <= args.epoch <= 255 and
            args.generation > 0 and args.marker_to_sck_samples > 0 and
            args.sample_period_ns > 0 and
            -10 <= args.search_start <= args.search_end <= 10 and
            1 <= args.repeats <= 1000 and
            0 <= args.max_offset_span <= 20):
        raise SystemExit("invalid bounded SCK search parameters")
    return board_ids


def discover_ordered(args: argparse.Namespace,
                     board_ids: list[str]) -> list[Board]:
    args.board_ids = board_ids
    boards = discover(args)
    missing = set(board_ids) - set(boards)
    if missing:
        raise SystemExit(f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    ordered = order_boards_by_board_no(boards, board_ids, args)
    args.board_id = [board.address for board in ordered]
    if args.expected_build:
        wrong = {board.address: board.build for board in ordered
                 if board.build != args.expected_build}
        if wrong:
            raise SystemExit(f"build mismatch: {wrong}")
    return ordered


def sck_status(board: Board, args: argparse.Namespace) -> dict[str, int | str]:
    return parse_sck_status(board_command(
        board, "READ:CALibration:SCK?", args))


def sck_arm(board: Board, args: argparse.Namespace) -> str:
    sequence = args.sequence or args.epoch
    command = (
        f"CALibration:SCK:ARM {args.source_node},{args.destination_node},"
        f"{args.codebook},{args.epoch},{sequence},{args.generation},"
        f"{args.marker_to_sck_samples},"
        f"{args.source_marker_offset_sample},"
        f"{args.destination_marker_offset_sample},"
        f"{args.configured_sck_offset_sample},"
        f"{args.search_start},{args.search_end},{args.guard_samples},"
        f"{args.max_best_distance},{args.min_margin}")
    response = board_command(board, command, marker_action_args(args))
    if not (response.startswith(
            f"{args.source_node},{args.destination_node},{args.epoch},"
            f"{args.generation}") or response.startswith("OK(no payload")):
        raise RuntimeError(f"{board.address}: SCK ARM rejected: {response!r}")
    return response


def wait_states(boards: list[Board], args: argparse.Namespace,
                accepted: tuple[int, ...]) -> list[dict[str, int | str]]:
    deadline = time.monotonic() + args.marker_timeout
    last = [sck_status(board, args) for board in boards]
    while time.monotonic() < deadline:
        if all(int(row["state"]) in accepted and
               int(row["train_epoch"]) == args.epoch for row in last):
            return last
        time.sleep(0.05)
        last = [sck_status(board, args) for board in boards]
    raise RuntimeError(f"SCK state timeout, accepted={accepted}: {last}")


def save_sck_capture(board: Board,
                     args: argparse.Namespace) -> dict[str, object]:
    response = board_command(board, "CALibration:SCK:CAPTure:SAVE", args)
    values = [value.strip().strip('"')
              for value in next(csv.reader([response]), [])]
    if len(values) != 3 or values[0] != "OK":
        raise RuntimeError(
            f"{board.address}: SCK capture SD save rejected: {response!r}")
    job_id = int(values[1], 0)
    path = values[2]
    deadline = time.monotonic() + args.marker_timeout
    last = ""
    while time.monotonic() < deadline:
        last = board_command(board, "SYSTem:STORage:JOB?", args)
        job = [value.strip().strip('"')
               for value in next(csv.reader([last]), [])]
        if len(job) >= 8 and int(job[1], 0) == job_id:
            if job[0] == "DONE":
                return {"board": board.address, "path": path,
                        "job_id": job_id, "size": int(job[4], 0)}
            if job[0] == "FAILED":
                raise RuntimeError(
                    f"{board.address}: SCK capture SD job failed: {last!r}")
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: SCK capture SD job timeout: {last!r}")


def download_sck_capture(board: Board, capture_file: dict[str, object],
                         args: argparse.Namespace) -> dict[str, object]:
    path = str(capture_file["path"])
    data = bytearray()
    file_size: int | None = None
    path_hash: int | None = None
    while file_size is None or len(data) < file_size:
        requested = 128 if file_size is None else min(128, file_size - len(data))
        response = board_command(
            board,
            f'SYSTem:STORage:FILE:READ? "{path}",{len(data)},{requested}',
            args)
        page = parse_storage_read(response, len(data))
        if file_size is None:
            file_size = int(page["file_size"])
            path_hash = int(page["path_hash"])
        elif int(page["file_size"]) != file_size:
            raise RuntimeError("SCK capture file size changed during download")
        payload = page["payload"]
        assert isinstance(payload, bytes)
        if not payload and not bool(page["eof"]):
            raise RuntimeError("SCK capture read made no progress before EOF")
        data.extend(payload)
        if bool(page["eof"]):
            break
    if file_size is None or len(data) != file_size:
        raise RuntimeError(f"SCK capture incomplete: {len(data)}/{file_size}")
    capture = json.loads(data.decode("utf-8"))
    analysis = summarize_sck_capture(capture)
    local_path = args.out_dir / (
        f"node{args.destination_node}_link{args.link_index}_sck_capture.json")
    local_path.write_bytes(data)
    return {"sd_path": path, "local_path": str(local_path),
            "size": len(data), "path_hash": path_hash, **analysis}


def run_link_trial(args: argparse.Namespace, ordered: list[Board],
                   prepare: bool) -> dict[str, object]:
    count = len(ordered)
    args.source_node = args.link_index
    args.destination_node = (args.link_index + 1) % count
    args.source_marker_offset_sample = int(
        args.node_marker_offset_samples[args.source_node])
    args.destination_marker_offset_sample = int(
        args.node_marker_offset_samples[args.destination_node])
    args.configured_sck_offset_sample = int(
        args.node_sck_offset_samples[args.destination_node])
    plan: dict[str, object] = {
        "phase": "TRN-SCK",
        "diagnostic_only": True,
        "node_ids_in_loop_order": list(args.board_id),
        "link": args.link_index,
        "source_node": args.source_node,
        "destination_node": args.destination_node,
        "measurement_direction": "mark_forward_sck_forward",
        "node_marker_offset_samples": list(args.node_marker_offset_samples),
        "source_marker_offset_sample_count":
            args.source_marker_offset_sample,
        "destination_marker_offset_sample_count":
            args.destination_marker_offset_sample,
        "node_sck_offset_samples": list(args.node_sck_offset_samples),
        "configured_sck_offset_sample_count":
            args.configured_sck_offset_sample,
        "marker_to_sck_samples": args.marker_to_sck_samples,
        "sample_period_ns": args.sample_period_ns,
        "search_offset_samples": [args.search_start, args.search_end],
        "reused_ring_identity": bool(args.reuse_ring_identity),
        "boards": {board.address: asdict(board) for board in ordered},
    }
    if args.dry_run:
        return {**plan, "passed": False, "dry_run": True}
    actions = prepare_ring(ordered, args) if prepare else []
    source = ordered[args.source_node]
    destination = ordered[args.destination_node]
    active = [source, destination]
    try:
        arms = [
            {"board": destination.address,
             "response": sck_arm(destination, args)},
            {"board": source.address,
             "response": sck_arm(source, args)},
        ]
        armed = wait_states(active, args, (STATE_PREPARED,))
        injection = board_command(
            source, "CALibration:SCK:INJect", marker_action_args(args))
        if not injection.startswith("OK"):
            raise RuntimeError(
                f"{source.address}: SCK INJECT rejected: {injection!r}")
        completed = wait_states(active, args, (STATE_ACCEPTED, STATE_REJECTED))
        validation = validate_link(completed[0], completed[1])
        capture_file = save_sck_capture(destination, args)
        capture_download = download_sck_capture(
            destination, capture_file, args)
    finally:
        for board in active:
            board_command(board, "CALibration:SCK:STOP", args)
    return {**plan, **validation, "actions": actions, "arms": arms,
            "armed": armed, "injection": injection, "completed": completed,
            "capture_file": capture_file,
            "capture_download": capture_download}


def run(args: argparse.Namespace) -> dict[str, object]:
    board_ids = validate_hil_args(args)
    ordered = discover_ordered(args, board_ids)
    board_ids = list(args.board_id)
    root_out = args.out_dir or (
        ROOT / "out" / "training" /
        f"sck_offset_matrix_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    root_out.mkdir(parents=True, exist_ok=True)
    args.out_dir = root_out
    if not args.all_links:
        args.out_dir = root_out
        return run_link_trial(
            args, ordered, prepare=not args.reuse_ring_identity)
    trial_count = len(ordered) * args.repeats
    if args.epoch + trial_count - 1 > 255:
        raise SystemExit("SCK matrix exceeds uint8 training epoch range")
    actions = ([] if args.reuse_ring_identity else prepare_ring(ordered, args))
    trials: list[dict[str, object]] = []
    trial_index = 0
    for link in range(len(ordered)):
        for repeat_index in range(1, args.repeats + 1):
            trial_args = argparse.Namespace(**vars(args))
            trial_args.link_index = link
            trial_args.epoch = args.epoch + trial_index
            trial_args.sequence = trial_args.epoch
            trial_args.out_dir = root_out / (
                f"link{link}_repeat{repeat_index}_e{trial_args.epoch}")
            trial_args.out_dir.mkdir(parents=True, exist_ok=True)
            try:
                trial = run_link_trial(trial_args, ordered, prepare=False)
            except Exception as exc:  # persist expected failure evidence
                trial = {
                    "phase": "TRN-SCK",
                    "link": link,
                    "source_node": link,
                    "destination_node": (link + 1) % len(ordered),
                    "train_epoch": trial_args.epoch,
                    "calibration_generation": args.generation,
                    "passed": False,
                    "error": f"{type(exc).__name__}: {exc}",
                }
            trial["repeat_index"] = repeat_index
            trials.append(trial)
            (trial_args.out_dir / "summary.json").write_text(
                json.dumps(trial, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
            print(json.dumps({"sck_trial": {
                "link": link, "repeat_index": repeat_index,
                "epoch": trial_args.epoch,
                "passed": bool(trial.get("passed")),
                "offset_sample": trial.get(
                    "calibrated_sck_offset_sample_count"),
                "error": trial.get("error", ""),
            }}, ensure_ascii=False), flush=True)
            trial_index += 1
            time.sleep(args.gap)
    matrix = summarize_repeat_matrix(
        trials, len(ordered), args.repeats, args.max_offset_span,
        args.sample_period_ns)
    return {
        "phase": "TRN-SCK_OFFSET_MATRIX",
        "diagnostic_only": True,
        "node_ids_in_loop_order": board_ids,
        "boards": {board.address: asdict(board) for board in ordered},
        "repeats": args.repeats,
        "max_offset_span_sample": args.max_offset_span,
        "calibration_generation": args.generation,
        "training_parameters": {
            "node_marker_offset_samples": list(
                args.node_marker_offset_samples),
            "node_sck_offset_samples": list(args.node_sck_offset_samples),
            "marker_to_sck_samples": args.marker_to_sck_samples,
            "sample_period_ns": args.sample_period_ns,
        },
        "initial_epoch": args.epoch,
        "actions": actions,
        "matrix": matrix,
        "trials": trials,
        "passed": bool(matrix["passed"]),
    }


def main() -> int:
    args = parse_args()
    result = run(args)
    out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"sck_train_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    summary = out_dir / "summary.json"
    summary.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(json.dumps({"passed": bool(result.get("passed")),
                      "summary": str(summary)}, ensure_ascii=False))
    return 0 if bool(result.get("passed")) else 1


if __name__ == "__main__":
    raise SystemExit(main())
