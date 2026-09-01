#!/usr/bin/env python3
"""Run the bounded TRN-02 DATA-window search on one directed physical link."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
from dataclasses import asdict
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(ROOT / "tools" / "tdma_ring_monitor") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools" / "tdma_ring_monitor"))

from calibration_ring_validate.calibration_marker_train import (  # noqa: E402
    Board,
    board_command,
    discover,
    marker_action_args,
    order_boards_by_board_no,
    prepare_ring,
)
from calibration_ring_validate.calibration_phase import (  # noqa: E402
    build_observed_offset_matrix,
    build_phase_training_contract,
    link_base_delay_ns,
    phase_delay_samples,
    validate_generation,
)
from calibration_ring_validate.calibration_timeout_config import (  # noqa: E402
    DEFAULT_ACTION_TIMEOUT_S,
    DEFAULT_PHASE_GAP_S,
    DEFAULT_SERIAL_SETTLE_S,
)
from calibration_ring_validate.calibration_load_guard import (  # noqa: E402
    CalibrationLoadGuard,
)
from calibration_ring_validate.calibration_clk_codebook_eval import (  # noqa: E402
    crc8_atm,
)


DATA_FIELDS = (
    "tag", "version", "state", "reject_reason", "flags",
    "board_id_lo", "board_id_hi", "build_id_lo", "build_id_hi",
    "source_node", "destination_node", "train_epoch", "train_sequence",
    "data_codebook_id", "data_crc32", "observed_crc32",
    "observed_header_fields_valid", "observed_header",
    "observed_header_inverse", "observed_header_crc8",
    "calibration_generation", "topology_generation", "topology_crc32",
    "profile_crc32", "schedule_crc32", "sample_period_ns",
    "marker_to_data_samples", "link_base_delay_ns",
    "marker_offset_sample_count",
    "configured_data_offset_sample_count",
    "search_start_offset_sample", "search_end_offset_sample",
    "guard_sample_count", "max_best_distance", "min_margin",
    "diagnostic_fault_flags", "diagnostic_wire_epoch",
    "diagnostic_header_crc8_xor", "polarity",
    "correlation_reject_reason",
    "best_lag_sample", "best_distance", "second_lag_sample",
    "second_distance", "margin", "resolved_offset_sample_count",
    "resolved_offset_ns", "training_window_start_ns",
    "training_window_end_ns", "marker_data_skew_ns",
    "captured_sample_count", "expected_sample_count",
    "dma_overrun_count", "pio_stall_count", "timeout_count",
    "marker_capture_tick_lo", "marker_capture_tick_hi",
    "data_capture_tick_lo", "data_capture_tick_hi",
)

STATE_PREPARED = 1
STATE_ACCEPTED = 3
STATE_REJECTED = 4
DATA_CAPTURE_SCHEMAS = {
    "HAOFV_DATA_TRAIN_CAPTURE_V1",  # legacy generic base field
    "HAOFV_DATA_TRAIN_CAPTURE_V2",  # explicit per-link base field
    "HAOFV_DATA_TRAIN_CAPTURE_V3",  # observed header and fault evidence
}
FAULT_EPOCH_OVERRIDE = 1 << 0
FAULT_HEADER_CRC8_XOR = 1 << 1
FAULT_PIO_STALL = 1 << 2
FAULT_DMA_OVERRUN = 1 << 3
FAULT_WIRE_ALL = FAULT_EPOCH_OVERRIDE | FAULT_HEADER_CRC8_XOR
FAULT_TRANSPORT_ALL = FAULT_PIO_STALL | FAULT_DMA_OVERRUN
FLAG_DIAGNOSTIC_ONLY = 1 << 0
FLAG_HARDWARE_MARKER = 1 << 1
FLAG_HARDWARE_DATA_CAPTURE = 1 << 2
FLAG_DMA_COMPLETE = 1 << 3
FLAG_CRC_VALID = 1 << 4
FLAG_EPOCH_VALID = 1 << 5
FLAG_SEQUENCE_VALID = 1 << 6
FLAG_POLARITY_VALID = 1 << 7
DESTINATION_REQUIRED_FLAGS = (
    FLAG_HARDWARE_MARKER | FLAG_HARDWARE_DATA_CAPTURE |
    FLAG_DMA_COMPLETE | FLAG_CRC_VALID | FLAG_EPOCH_VALID |
    FLAG_SEQUENCE_VALID | FLAG_POLARITY_VALID
)
DIRECTION_CHOICES = ("forward", "reverse")
DATA_REJECT_NAMES = {
    2: "GENERATION",
    3: "EPOCH",
    4: "SEQUENCE",
    5: "CRC",
    6: "EVIDENCE_FLAGS",
    7: "CORRELATION",
    8: "POLARITY",
    9: "CAPTURE_TRUNCATED",
    10: "DMA",
    11: "PIO_STALL",
    12: "TIMEOUT",
    13: "DISTANCE",
    14: "MARGIN",
    15: "SEARCH_RANGE",
    16: "EDGE_ORDER",
}
DATA_REJECT_CODES = {name: code for code, name in DATA_REJECT_NAMES.items()}


def direction_endpoints(link: int, node_count: int,
                        direction: str) -> tuple[int, int]:
    """Resolve one physical link from runtime training parameters."""
    low = link
    high = (link + 1) % node_count
    if direction == "forward":
        return low, high
    if direction == "reverse":
        return high, low
    raise ValueError(f"unsupported direction {direction!r}")


def _u64(row: dict[str, int | str], low: str, high: str) -> int:
    return int(row[low]) | (int(row[high]) << 32)


def parse_data_status(raw: str) -> dict[str, int | str]:
    values = next(csv.reader([raw]), [])
    if len(values) != len(DATA_FIELDS):
        raise ValueError(
            f"TRN-02 field count {len(values)}, expected {len(DATA_FIELDS)}")
    result: dict[str, int | str] = {"tag": values[0].strip().strip('"')}
    if result["tag"] != "DATATRN":
        raise ValueError(f"invalid TRN-02 tag {result['tag']!r}")
    for field, value in zip(DATA_FIELDS[1:], values[1:]):
        result[field] = int(value.strip().strip('"'), 0)
    result["board_unique_id"] = _u64(
        result, "board_id_lo", "board_id_hi")
    result["build_id"] = _u64(result, "build_id_lo", "build_id_hi")
    result["marker_capture_tick"] = _u64(
        result, "marker_capture_tick_lo", "marker_capture_tick_hi")
    result["data_capture_tick"] = _u64(
        result, "data_capture_tick_lo", "data_capture_tick_hi")
    return result


def decode_observed_header(row: dict[str, int | str]) -> dict[str, int]:
    if int(row["observed_header_fields_valid"]) == 0:
        raise ValueError("observed marker header fields are unavailable")
    header = int(row["observed_header"])
    inverse = int(row["observed_header_inverse"])
    crc8 = int(row["observed_header_crc8"])
    if not (0 <= header <= 0xFFFF and 0 <= inverse <= 0xFFFF and
            0 <= crc8 <= 0xFF):
        raise ValueError("observed marker header fields exceed wire widths")
    calculated_crc8 = crc8_atm(bytes((
        header >> 8, header & 0xFF, inverse >> 8, inverse & 0xFF)))
    return {
        "version": (header >> 14) & 0x03,
        "codebook_id": (header >> 12) & 0x03,
        "epoch": (header >> 4) & 0xFF,
        "source_node": (header >> 1) & 0x07,
        "polarity": header & 0x01,
        "header": header,
        "header_inverse": inverse,
        "header_crc8": crc8,
        "calculated_header_crc8": calculated_crc8,
        "inverse_valid": int((header ^ inverse) == 0xFFFF),
        "crc8_valid": int(crc8 == calculated_crc8),
    }


def validate_responder_wire_fault(
        receiver: dict[str, int | str], *, expected_epoch: int,
        expected_source_node: int, wire_epoch: int = 0,
        header_crc8_xor: int = 0) -> list[str]:
    errors: list[str] = []
    try:
        observed = decode_observed_header(receiver)
    except ValueError:
        return ["receiver_observed_header_unavailable"]
    if observed["inverse_valid"] == 0:
        errors.append("receiver_observed_header_inverse")
    if observed["source_node"] != expected_source_node:
        errors.append("receiver_observed_source_node")
    if wire_epoch != 0:
        if observed["epoch"] != wire_epoch or wire_epoch == expected_epoch:
            errors.append("receiver_observed_wire_epoch")
        if observed["crc8_valid"] == 0:
            errors.append("receiver_observed_epoch_crc8")
        if int(receiver["correlation_reject_reason"]) != 9:
            errors.append("receiver_epoch_correlation_reason")
    elif header_crc8_xor != 0:
        if observed["epoch"] != expected_epoch:
            errors.append("receiver_crc_fault_epoch")
        if observed["header_crc8"] != (
                observed["calculated_header_crc8"] ^ header_crc8_xor):
            errors.append("receiver_observed_crc8_xor")
        if int(receiver["correlation_reject_reason"]) != 8:
            errors.append("receiver_crc_correlation_reason")
    return errors


def validate_transport_fault(
        receiver: dict[str, int | str], responder: dict[str, int | str], *,
        expected_reason: int, expected_fault_flags: int) -> dict[str, object]:
    """Validate an initiator-side RX transport fault.

    The marker source is the DATA receiver/initiator in the current
    marker-forward/data-return persona.  PIO/DMA faults therefore belong to
    that source endpoint; the opposite endpoint remains a normal responder.
    """
    errors: list[str] = []
    if expected_reason not in DATA_REJECT_NAMES:
        raise ValueError("unknown DATA rejection reason")
    if int(receiver["state"]) != STATE_REJECTED:
        errors.append("receiver_state")
    if int(receiver["reject_reason"]) != expected_reason:
        errors.append("receiver_reject_reason")
    if int(responder["state"]) != STATE_ACCEPTED:
        errors.append("responder_state")
    if int(responder["reject_reason"]) != 0:
        errors.append("responder_reject_reason")
    if int(receiver["diagnostic_fault_flags"]) != expected_fault_flags:
        errors.append("receiver_transport_fault_readback")
    if int(responder["diagnostic_fault_flags"]) != 0:
        errors.append("responder_transport_fault_readback")
    if expected_reason == DATA_REJECT_CODES["PIO_STALL"]:
        if int(receiver["pio_stall_count"]) == 0:
            errors.append("receiver_pio_stall_evidence")
        if int(receiver["timeout_count"]) != 0:
            errors.append("receiver_timeout_overrode_pio_stall")
    elif expected_reason == DATA_REJECT_CODES["DMA"]:
        if int(receiver["dma_overrun_count"]) == 0:
            errors.append("receiver_dma_evidence")
    return {
        "phase": "TRN-03D_DATA_TRANSPORT_FAULT",
        "passed": not errors,
        "accepted": False,
        "diagnostic_only": True,
        "active_candidate_allowed": False,
        "trn03_staging_allowed": False,
        "expected_reject_reason": expected_reason,
        "expected_reject_name": DATA_REJECT_NAMES[expected_reason],
        "expected_transport_fault_flags": expected_fault_flags,
        "errors": errors,
        "receiver": receiver,
        "responder": responder,
    }


def parse_storage_read(raw: str, expected_offset: int) -> dict[str, object]:
    values = [value.strip().strip('"')
              for value in next(csv.reader([raw]), [])]
    if len(values) != 10 or values[0] != "OK":
        raise ValueError(f"storage read rejected: {raw!r}")
    offset = int(values[2], 0)
    requested = int(values[3], 0)
    returned = int(values[4], 0)
    file_size = int(values[5], 0)
    error = int(values[8], 0)
    if offset != expected_offset or error != 0:
        raise ValueError(
            f"storage read mismatch: offset={offset}, "
            f"expected={expected_offset}, error={error}")
    payload = bytes.fromhex(values[9])
    if len(payload) != returned or returned > requested:
        raise ValueError(
            f"storage read length mismatch: payload={len(payload)}, "
            f"returned={returned}, requested={requested}")
    return {
        "offset": offset,
        "returned": returned,
        "file_size": file_size,
        "eof": int(values[6], 0) != 0,
        "path_hash": int(values[7], 0),
        "payload": payload,
    }


def summarize_data_capture(capture: object) -> dict[str, object]:
    if (not isinstance(capture, dict) or
            capture.get("schema") not in DATA_CAPTURE_SCHEMAS):
        raise ValueError(
            f"capture schema must be one of {sorted(DATA_CAPTURE_SCHEMAS)}")
    words = capture.get("raw_words")
    sample_count = capture.get("raw_sample_count")
    word_count = capture.get("raw_word_count")
    if (not isinstance(words, list) or not isinstance(sample_count, int) or
            not isinstance(word_count, int) or word_count != len(words) or
            sample_count <= 0 or sample_count > word_count * 32 or
            any(not isinstance(word, int) or word < 0 or word > 0xFFFFFFFF
                for word in words)):
        raise ValueError("invalid DATA capture words/count")
    samples = [
        (words[index >> 5] >> (index & 31)) & 1
        for index in range(sample_count)
    ]
    transitions = [
        index for index in range(1, sample_count)
        if samples[index] != samples[index - 1]
    ]
    high_samples = sum(samples)
    return {
        "sample_count": sample_count,
        "high_samples": high_samples,
        "low_samples": sample_count - high_samples,
        "transition_count": len(transitions),
        "first_transition_sample": transitions[0] if transitions else None,
        "last_transition_sample": transitions[-1] if transitions else None,
        "constant_level": samples[0] if not transitions else None,
    }


def validate_link(source: dict[str, int | str],
                  destination: dict[str, int | str], *,
                  expected_config: dict[str, int] | None = None
                  ) -> dict[str, object]:
    errors: list[str] = []
    common = (
        "source_node", "destination_node", "train_epoch", "train_sequence",
        "data_codebook_id", "data_crc32", "calibration_generation",
        "topology_crc32", "profile_crc32", "schedule_crc32",
        "sample_period_ns", "marker_to_data_samples", "link_base_delay_ns",
        "marker_offset_sample_count",
        "configured_data_offset_sample_count",
        "search_start_offset_sample", "search_end_offset_sample",
        "guard_sample_count",
    )
    for field in common:
        if int(source[field]) != int(destination[field]):
            errors.append(f"common_{field}")
    if expected_config is not None:
        for field, expected in expected_config.items():
            for endpoint, record in (("source", source),
                                     ("destination", destination)):
                if int(record[field]) != expected:
                    errors.append(f"{endpoint}_{field}_readback")
    if int(source["state"]) != STATE_ACCEPTED:
        errors.append("source_state")
    if int(source["reject_reason"]) != 0:
        errors.append("source_reject_reason")
    if (int(source["flags"]) & DESTINATION_REQUIRED_FLAGS) != \
            DESTINATION_REQUIRED_FLAGS:
        errors.append("source_flags")
    if int(destination["state"]) != STATE_ACCEPTED:
        errors.append("destination_state")
    if int(destination["reject_reason"]) != 0:
        errors.append("destination_reject_reason")
    if (int(destination["flags"]) &
            (FLAG_DIAGNOSTIC_ONLY | FLAG_HARDWARE_MARKER |
             FLAG_DMA_COMPLETE)) != (
                FLAG_DIAGNOSTIC_ONLY | FLAG_HARDWARE_MARKER |
                FLAG_DMA_COMPLETE):
        errors.append("destination_flags")
    if int(source["data_crc32"]) != int(source["observed_crc32"]):
        errors.append("source_crc")
    if int(source["correlation_reject_reason"]) != 0:
        errors.append("source_correlation")
    resolved = int(source["resolved_offset_sample_count"])
    if not (int(source["search_start_offset_sample"]) <= resolved <=
            int(source["search_end_offset_sample"])):
        errors.append("source_offset_range")
    for prefix, record in (("source", source), ("destination", destination)):
        for field in ("dma_overrun_count", "pio_stall_count", "timeout_count"):
            if int(record[field]) != 0:
                errors.append(f"{prefix}_{field}")
    calibrated = (
        int(source["configured_data_offset_sample_count"]) + resolved)
    return {
        "phase": "TRN-02B",
        "passed": not errors,
        "diagnostic_only": True,
        "errors": errors,
        "resolved_offset_sample_count": resolved,
        "resolved_offset_ns": int(source["resolved_offset_ns"]),
        "calibrated_data_offset_sample_count": calibrated,
        "calibrated_data_offset_ns": (
            calibrated * int(source["sample_period_ns"])),
        "marker_data_skew_ns": int(source["marker_data_skew_ns"]),
        "source": source,
        "destination": destination,
    }


def validate_expected_rejection(
        source: dict[str, int | str],
        destination: dict[str, int | str],
        expected_reason: int) -> dict[str, object]:
    """Validate a deliberate failure at the DATA receiving endpoint."""
    errors: list[str] = []
    if expected_reason not in DATA_REJECT_NAMES:
        raise ValueError("unknown DATA rejection reason")
    if int(source["state"]) != STATE_REJECTED:
        errors.append("receiver_state")
    if int(source["reject_reason"]) != expected_reason:
        errors.append("receiver_reject_reason")
    if int(destination["state"]) != STATE_ACCEPTED:
        errors.append("responder_state")
    if int(destination["reject_reason"]) != 0:
        errors.append("responder_reject_reason")
    if int(source["calibration_generation"]) != int(
            destination["calibration_generation"]):
        errors.append("calibration_generation")
    if int(source["train_epoch"]) != int(destination["train_epoch"]):
        errors.append("train_epoch")
    return {
        "phase": "TRN-03D_DATA_FAULT",
        "passed": not errors,
        "accepted": False,
        "diagnostic_only": True,
        "active_candidate_allowed": False,
        "trn03_staging_allowed": False,
        "expected_reject_reason": expected_reason,
        "expected_reject_name": DATA_REJECT_NAMES[expected_reason],
        "errors": errors,
        "source": source,
        "destination": destination,
    }


def summarize_repeat_matrix(
        trials: list[dict[str, object]], node_count: int, repeats: int,
        max_offset_span: int, *, selected_links: list[int] | None = None
        ) -> dict[str, object]:
    """Gate a complete matrix or a repeat set for selected physical links."""
    links_to_summarize = (list(range(node_count)) if selected_links is None
                          else list(selected_links))
    if (not links_to_summarize or len(set(links_to_summarize)) !=
            len(links_to_summarize) or
            any(link < 0 or link >= node_count for link in links_to_summarize)):
        raise ValueError("selected links must be unique physical link indices")
    complete_matrix = links_to_summarize == list(range(node_count))
    expected_trial_count = len(links_to_summarize) * repeats
    grouped: dict[int, list[dict[str, object]]] = {
        link: [] for link in links_to_summarize
    }
    for trial in trials:
        link = int(trial.get("link", -1))
        if link in grouped:
            grouped[link].append(trial)

    link_summaries: list[dict[str, object]] = []
    for link, rows in grouped.items():
        accepted = [row for row in rows if bool(row.get("passed"))]
        offsets = [int(row["resolved_offset_sample_count"])
                   for row in accepted]
        calibrated_offsets: list[int] = []
        for row in accepted:
            calibrated = row.get("calibrated_data_offset_sample_count")
            if calibrated is None:
                configured = int(row["source"].get(
                    "configured_data_offset_sample_count", 0))
                calibrated = configured + int(
                    row["resolved_offset_sample_count"])
            calibrated_offsets.append(int(calibrated))
        distances = [int(row["source"]["best_distance"])
                     for row in accepted]
        margins = [int(row["source"]["margin"])
                   for row in accepted]
        skews = [int(row["marker_data_skew_ns"]) for row in accepted]
        offset_span = max(offsets) - min(offsets) if offsets else None
        failures = []
        if len(rows) != repeats:
            failures.append("trial_count")
        if len(accepted) != repeats:
            failures.append("rejected_trial")
        if offset_span is None or offset_span > max_offset_span:
            failures.append("offset_span")
        if any(skew != 0 for skew in skews):
            failures.append("marker_data_skew")
        direction_rows = {
            (int(row["marker_source_node"]),
             int(row["marker_destination_node"]),
             int(row["data_source_node"]),
             int(row["data_destination_node"]),
             str(row["marker_direction"]),
             str(row["data_direction"]))
            for row in rows
            if all(field in row for field in (
                "marker_source_node", "marker_destination_node",
                "data_source_node", "data_destination_node",
                "marker_direction", "data_direction"))
        }
        if len(direction_rows) != 1:
            failures.append("direction_config")
        direction_row = next(iter(direction_rows), (None,) * 6)
        link_summaries.append({
            "link": link,
            "marker_source_node": direction_row[0],
            "marker_destination_node": direction_row[1],
            "data_source_node": direction_row[2],
            "data_destination_node": direction_row[3],
            "marker_direction": direction_row[4],
            "data_direction": direction_row[5],
            "measurement_direction": "configured_bidirectional_link",
            "trial_count": len(rows),
            "accepted_count": len(accepted),
            "offset_histogram": {
                str(key): value
                for key, value in sorted(Counter(offsets).items())
            },
            "residual_offset_histogram": {
                str(key): value
                for key, value in sorted(Counter(offsets).items())
            },
            "calibrated_offset_histogram": {
                str(key): value
                for key, value in sorted(Counter(calibrated_offsets).items())
            },
            "offset_min_sample": min(offsets) if offsets else None,
            "offset_max_sample": max(offsets) if offsets else None,
            "offset_mean_sample": (
                statistics.fmean(offsets) if offsets else None),
            "offset_span_sample": offset_span,
            "calibrated_offset_min_sample": (
                min(calibrated_offsets) if calibrated_offsets else None),
            "calibrated_offset_max_sample": (
                max(calibrated_offsets) if calibrated_offsets else None),
            "best_distance_max": max(distances) if distances else None,
            "margin_min": min(margins) if margins else None,
            "marker_data_skew_ns": sorted(set(skews)),
            "gate_failures": failures,
            "passed": not failures,
        })

    accepted = [trial for trial in trials if bool(trial.get("passed"))]

    def source_values(field: str) -> set[int]:
        return {int(trial["source"][field]) for trial in accepted}

    identity_sets = {
        "calibration_generation": source_values("calibration_generation"),
        "topology_generation": source_values("topology_generation"),
        "topology_crc32": source_values("topology_crc32"),
        "profile_crc32": source_values("profile_crc32"),
        "schedule_crc32": source_values("schedule_crc32"),
        "sample_period_ns": source_values("sample_period_ns"),
    }
    topology_generation_by_node: dict[int, set[int]] = {}
    for trial in accepted:
        source_node = int(trial.get(
            "source_node", trial["source"].get("source_node", -1)))
        topology_generation_by_node.setdefault(source_node, set()).add(
            int(trial["source"]["topology_generation"]))
    identity_failures = [
        field for field, values in identity_sets.items()
        if field != "topology_generation" and len(values) != 1
    ]
    identity_failures.extend(
        f"topology_generation_node{node}"
        for node, values in sorted(topology_generation_by_node.items())
        if len(values) != 1)
    gate_failures = []
    if len(trials) != expected_trial_count:
        gate_failures.append("matrix_trial_count")
    if len(accepted) != expected_trial_count:
        gate_failures.append("matrix_rejected_trial")
    if identity_failures:
        gate_failures.append("mixed_generation_or_profile")
    if not all(bool(row["passed"]) for row in link_summaries):
        gate_failures.append("link_repeat_gate")
    values_by_node: list[list[int]] = [[] for _ in range(node_count)]
    for trial in trials:
        if bool(trial.get("passed")):
            destination = int(trial["data_destination_node"])
            calibrated = trial.get("calibrated_data_offset_sample_count")
            if calibrated is None:
                calibrated = (
                    int(trial["source"].get(
                        "configured_data_offset_sample_count", 0)) +
                    int(trial["resolved_offset_sample_count"]))
            values_by_node[destination].append(int(calibrated))
    offset_matrix = build_observed_offset_matrix(
        signal="DATA", values_by_node=values_by_node,
        sample_period_ns=(next(iter(identity_sets["sample_period_ns"]))
                          if identity_sets["sample_period_ns"] else 4))
    for row in offset_matrix["rows"]:
        row["data_offset_sample_counts_by_node"] = list(
            row["offset_sample_counts_by_node"])
        row["data_offset_ns_by_node"] = list(row["offset_ns_by_node"])
    if complete_matrix and offset_matrix["missing_nodes"]:
        gate_failures.append("data_offset_matrix_incomplete")
    return {
        "selected_links": links_to_summarize,
        "complete_node_matrix": complete_matrix,
        "expected_trial_count": expected_trial_count,
        "trial_count": len(trials),
        "accepted_count": len(accepted),
        "identity": {
            field: sorted(values) for field, values in identity_sets.items()
        } | {
            "topology_generation_by_node": {
                str(node): sorted(values)
                for node, values in sorted(topology_generation_by_node.items())
            }
        },
        "identity_failures": identity_failures,
        "links": link_summaries,
        "offset_matrix": offset_matrix,
        "gate_failures": gate_failures,
        "passed": not gate_failures,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", action="append", required=True,
                        help="exact *IDN? address in physical loop order")
    parser.add_argument("--source-node", type=int, default=0)
    parser.add_argument("--destination-node", type=int, default=1)
    parser.add_argument("--link-index", type=int, default=0,
                        help="physical link index for a single-link run")
    parser.add_argument(
        "--marker-direction", choices=DIRECTION_CHOICES, required=True,
        help="runtime TRN-02 parameter relative to physical node order")
    parser.add_argument(
        "--data-direction", choices=DIRECTION_CHOICES, required=True,
        help="runtime TRN-02 parameter relative to physical node order")
    parser.add_argument("--reference-node", type=int, default=0)
    parser.add_argument("--codebook", type=int, default=0)
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--sequence", type=int)
    parser.add_argument("--generation", type=int, default=1)
    parser.add_argument("--marker-to-data-samples", type=int, default=1000)
    parser.add_argument(
        "--link-delay-ns", type=int, action="append", required=True,
        help=("measured end-to-end delay; repeat once per physical link; "
              "the training base is link-delay/2"))
    parser.add_argument("--sample-period-ns", type=int, default=4,
                        help="runtime PIO sampling period used by offsets")
    parser.add_argument(
        "--node-marker-offset-samples", type=int, action="append",
        help=("one accepted TRN-00 marker offset per node in physical "
              "loop order; link i uses its destination node offset"))
    parser.add_argument(
        "--node-data-offset-samples", type=int, action="append",
        help=("one accepted DATA offset per receiving node; applied to the "
              "base delay before residual search"))
    parser.add_argument("--search-start", type=int, default=-10)
    parser.add_argument("--search-end", type=int, default=10)
    parser.add_argument("--guard-samples", type=int, default=0)
    parser.add_argument("--max-best-distance", type=int, default=512)
    parser.add_argument("--min-margin", type=int, default=0)
    parser.add_argument(
        "--responder-wire-epoch", type=int,
        help=("TRN-03D only: responder transmits this stale epoch while "
              "both endpoints retain the requested epoch identity"))
    parser.add_argument(
        "--responder-header-crc8-xor", type=lambda value: int(value, 0),
        default=0,
        help=("TRN-03D only: XOR mask applied to the responder's transmitted "
              "header CRC8"))
    parser.add_argument(
        "--fault-pio-stall", action="store_true",
        help=("TRN-03D only: pause the initiator RX DMA and require a real "
              "PIO RX FIFO stall"))
    parser.add_argument(
        "--fault-dma-overrun", action="store_true",
        help=("TRN-03D only: arm the initiator RX DMA one word short and "
              "require a DMA rejection"))
    parser.add_argument(
        "--expect-reject-reason", choices=sorted(DATA_REJECT_CODES),
        help=("TRN-03D fault mode: pass only when the DATA receiving endpoint "
              "rejects with this exact reason and the responder accepts"))
    parser.add_argument("--all-links", action="store_true",
                        help="run every directed physical link")
    parser.add_argument("--repeats", type=int, default=1,
                        help="independent trials per selected link")
    parser.add_argument("--max-offset-span", type=int, default=1,
                        help="maximum accepted repeat span in raw samples")
    parser.add_argument("--level", type=int, default=7)
    parser.add_argument(
        "--reuse-ring-identity", action="store_true",
        help=("reuse a stopped ring prepared by same-generation TRN-00; "
              "do not rewrite topology or operating profile"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--action-timeout", type=float,
                        default=DEFAULT_ACTION_TIMEOUT_S)
    parser.add_argument("--marker-timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=DEFAULT_SERIAL_SETTLE_S)
    parser.add_argument("--arm-wait", type=float, default=3.0)
    parser.add_argument("--topology-retries", type=int, default=3)
    parser.add_argument("--gap", type=float, default=DEFAULT_PHASE_GAP_S)
    parser.add_argument("--poll-interval", type=float, default=0.02)
    parser.add_argument("--expected-build")
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--short-open", action="store_true",
                        help="open/close CDC for every command (diagnostic fallback)")
    return parser.parse_args()


def data_status(board: Board, args: argparse.Namespace) -> dict[str, int | str]:
    return parse_data_status(board_command(
        board, "READ:CALibration:DATA?", args))


def data_arm(board: Board, args: argparse.Namespace, *,
             diagnostic_fault_flags: int = 0,
             diagnostic_wire_epoch: int = 0,
             diagnostic_header_crc8_xor: int = 0) -> str:
    sequence = args.sequence or args.epoch
    command = (
        f"CALibration:DATA:ARM {args.source_node},{args.destination_node},"
        f"{args.codebook},{args.epoch},{sequence},{args.generation},"
        f"{args.marker_to_data_samples},{args.link_base_delay_ns_current},"
        f"{args.link_marker_offset_sample},"
        f"{args.link_data_offset_sample},"
        f"{args.search_start},{args.search_end},{args.guard_samples},"
        f"{args.max_best_distance},{args.min_margin},"
        f"{diagnostic_fault_flags},{diagnostic_wire_epoch},"
        f"{diagnostic_header_crc8_xor}")
    response = board_command(board, command, marker_action_args(args))
    if not (response.startswith(
            f"{args.source_node},{args.destination_node},{args.epoch},"
            f"{args.generation}") or response.startswith("OK(no payload")):
        deadline = time.monotonic() + args.marker_timeout
        snapshot = data_status(board, args)
        while (time.monotonic() < deadline and
               (int(snapshot["state"]) != STATE_PREPARED or
                int(snapshot["train_epoch"]) != args.epoch)):
            time.sleep(args.poll_interval)
            snapshot = data_status(board, args)
        if (int(snapshot["state"]) != STATE_PREPARED or
                int(snapshot["train_epoch"]) != args.epoch):
            raise RuntimeError(
                f"{board.address}: DATA ARM rejected: {response!r}, "
                f"snapshot={snapshot}")
    return response


def wait_states(boards: list[Board], args: argparse.Namespace,
                accepted: tuple[int, ...]) -> list[dict[str, int | str]]:
    deadline = time.monotonic() + args.marker_timeout
    with ThreadPoolExecutor(max_workers=len(boards)) as executor:
        last = list(executor.map(data_status, boards, [args] * len(boards)))
    while time.monotonic() < deadline:
        if all(int(row["state"]) in accepted and
               int(row["train_epoch"]) == args.epoch for row in last):
            return last
        time.sleep(args.poll_interval)
        with ThreadPoolExecutor(max_workers=len(boards)) as executor:
            last = list(executor.map(data_status, boards, [args] * len(boards)))
    raise RuntimeError(f"DATA state timeout, accepted={accepted}: {last}")


def wait_idle(boards: list[Board],
              args: argparse.Namespace) -> list[dict[str, int | str]]:
    deadline = time.monotonic() + args.marker_timeout
    with ThreadPoolExecutor(max_workers=len(boards)) as executor:
        last = list(executor.map(data_status, boards, [args] * len(boards)))
    while time.monotonic() < deadline:
        if all(int(row["state"]) == 0 for row in last):
            return last
        time.sleep(args.poll_interval)
        with ThreadPoolExecutor(max_workers=len(boards)) as executor:
            last = list(executor.map(data_status, boards, [args] * len(boards)))
    raise RuntimeError(f"DATA STOP did not restore IDLE: {last}")


def save_data_capture(board: Board, args: argparse.Namespace) -> dict[str, object]:
    response = board_command(
        board, "CALibration:DATA:CAPTure:SAVE", args)
    values = [value.strip().strip('"')
              for value in next(csv.reader([response]), [])]
    if len(values) != 3 or values[0] != "OK":
        raise RuntimeError(
            f"{board.address}: DATA capture SD save rejected: {response!r}")
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
                    f"{board.address}: DATA capture SD job failed: {last!r}")
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: DATA capture SD job timeout: {last!r}")


def download_data_capture(board: Board, capture_file: dict[str, object],
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
            raise RuntimeError("DATA capture file size changed during download")
        payload = page["payload"]
        assert isinstance(payload, bytes)
        if not payload and not bool(page["eof"]):
            raise RuntimeError("DATA capture read made no progress before EOF")
        data.extend(payload)
        if bool(page["eof"]):
            break
    if file_size is None or len(data) != file_size:
        raise RuntimeError(
            f"DATA capture download incomplete: {len(data)}/{file_size}")
    capture = json.loads(data.decode("utf-8"))
    analysis = summarize_data_capture(capture)
    local_path = args.out_dir / (
        f"node{args.data_destination_node}_link{args.link_index}_capture.json")
    local_path.write_bytes(data)
    return {
        "sd_path": path,
        "local_path": str(local_path),
        "size": len(data),
        "path_hash": path_hash,
        **analysis,
    }


def validate_hil_args(args: argparse.Namespace) -> list[str]:
    board_ids = list(args.board_id)
    count = len(board_ids)
    if not 2 <= count <= 8 or len(set(board_ids)) != count:
        raise SystemExit("board IDs must contain 2..8 unique entries")
    marker_offsets = list(args.node_marker_offset_samples or [0] * count)
    if (len(marker_offsets) != count or
            any(offset < -10 or offset > 10 for offset in marker_offsets)):
        raise SystemExit(
            "node-marker-offset-samples must provide one -10..+10 value "
            "per board")
    args.node_marker_offset_samples = marker_offsets
    data_offsets = list(args.node_data_offset_samples or [0] * count)
    if (len(data_offsets) != count or
            any(offset < -10 or offset > 10 for offset in data_offsets)):
        raise SystemExit(
            "node-data-offset-samples must provide one -10..+10 value "
            "per board")
    args.node_data_offset_samples = data_offsets
    link_delays = list(args.link_delay_ns)
    if len(link_delays) != count:
        raise SystemExit("link-delay-ns must provide one value per physical link")
    try:
        args.link_base_delay_ns = [
            link_base_delay_ns(value) for value in link_delays]
        for link, base_ns in enumerate(args.link_base_delay_ns):
            marker_destination = direction_endpoints(
                link, count, args.marker_direction)[1]
            data_destination = direction_endpoints(
                link, count, args.data_direction)[1]
            phase_delay_samples(
                link_base_ns=base_ns,
                sample_period_ns=args.sample_period_ns,
                node_offset_samples=marker_offsets[marker_destination])
            phase_delay_samples(
                link_base_ns=base_ns,
                sample_period_ns=args.sample_period_ns,
                node_offset_samples=data_offsets[data_destination])
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if not 0 <= args.link_index < count:
        raise SystemExit("link-index outside physical link range")
    marker_nodes = direction_endpoints(
        args.link_index, count, args.marker_direction)
    data_nodes = direction_endpoints(
        args.link_index, count, args.data_direction)
    if args.all_links:
        args.source_node, args.destination_node = marker_nodes
    if (args.source_node, args.destination_node) != marker_nodes:
        raise SystemExit(
            "source-node/destination-node do not match marker-direction")
    if data_nodes != (args.destination_node, args.source_node):
        raise SystemExit(
            "current TRN-02 persona requires DATA direction opposite MARK")
    args.data_source_node, args.data_destination_node = data_nodes
    if not 0 <= args.reference_node < count:
        raise SystemExit("reference-node outside board order")
    try:
        validate_generation(args.generation)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if not (0 <= args.codebook <= 3 and 1 <= args.epoch <= 255 and
            args.marker_to_data_samples > 0 and
            args.sample_period_ns > 0 and
            -10 <= args.search_start <= args.search_end <= 10 and
            args.guard_samples >= 0):
        raise SystemExit("invalid bounded TRN-02 search parameters")
    if not 1 <= args.repeats <= 1000:
        raise SystemExit("repeats must be in [1, 1000]")
    if not 0 <= args.max_offset_span <= 20:
        raise SystemExit("max-offset-span must be in [0, 20]")
    if not (0 <= args.max_best_distance <= 0xFFFFFFFF and
            0 <= args.min_margin <= 0xFFFFFFFF):
        raise SystemExit("distance and margin gates must fit uint32")
    if args.expect_reject_reason and (args.all_links or args.repeats != 1):
        raise SystemExit(
            "expect-reject-reason requires one explicit link and one trial")
    wire_epoch = args.responder_wire_epoch
    crc8_xor = args.responder_header_crc8_xor
    if wire_epoch is not None and crc8_xor != 0:
        raise SystemExit("select exactly one responder wire fault per trial")
    if wire_epoch is not None and not (
            1 <= wire_epoch <= 255 and wire_epoch != args.epoch):
        raise SystemExit(
            "responder-wire-epoch must be in [1,255] and differ from epoch")
    if not 0 <= crc8_xor <= 0xFF:
        raise SystemExit("responder-header-crc8-xor must fit uint8")
    if (wire_epoch is not None or crc8_xor != 0) and \
            args.expect_reject_reason not in {"EPOCH", "CRC"}:
        raise SystemExit(
            "responder wire faults require the matching EPOCH or CRC gate")
    if wire_epoch is not None and args.expect_reject_reason != "EPOCH":
        raise SystemExit("responder-wire-epoch requires expected EPOCH reject")
    if crc8_xor != 0 and args.expect_reject_reason != "CRC":
        raise SystemExit("responder-header-crc8-xor requires expected CRC reject")
    transport_fault_flags = (
        FAULT_PIO_STALL if args.fault_pio_stall else 0) | (
            FAULT_DMA_OVERRUN if args.fault_dma_overrun else 0)
    if transport_fault_flags and (wire_epoch is not None or crc8_xor != 0):
        raise SystemExit(
            "wire and transport faults cannot be combined in one DATA trial")
    if args.fault_pio_stall and args.fault_dma_overrun:
        raise SystemExit("select exactly one initiator transport fault")
    if args.fault_pio_stall and args.expect_reject_reason != "PIO_STALL":
        raise SystemExit("fault-pio-stall requires expected PIO_STALL reject")
    if args.fault_dma_overrun and args.expect_reject_reason != "DMA":
        raise SystemExit("fault-dma-overrun requires expected DMA reject")
    if transport_fault_flags and not args.expect_reject_reason:
        raise SystemExit(
            "initiator transport faults require --expect-reject-reason")
    args.diagnostic_wire_fault_flags = (
        FAULT_EPOCH_OVERRIDE if wire_epoch is not None else
        FAULT_HEADER_CRC8_XOR if crc8_xor != 0 else 0)
    args.diagnostic_transport_fault_flags = transport_fault_flags
    args.diagnostic_fault_flags = (
        args.diagnostic_wire_fault_flags | args.diagnostic_transport_fault_flags)
    args.diagnostic_wire_epoch = wire_epoch or 0
    args.diagnostic_header_crc8_xor = crc8_xor
    return board_ids


def discover_ordered(args: argparse.Namespace,
                     board_ids: list[str]) -> list[Board]:
    args.board_ids = board_ids
    boards = getattr(args, "discovered_boards", None) or discover(args)
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


def run_link_trial(args: argparse.Namespace, ordered: list[Board],
                   prepare: bool) -> dict[str, object]:
    board_ids = list(args.board_id)
    node_count = len(board_ids)
    args.link_marker_offset_sample = int(
        args.node_marker_offset_samples[args.destination_node])
    args.link_data_offset_sample = int(
        args.node_data_offset_samples[args.data_destination_node])
    args.link_base_delay_ns_current = int(
        args.link_base_delay_ns[args.link_index])
    args.configured_window_center_ns = (
        args.link_base_delay_ns_current +
        args.link_data_offset_sample * args.sample_period_ns)
    if args.configured_window_center_ns <= 0:
        raise ValueError("configured DATA offset makes base delay non-positive")
    plan: dict[str, object] = {
        "phase": "TRN-02B",
        "diagnostic_only": True,
        "node_ids_in_loop_order": board_ids,
        "source_node": args.source_node,
        "destination_node": args.destination_node,
        "link": args.link_index,
        "marker_source_node": args.source_node,
        "marker_destination_node": args.destination_node,
        "data_source_node": args.data_source_node,
        "data_destination_node": args.data_destination_node,
        "marker_direction": args.marker_direction,
        "data_direction": args.data_direction,
        "measurement_direction": "configured_bidirectional_link",
        "reused_ring_identity": bool(args.reuse_ring_identity),
        "link_delay_ns": int(args.link_delay_ns[args.link_index]),
        "link_base_delay_ns": args.link_base_delay_ns_current,
        "configured_window_center_ns": args.configured_window_center_ns,
        "sample_period_ns": args.sample_period_ns,
        "search_offset_samples": [args.search_start, args.search_end],
        "marker_to_data_samples": args.marker_to_data_samples,
        "node_marker_offset_samples": list(args.node_marker_offset_samples),
        "link_marker_offset_sample": args.link_marker_offset_sample,
        "node_data_offset_samples": list(args.node_data_offset_samples),
        "configured_data_offset_sample_count": args.link_data_offset_sample,
        "max_best_distance": args.max_best_distance,
        "min_margin": args.min_margin,
        "responder_wire_fault": {
            "flags": args.diagnostic_wire_fault_flags,
            "wire_epoch": args.diagnostic_wire_epoch,
            "header_crc8_xor": args.diagnostic_header_crc8_xor,
        },
        "initiator_transport_fault": {
            "flags": args.diagnostic_transport_fault_flags,
            "pio_stall": bool(args.fault_pio_stall),
            "dma_overrun": bool(args.fault_dma_overrun),
        },
        "boards": {board.address: asdict(board) for board in ordered},
        "unified_phase_training": build_phase_training_contract(
            signal="DATA", link_delay_ns_by_link=args.link_delay_ns,
            node_offset_samples=args.node_data_offset_samples,
            sample_period_ns=args.sample_period_ns,
            destination_node_by_link=[direction_endpoints(
                link, node_count, args.data_direction)[1]
                for link in range(node_count)],
            capture_origin="rx_csn_pio_edge"),
    }
    if args.dry_run:
        return {**plan, "passed": False, "dry_run": True}

    actions = prepare_ring(ordered, args) if prepare else []
    with ThreadPoolExecutor(max_workers=len(ordered)) as executor:
        active_values = list(executor.map(
            lambda board: board_command(
                board, "READ:CALibration:ACTive?", args), ordered))
    active_before = dict(zip((board.address for board in ordered), active_values))
    source = ordered[args.source_node]
    destination = ordered[args.destination_node]
    active = [source, destination]
    arms: list[dict[str, str]] = []
    try:
        arms.append({
            "board": destination.address,
            "role": "responder",
            "diagnostic_fault_flags": args.diagnostic_wire_fault_flags,
            "response": data_arm(
                destination, args,
                diagnostic_fault_flags=args.diagnostic_wire_fault_flags,
                diagnostic_wire_epoch=args.diagnostic_wire_epoch,
                diagnostic_header_crc8_xor=
                    args.diagnostic_header_crc8_xor),
        })
        arms.append({"board": source.address,
                     "role": "initiator",
                     "diagnostic_fault_flags":
                         args.diagnostic_transport_fault_flags,
                     "response": data_arm(
                         source, args,
                         diagnostic_fault_flags=
                             args.diagnostic_transport_fault_flags)})
        armed = wait_states(active, args, (STATE_PREPARED,))
        injection = board_command(
            source, "CALibration:DATA:INJect", marker_action_args(args))
        if not injection.startswith("OK"):
            raise RuntimeError(
                f"{source.address}: DATA INJECT rejected: {injection!r}")
        completed = wait_states(
            active, args, (STATE_ACCEPTED, STATE_REJECTED))
        source_row = completed[0]
        destination_row = completed[1]
        expected_config = {
                "source_node": args.source_node,
                "destination_node": args.destination_node,
                "train_epoch": args.epoch,
                "train_sequence": args.sequence or args.epoch,
                "calibration_generation": args.generation,
                "sample_period_ns": args.sample_period_ns,
                "marker_to_data_samples": args.marker_to_data_samples,
                "link_base_delay_ns": args.link_base_delay_ns_current,
                "marker_offset_sample_count":
                    args.link_marker_offset_sample,
                "configured_data_offset_sample_count":
                    args.link_data_offset_sample,
                "search_start_offset_sample": args.search_start,
                "search_end_offset_sample": args.search_end,
                "guard_sample_count": args.guard_samples,
                "max_best_distance": args.max_best_distance,
                "min_margin": args.min_margin,
            }
        if args.expect_reject_reason:
            expected_reason = DATA_REJECT_CODES[args.expect_reject_reason]
            if args.diagnostic_transport_fault_flags:
                validation = validate_transport_fault(
                    source_row, destination_row,
                    expected_reason=expected_reason,
                    expected_fault_flags=
                        args.diagnostic_transport_fault_flags)
            else:
                validation = validate_expected_rejection(
                    source_row, destination_row, expected_reason)
            for field, expected in expected_config.items():
                for endpoint, record in (("source", source_row),
                                         ("destination", destination_row)):
                    if int(record[field]) != expected:
                        validation["errors"].append(
                            f"{endpoint}_{field}_readback")
            fault_readback = {
                "source_diagnostic_fault_flags":
                    args.diagnostic_transport_fault_flags,
                "destination_diagnostic_fault_flags":
                    args.diagnostic_wire_fault_flags,
                "source_diagnostic_wire_epoch": 0,
                "destination_diagnostic_wire_epoch":
                    args.diagnostic_wire_epoch,
                "source_diagnostic_header_crc8_xor": 0,
                "destination_diagnostic_header_crc8_xor":
                    args.diagnostic_header_crc8_xor,
            }
            for field, expected in fault_readback.items():
                endpoint, field_name = field.split("_", 1)
                record = source_row if endpoint == "source" else destination_row
                if int(record[field_name]) != expected:
                    validation["errors"].append(
                        f"{endpoint}_{field_name}_readback")
            if args.diagnostic_wire_fault_flags != 0:
                validation["errors"].extend(validate_responder_wire_fault(
                    source_row, expected_epoch=args.epoch,
                    expected_source_node=args.source_node,
                    wire_epoch=args.diagnostic_wire_epoch,
                    header_crc8_xor=args.diagnostic_header_crc8_xor))
            validation["passed"] = not validation["errors"]
        else:
            validation = validate_link(
                source_row, destination_row,
                expected_config=expected_config)
        if (args.diagnostic_transport_fault_flags == FAULT_PIO_STALL and
                int(source_row["captured_sample_count"]) == 0):
            capture_file = {
                "skipped": True,
                "reason": "PIO_STALL_NO_RAW_CAPTURE",
            }
            capture_download = None
        else:
            capture_file = save_data_capture(source, args)
            capture_download = download_data_capture(
                source, capture_file, args)
    finally:
        with ThreadPoolExecutor(max_workers=len(active)) as executor:
            list(executor.map(
                lambda board: board_command(
                    board, "CALibration:DATA:STOP", args), active))
        idle = wait_idle(active, args)
    with ThreadPoolExecutor(max_workers=len(ordered)) as executor:
        active_values = list(executor.map(
            lambda board: board_command(
                board, "READ:CALibration:ACTive?", args), ordered))
    active_after = dict(zip((board.address for board in ordered), active_values))
    active_unchanged = active_before == active_after
    result = {
        **plan,
        **validation,
        "expected_failure": bool(args.expect_reject_reason),
        "actions": actions,
        "arms": arms,
        "armed": armed,
        "injection": injection,
        "completed": completed,
        "idle_after_stop": idle,
        "active_before": active_before,
        "active_after": active_after,
        "active_unchanged": active_unchanged,
        "capture_file": capture_file,
        "capture_download": capture_download,
    }
    if not active_unchanged:
        result["errors"].append("active_record_changed")
        result["passed"] = False
    if not args.expect_reject_reason:
        residual_offset = int(validation["resolved_offset_sample_count"])
        result["residual_offset_sample_count"] = residual_offset
        result["calibrated_data_offset_sample_count"] = (
            args.link_data_offset_sample + residual_offset)
    return result


def run_hil(args: argparse.Namespace) -> dict[str, object]:
    board_ids = validate_hil_args(args)
    ordered = discover_ordered(args, board_ids)
    return run_link_trial(
        args, ordered, prepare=not args.reuse_ring_identity)


def run_repeat_matrix(args: argparse.Namespace) -> dict[str, object]:
    board_ids = validate_hil_args(args)
    node_count = len(board_ids)
    selected_links = (list(range(node_count)) if args.all_links else
                      [args.link_index])
    trial_count = len(selected_links) * args.repeats
    if args.epoch + trial_count - 1 > 255:
        raise SystemExit("repeat matrix exceeds the uint8 training epoch range")
    ordered = discover_ordered(args, board_ids)
    board_ids = list(args.board_id)
    root_out = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn02_data_matrix_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    root_out.mkdir(parents=True, exist_ok=True)
    if args.dry_run:
        return {
            "phase": "TRN-02D_REPEAT_MATRIX",
            "diagnostic_only": True,
            "node_ids_in_loop_order": board_ids,
            "repeats": args.repeats,
            "calibration_generation": args.generation,
            "passed": False,
            "dry_run": True,
        }

    actions = ([] if args.reuse_ring_identity else
               prepare_ring(ordered, args))
    trials: list[dict[str, object]] = []
    trial_index = 0
    for link in selected_links:
        for repeat_index in range(1, args.repeats + 1):
            trial_args = argparse.Namespace(**vars(args))
            trial_args.link_index = link
            (trial_args.source_node,
             trial_args.destination_node) = direction_endpoints(
                 link, node_count, args.marker_direction)
            (trial_args.data_source_node,
             trial_args.data_destination_node) = direction_endpoints(
                 link, node_count, args.data_direction)
            trial_args.epoch = args.epoch + trial_index
            trial_args.sequence = trial_args.epoch
            trial_args.out_dir = root_out / (
                f"link{link}_repeat{repeat_index}_e{trial_args.epoch}")
            trial_args.out_dir.mkdir(parents=True, exist_ok=True)
            try:
                trial = run_link_trial(trial_args, ordered, prepare=False)
            except Exception as exc:  # retain failed search evidence
                trial = {
                    "phase": "TRN-02B",
                    "link": link,
                    "source_node": trial_args.source_node,
                    "destination_node": trial_args.destination_node,
                    "marker_source_node": trial_args.source_node,
                    "marker_destination_node": trial_args.destination_node,
                    "data_source_node": trial_args.data_source_node,
                    "data_destination_node": trial_args.data_destination_node,
                    "marker_direction": args.marker_direction,
                    "data_direction": args.data_direction,
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
            print(json.dumps({
                "trn02_trial": {
                    "link": link,
                    "repeat_index": repeat_index,
                    "epoch": trial_args.epoch,
                    "passed": bool(trial.get("passed")),
                    "residual_offset_sample": trial.get(
                        "resolved_offset_sample_count"),
                    "calibrated_offset_sample": trial.get(
                        "calibrated_data_offset_sample_count"),
                    "error": trial.get("error", ""),
                }
            }, ensure_ascii=False), flush=True)
            trial_index += 1
            time.sleep(args.gap)

    matrix = summarize_repeat_matrix(
        trials, node_count, args.repeats, args.max_offset_span,
        selected_links=selected_links)
    return {
        "phase": ("TRN-02D_REPEAT_MATRIX" if args.all_links else
                  "TRN-02B_SELECTED_LINK_REPEAT"),
        "diagnostic_only": True,
        "node_ids_in_loop_order": board_ids,
        "boards": {board.address: asdict(board) for board in ordered},
        "repeats": args.repeats,
        "selected_links": selected_links,
        "max_offset_span_sample": args.max_offset_span,
        "calibration_generation": args.generation,
        "reused_ring_identity": bool(args.reuse_ring_identity),
        "training_parameters": {
            "marker_direction": args.marker_direction,
            "data_direction": args.data_direction,
            "node_marker_offset_samples": list(
                args.node_marker_offset_samples),
            "node_data_offset_samples": list(
                args.node_data_offset_samples),
            "sample_period_ns": args.sample_period_ns,
            "link_delay_ns_by_link": list(args.link_delay_ns),
            "link_base_delay_ns_by_link": list(args.link_base_delay_ns),
        },
        "initial_epoch": args.epoch,
        "actions": actions,
        "matrix": matrix,
        "trials": trials,
        "passed": bool(matrix["passed"]),
    }


def main() -> int:
    args = parse_args()
    args.keep_open = not args.short_open
    args.out_dir = args.out_dir or (
        ROOT / "out" / "training" /
        f"trn02_data_link{args.link_index}_"
        f"{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    args.board_ids = list(args.board_id)
    guard_boards = discover(args)
    missing = set(args.board_id) - set(guard_boards)
    if missing:
        raise SystemExit(
            f"boards not found by *IDN?: {', '.join(sorted(missing))}")
    load_guard = CalibrationLoadGuard(
        [guard_boards[address] for address in args.board_id], args)
    args.discovered_boards = guard_boards
    with load_guard:
        result = (run_repeat_matrix(args)
                  if args.all_links or args.repeats > 1 else run_hil(args))
    result["realtime_calibration_load"] = load_guard.evidence()
    result["passed"] = (bool(result.get("passed")) and
                        bool(load_guard.evidence()["passed"]))
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    (args.out_dir / "summary.json").write_text(
        encoded + "\n", encoding="utf-8")
    return 0 if bool(result.get("passed")) else 1


if __name__ == "__main__":
    raise SystemExit(main())
