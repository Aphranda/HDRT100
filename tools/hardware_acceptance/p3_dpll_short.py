"""Four-board 60-second DPLL evidence and phase severity policy."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re
from fractions import Fraction
from typing import Any
import zlib

from tools.common.host_evidence_clock import validate as validate_host_clock


SCHEMA = "HAOFV_P3_DPLL_SHORT_V4"
PROVISIONAL_SCHEMA = "HAOFV_P3_JOINT_PROVISIONAL_V1"
PHASE_WARN_NS = 100.0
PHASE_ERROR_NS = 1000.0
PHASE_WORD_COUNT = 52
U32_FIELD_COUNT = 26
INTERNAL_SETTLING_SECONDS = 10
INTERNAL_SUMMARY_INTERVAL_MS = 10000
INTERNAL_MAX_SUCCESS_GAP_S = 1.0
HOST_RAW_RATE_TOLERANCE_PPM = 5000


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def evidence_entry(root: Path, path: Path) -> dict[str, str]:
    resolved = path.resolve()
    return {"path": resolved.relative_to(root.resolve()).as_posix(),
            "sha256": sha256_file(resolved)}


def _evidence_path(root: Path, entry: Any, label: str) -> Path:
    if not isinstance(entry, dict):
        raise ValueError(f"missing {label} evidence")
    relative, expected = entry.get("path"), entry.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise ValueError(f"invalid {label} evidence")
    path = (root / relative).resolve()
    if not path.is_file() or sha256_file(path) != expected:
        raise ValueError(f"{label} evidence is missing or changed")
    return path


def _signed64(lo: int, hi: int) -> int:
    value = lo | (hi << 32)
    return value - (1 << 64) if value & (1 << 63) else value


def parse_phase_status(raw: str) -> dict[str, int]:
    try:
        words = [int(token.strip(), 0) for token in raw.split(",")]
    except (AttributeError, ValueError) as exc:
        raise ValueError("phase status contains a non-integer field") from exc
    if len(words) != PHASE_WORD_COUNT or any(not 0 <= value <= 0xFFFFFFFF for value in words):
        raise ValueError("phase status must contain 52 UInt32 words")
    signed = [_signed64(words[index], words[index + 1])
              for index in range(U32_FIELD_COUNT, U32_FIELD_COUNT + 8, 2)]
    return {"schema": words[0], "active": words[2], "session": words[6],
            "generation": words[7], "applied": words[12],
            "source_slot": words[18], "target_slot": words[19],
            "delta_ns": signed[0], "cumulative_ns": signed[1],
            "residual_lo": signed[2], "residual_hi": signed[3]}


def phase_severity(max_abs_ns: float) -> str:
    if not math.isfinite(max_abs_ns) or max_abs_ns < 0:
        raise ValueError("phase maximum must be finite and non-negative")
    if max_abs_ns > PHASE_ERROR_NS:
        return "ERROR"
    if max_abs_ns > PHASE_WARN_NS:
        return "WARN"
    return "INFO"


def _decode_internal_trace(data: bytes, capture_id: int) -> dict[str, Any]:
    from tools.vdc_priority_trace.vdc_priority_trace import decode
    return decode(data, capture_id)


def _reconstruct_read_pages(path: Path, capture_id: int) -> bytes:
    from tools.vdc_priority_trace.vdc_priority_trace import READ_MAX_BYTES, parse_page

    pages = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(pages, list) or not pages:
        raise ValueError("internal trace read pages are empty")
    output = bytearray()
    expected_total = expected_crc = None
    command_pattern = re.compile(
        r"^\s*SYST(?:EM)?:VDC:PRIORITY:TRACE:READ\?\s+"
        r"(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*$", re.IGNORECASE)
    expected_offset, expected_size = 0, 4
    for page in pages:
        if not isinstance(page, dict):
            raise ValueError("internal trace read page is invalid")
        command = command_pattern.fullmatch(str(page.get("command", "")))
        if (command is None or int(command.group(1)) != capture_id or
                int(command.group(2)) != expected_offset or
                int(command.group(3)) != expected_size):
            raise ValueError("internal trace read page command or offset differs")
        try:
            total, crc, payload = parse_page(
                str(page.get("response", "")), expected_offset, expected_size)
        except (IndexError, ValueError) as exc:
            raise ValueError("internal trace read page response is invalid") from exc
        if expected_total is None:
            expected_total, expected_crc = total, crc
        elif (total, crc) != (expected_total, expected_crc):
            raise ValueError("internal trace read page metadata differs")
        output.extend(payload)
        expected_offset += expected_size
        if expected_offset < expected_total:
            expected_size = min(READ_MAX_BYTES, expected_total - expected_offset)
    if (len(output) != expected_total or zlib.crc32(output) != expected_crc):
        raise ValueError("internal trace read pages fail length or CRC")
    return bytes(output)


def _assess_internal_trace(decoded: dict[str, Any], seconds: int) -> dict[str, Any]:
    from tools.vdc_priority_trace.vdc_priority_trace_capture import assess_capture
    return assess_capture(
        decoded, seconds, False,
        max_success_gap_s=INTERNAL_MAX_SUCCESS_GAP_S,
        summary_interval_ms=INTERNAL_SUMMARY_INTERVAL_MS)


def _internal_trace_summary(
        root: Path, input_report_path: Path, board_index: int, uid: str,
        board: dict[str, Any], endpoint: dict[str, int], seconds: int) -> dict[str, Any]:
    folder = input_report_path.parent / f"NO{board_index + 1}"
    raw_path = folder / "summary.ram.bin"
    decoded_path = folder / "decoded.json"
    pages_path = folder / "read-pages.json"
    capture_id = board.get("trace_capture_id")
    if isinstance(capture_id, bool) or not isinstance(capture_id, int) or capture_id <= 0:
        raise ValueError("missing internal trace capture identity")
    if not raw_path.is_file() or not decoded_path.is_file() or not pages_path.is_file():
        raise ValueError("internal trace artifacts are incomplete")
    native = raw_path.read_bytes()
    if _reconstruct_read_pages(pages_path, capture_id) != native:
        raise ValueError("internal trace read pages differ from native bytes")
    replay = _decode_internal_trace(native, capture_id)
    decoded = json.loads(decoded_path.read_text(encoding="utf-8"))
    if replay != decoded:
        raise ValueError("decoded internal trace differs from native bytes")
    status, records = decoded.get("status"), decoded.get("records")
    if not isinstance(status, dict) or not isinstance(records, list):
        raise ValueError("internal trace shape is invalid")
    interval_ms, tick_hz = status.get("sample_interval_ms"), status.get("tick_hz")
    expected_bins = math.ceil(seconds * 1000 / INTERNAL_SUMMARY_INTERVAL_MS)
    settling_bins = math.ceil(INTERNAL_SETTLING_SECONDS * 1000 /
                              INTERNAL_SUMMARY_INTERVAL_MS)
    # Core1 closes the open bin at STOP as PARTIAL|TERMINAL
    # (vdc_priority_trace_summary.inc priority_summary_commit). When STOP lands
    # just after the last interval boundary, that boundary has already committed
    # a full bin, so one extra terminal bin shorter than an interval follows
    # (p3-132149: NO2 bin 6, ~1 ms). Accept exactly that shape; the full bins
    # before it must still number expected_bins and none may be terminal.
    boundary_tail = False
    if (len(records) == expected_bins + 1 and isinstance(tick_hz, int) and
            not isinstance(tick_hz, bool) and tick_hz > 0 and
            all(isinstance(record, dict) for record in records)):
        tail, full = records[-1], records[:-1]
        interval_ticks = tick_hz * INTERNAL_SUMMARY_INTERVAL_MS // 1000
        if ({"PARTIAL", "TERMINAL"} <= set(tail.get("flag_names", [])) and
                tail["observed_end_raw"] - tail["observed_start_raw"] < interval_ticks and
                not any("TERMINAL" in record.get("flag_names", []) for record in full) and
                full[-1]["observed_end_raw"] - full[-1]["observed_start_raw"] >=
                interval_ticks):
            expected_bins += 1
            boundary_tail = True
    if (not decoded.get("schema", "").startswith("VDC_SUMMARY_TRACE_DECODE_V") or
            status.get("capture_id") != capture_id or status.get("mode") != 4 or
            status.get("session") != endpoint["session"] or
            status.get("generation") != endpoint["generation"] or
            status.get("local_slot") != board_index or
            status.get("reference_slot") != 0 or status.get("node_count") != 4 or
            isinstance(tick_hz, bool) or not isinstance(tick_hz, int) or tick_hz <= 0 or
            len(records) != expected_bins or len(records) <= settling_bins):
        raise ValueError("internal trace identity, mode, clock, or bin count is invalid")
    assessment = _assess_internal_trace(decoded, seconds)
    if not all(assessment.get(field) is True for field in (
            "schema_ok", "interval_ok", "terminal", "coverage_complete",
            "all_bins_have_success", "success_gap_passed")):
        raise ValueError("internal trace coverage or success-gap assessment failed")
    settling = records[:settling_bins]
    expected_settling_ticks = tick_hz * INTERNAL_SUMMARY_INTERVAL_MS // 1000
    if any((record["observed_end_raw"] - record["observed_start_raw"] <
            expected_settling_ticks or "PARTIAL" in record.get("flag_names", []) or
            "CLOCK_INVALID" in record.get("flag_names", [])) for record in settling):
        raise ValueError("internal settling bin is incomplete")
    steady = records[settling_bins:]
    for expected_index, record in enumerate(records):
        if not isinstance(record, dict) or record.get("bin_index") != expected_index:
            raise ValueError("internal trace bin sequence is invalid")
    for record in steady:
        if (record.get("residual_extrema_valid") is not True or
                record.get("coverage_incomplete") is not False or
                not isinstance(record.get("success_count"), int) or
                record["success_count"] <= 0 or
                not isinstance(record.get("residual_min_ns"), int) or
                not isinstance(record.get("residual_max_ns"), int) or
                record["residual_min_ns"] > record["residual_max_ns"]):
            raise ValueError("steady internal trace bin is incomplete")
    low = min(record["residual_min_ns"] for record in steady)
    high = max(record["residual_max_ns"] for record in steady)
    last = steady[-1]
    # The endpoint is the envelope of the last estimation window, which spans
    # many decisions; a boundary tail bin holds only the few after the last
    # full interval, so the window is bounded by the last full bin plus it.
    closing = steady[-2:] if boundary_tail and len(steady) >= 2 else [last]
    endpoint_consistent = (
        min(record["residual_min_ns"] for record in closing) <= endpoint["residual_lo"] <=
        endpoint["residual_hi"] <= max(record["residual_max_ns"] for record in closing))
    if not endpoint_consistent:
        raise ValueError("final endpoint is outside the last steady trace bin")
    observed_ticks = sum(
        record["observed_end_raw"] - record["observed_start_raw"]
        for record in steady)
    startup = records[0]
    bins = [{
        "bin_index": record["bin_index"],
        "settling": record["bin_index"] < settling_bins,
        "observed_start_raw": record["observed_start_raw"],
        "observed_end_raw": record["observed_end_raw"],
        "residual_min_ns": record.get("residual_min_ns"),
        "residual_max_ns": record.get("residual_max_ns"),
        "residual_extrema_valid": record.get("residual_extrema_valid") is True,
    } for record in records]
    return {
        "board_id": uid,
        "capture_id": capture_id,
        "schema": decoded["schema"],
        "sample_interval_ms": interval_ms,
        "tick_hz": tick_hz,
        "session": status["session"],
        "generation": status["generation"],
        "local_slot": status["local_slot"],
        "reference_slot": status["reference_slot"],
        "node_count": status["node_count"],
        "total_bins": len(records),
        "settling_seconds_excluded": INTERNAL_SETTLING_SECONDS,
        "settling_bins_excluded": settling_bins,
        "steady_bins": len(steady),
        "steady_observed_s": observed_ticks / tick_hz,
        "steady_residual_min_ns": low,
        "steady_residual_max_ns": high,
        "steady_max_abs_ns": float(max(abs(low), abs(high))),
        "startup_residual_min_ns": startup.get("residual_min_ns"),
        "startup_residual_max_ns": startup.get("residual_max_ns"),
        "startup_extrema_valid": startup.get("residual_extrema_valid") is True,
        "last_bin_residual_min_ns": last["residual_min_ns"],
        "last_bin_residual_max_ns": last["residual_max_ns"],
        "endpoint_within_last_bin": endpoint_consistent,
        "read_pages_verified": True,
        "bins": bins,
        "coverage_assessment": assessment,
        "native_trace": evidence_entry(root, raw_path),
        "decoded_trace": evidence_entry(root, decoded_path),
        "read_pages": evidence_entry(root, pages_path),
    }


def _fraction_floor(value: Fraction) -> int:
    return value.numerator // value.denominator


def _fraction_ceil(value: Fraction) -> int:
    return -((-value.numerator) // value.denominator)


def _validate_anchor(uid: str, raw: Any, label: str,
                     host_clock: dict[str, Any]) -> dict[str, Any]:
    if (not isinstance(raw, dict) or raw.get("schema") != "VDC_HOST_RAW_ANCHOR_V1" or
            raw.get("label") != label or raw.get("uid") != uid or
            raw.get("host_clock") != host_clock):
        raise ValueError(f"{label} host/raw anchor identity is invalid")
    start, end, bridge = raw.get("host_start_ns"), raw.get("host_end_ns"), raw.get("bridge")
    if (isinstance(start, bool) or not isinstance(start, int) or
            isinstance(end, bool) or not isinstance(end, int) or end < start or
            not isinstance(bridge, dict)):
        raise ValueError(f"{label} host/raw anchor shape is invalid")
    required = (
        "schema", "platform_supported", "configuration_supported", "bridge_valid",
        "clock_ready", "expected_hz", "cached_hz", "sdk_sys_hz",
        "timer0_sample_valid", "timer1_sample_valid", "timer0_sample_us",
        "timer1_sample_ticks", "raw_before", "local_ns", "raw_after", "tick_hz")
    if any(isinstance(bridge.get(name), bool) or not isinstance(bridge.get(name), int)
           for name in required):
        raise ValueError(f"{label} bridge fields are invalid")
    if (bridge["schema"] != 1 or
            any(bridge[name] != 1 for name in (
                "platform_supported", "configuration_supported", "bridge_valid",
                "clock_ready", "timer0_sample_valid", "timer1_sample_valid")) or
            bridge["expected_hz"] <= 0 or
            not (bridge["expected_hz"] == bridge["cached_hz"] ==
                 bridge["sdk_sys_hz"] == bridge["tick_hz"]) or
            bridge["raw_before"] >= bridge["raw_after"] or
            bridge["local_ns"] % 1000):
        raise ValueError(f"{label} bridge validity gate failed")
    return raw


def _host_projection_model(uid: str, board: dict[str, Any], trace: dict[str, Any],
                           host_clock: dict[str, Any]) -> dict[str, Any]:
    anchors = board.get("host_raw_anchors")
    if not isinstance(anchors, dict):
        raise ValueError("host/raw anchors are missing")
    pre = _validate_anchor(uid, anchors.get("pre"), "pre", host_clock)
    post = _validate_anchor(uid, anchors.get("post"), "post", host_clock)
    first, last = pre["bridge"], post["bridge"]
    dynamic = {
        "timer0_sample_valid", "timer1_sample_valid", "timer0_sample_us",
        "timer1_sample_ticks", "raw_before", "local_ns", "raw_after"}
    if ({key: value for key, value in first.items() if key not in dynamic} !=
            {key: value for key, value in last.items() if key not in dynamic}):
        raise ValueError("bridge configuration changed across acquisition")
    if (last["raw_before"] <= first["raw_after"] or
            post["host_start_ns"] <= pre["host_end_ns"]):
        raise ValueError("host/raw anchors are not ordered")
    if trace.get("tick_hz") != first["tick_hz"] or trace.get("tick_hz") != last["tick_hz"]:
        raise ValueError("native trace tick rate differs from bridge clock identity")
    bins = trace.get("bins")
    if not isinstance(bins, list) or not bins:
        raise ValueError("internal trace bins are missing")
    if (bins[0]["observed_start_raw"] < first["raw_after"] or
            bins[-1]["observed_end_raw"] > last["raw_before"]):
        raise ValueError("internal trace is not enclosed by host/raw anchors")

    pre_raw_center = Fraction(first["raw_before"] + first["raw_after"], 2)
    post_raw_center = Fraction(last["raw_before"] + last["raw_after"], 2)
    pre_host_center = Fraction(pre["host_start_ns"] + pre["host_end_ns"], 2)
    post_host_center = Fraction(post["host_start_ns"] + post["host_end_ns"], 2)
    measured = (post_host_center - pre_host_center) / (post_raw_center - pre_raw_center)
    expected = Fraction(1_000_000_000, first["tick_hz"])
    center_rate_error_ppm = float((measured - expected) / expected * 1_000_000)

    corners = []
    for raw0 in (first["raw_before"], first["raw_after"]):
        for host0 in (pre["host_start_ns"], pre["host_end_ns"]):
            for raw1 in (last["raw_before"], last["raw_after"]):
                for host1 in (post["host_start_ns"], post["host_end_ns"]):
                    if raw1 > raw0:
                        corners.append((raw0, host0, raw1, host1))
    corner_rate_errors = [
        float((Fraction(host1 - host0, raw1 - raw0) - expected) /
              expected * 1_000_000)
        for raw0, host0, raw1, host1 in corners]
    rate_error_ppm_worst = max(abs(value) for value in corner_rate_errors)
    if rate_error_ppm_worst > HOST_RAW_RATE_TOLERANCE_PPM:
        raise ValueError("host/raw anchor corner rate differs from admitted TIMER1 rate")

    def project(raw_tick: int) -> tuple[int, int]:
        values = [
            Fraction(host0) + Fraction(raw_tick - raw0, raw1 - raw0) * (host1 - host0)
            for raw0, host0, raw1, host1 in corners]
        return _fraction_floor(min(values)), _fraction_ceil(max(values))

    projected_bins = []
    for row in bins:
        start_lo, start_hi = project(row["observed_start_raw"])
        end_lo, end_hi = project(row["observed_end_raw"])
        if end_lo <= start_hi:
            raise ValueError("projected internal bin has no certain host-time interior")
        projected_bins.append({
            **row,
            "host_start_ns": [start_lo, start_hi],
            "host_end_ns": [end_lo, end_hi],
        })
    return {
        "board_id": uid,
        "tick_hz": first["tick_hz"],
        "center_rate_error_ppm": center_rate_error_ppm,
        "corner_rate_error_ppm": [min(corner_rate_errors), max(corner_rate_errors)],
        "worst_rate_error_ppm": rate_error_ppm_worst,
        "pre": {"host_ns": [pre["host_start_ns"], pre["host_end_ns"]],
                "raw_ticks": [first["raw_before"], first["raw_after"]],
                "local_ns": first["local_ns"]},
        "post": {"host_ns": [post["host_start_ns"], post["host_end_ns"]],
                 "raw_ticks": [last["raw_before"], last["raw_after"]],
                 "local_ns": last["local_ns"]},
        "bins": projected_bins,
    }


def _scope_record_interval(report: dict[str, Any], sample: dict[str, Any]) -> list[int]:
    """Enclose the exported RAW record, not merely the observed trigger reply."""
    def timestamp(value):
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError("scope command timestamp is invalid")
        return value

    commands = report.get("commands")
    if not isinstance(commands, list) or not commands:
        raise ValueError("scope command evidence is missing")
    for row in commands:
        if (not isinstance(row, dict) or row.get("error") or
                timestamp(row.get("start_ns")) > timestamp(row.get("end_ns"))):
            raise ValueError("scope command interval or result is invalid")
    release = [row for row in commands if row.get("command") == ":TRIG:EDGE:LEV 1.5"]
    if len(release) != 1:
        raise ValueError("scope trigger admission command is not unique")
    release = release[0]
    wait = [row for row in commands if row.get("command") == ":TRIG:STAT?" and
            row.get("raw") == "WAIT" and row["end_ns"] <= release["start_ns"]]
    stopped = [row for row in commands if row.get("command") == ":TRIG:STAT?" and
               row.get("raw") == "STOP" and row["start_ns"] >= release["end_ns"]]
    fresh = timestamp(report.get("fresh_wait_ns"))
    admitted = timestamp(report.get("trigger_admitted_ns"))
    complete = timestamp(report.get("trigger_complete_observed_ns"))
    if (not wait or not stopped or report.get("capture_complete") is not True or
            not max(row["end_ns"] for row in wait) <= fresh <= release["start_ns"] or
            not release["end_ns"] <= admitted <= complete or
            min(row["end_ns"] for row in stopped) > complete):
        raise ValueError("scope fresh WAIT/admission/STOP ordering is invalid")
    recorded = sample.get("acquisition_host_ns")
    if (not isinstance(recorded, list) or len(recorded) != 2 or
            any(isinstance(value, bool) or not isinstance(value, int) for value in recorded) or
            recorded != [admitted, complete]):
        raise ValueError("scope host-time interval differs from bound scope record")
    channels = report.get("channels", [])
    if [row.get("channel") for row in channels] != [1, 2, 3, 4]:
        raise ValueError("scope RAW interval requires four ordered channels")
    earliest = Fraction(0)
    axis = None
    for row in channels:
        try:
            preamble = [Fraction(value) for value in row["preamble"].split(",")]
        except (ValueError, ZeroDivisionError) as exc:
            raise ValueError("scope RAW time axis is invalid") from exc
        if len(preamble) != 10 or preamble[4] <= 0:
            raise ValueError("scope RAW time axis is invalid")
        if axis is not None and axis != preamble[2:7]:
            raise ValueError("scope RAW channel time axes differ")
        axis = preamble[2:7]
        blocks = row.get("blocks", [])
        if not blocks:
            raise ValueError("scope RAW block range is missing")
        for block in blocks:
            start, stop = timestamp(block.get("start")), timestamp(block.get("stop"))
            if not 1 <= start <= stop <= preamble[2]:
                raise ValueError("scope RAW block range is invalid")
            earliest = min(earliest, preamble[5] +
                           (start - 1 - preamble[6]) * preamble[4])
    # The device can trigger before write() returns. Its frozen record can
    # include samples preceding that trigger; round that extension outward.
    return [release["start_ns"] + _fraction_floor(earliest * 1_000_000_000),
            min(row["end_ns"] for row in stopped)]


def _time_correlate_scope(inner: dict[str, Any], traces: list[dict[str, Any]],
                          scope_reports: list[dict[str, Any]]) -> dict[str, Any]:
    boards, samples = inner.get("boards"), inner.get("scope_samples")
    if not isinstance(boards, dict) or not isinstance(samples, list):
        raise ValueError("scope correlation inputs are missing")
    plan, actions = inner.get("plan"), inner.get("actions")
    if (not isinstance(plan, dict) or not isinstance(actions, list) or
            inner.get("queries_during_acquisition") != 0 or
            plan.get("runtime_anchor_queries") != 0 or
            plan.get("core1_probe_work_added") is not False or
            plan.get("realtime_resource_seat_added") is not False):
        raise ValueError("nonintrusive probe boundary is not proven")
    host_clock = validate_host_clock(inner.get("host_clock"))
    bridge_command = "SYSTEM:VDC:FEEDBACK:BRIDGE?"
    start_command = "SYSTEM:TDMA:RING:START"
    stop_command = "SYSTEM:TDMA:RING:STOP"
    for row in actions:
        if not isinstance(row, dict):
            raise ValueError("raw action row is invalid")
        command = row.get("command")
        normalized = command.upper() if isinstance(command, str) else ""
        if "?" in normalized or normalized in (bridge_command, start_command, stop_command):
            start_ns, end_ns = row.get("start_ns"), row.get("end_ns")
            if (row.get("uid") not in boards or not normalized or
                    isinstance(start_ns, bool) or not isinstance(start_ns, int) or
                    isinstance(end_ns, bool) or not isinstance(end_ns, int) or end_ns < start_ns):
                raise ValueError("timed raw action identity or interval is invalid")
    pre_actions, post_actions, run_starts, run_stops = [], [], [], []
    for uid, board in boards.items():
        anchors = board.get("host_raw_anchors") if isinstance(board, dict) else None
        bridge_actions = [row for row in actions if isinstance(row, dict) and
                          row.get("uid") == uid and
                          str(row.get("command", "")).upper() == bridge_command]
        start_actions = [row for row in actions if isinstance(row, dict) and
                         row.get("uid") == uid and
                         str(row.get("command", "")).upper() == start_command]
        stop_actions = [row for row in actions if isinstance(row, dict) and
                        row.get("uid") == uid and
                        str(row.get("command", "")).upper() == stop_command]
        if (not isinstance(anchors, dict) or len(bridge_actions) != 2 or
                len(start_actions) != 1 or not stop_actions):
            raise ValueError("STOP-only bridge action set is incomplete")
        bridge_actions.sort(key=lambda row: row.get("start_ns", -1))
        start_action = start_actions[0]
        stop_action = max(stop_actions, key=lambda row: row.get("end_ns", -1))
        if (start_action.get("error") or stop_action.get("error") or
                str(start_action.get("raw", "")).strip().strip('"') != "OK" or
                str(stop_action.get("raw", "")).strip().strip('"') != "OK"):
            raise ValueError("START/STOP action did not complete successfully")
        for label, action in zip(("pre", "post"), bridge_actions):
            anchor = anchors.get(label)
            if (not isinstance(anchor, dict) or
                    any(isinstance(action.get(name), bool) or
                        not isinstance(action.get(name), int)
                        for name in ("start_ns", "end_ns")) or
                    not (anchor.get("host_start_ns") <= action["start_ns"] <=
                         action["end_ns"] <= anchor.get("host_end_ns"))):
                raise ValueError("host/raw anchor does not enclose its raw action")
            try:
                from tools.workflows.vdc_joint_capture.internal_seal import parse_bridge
                action_bridge = parse_bridge(str(action.get("raw", "")))
            except (TypeError, ValueError) as exc:
                raise ValueError("bridge action raw response is invalid") from exc
            if action.get("error") or action_bridge != anchor.get("bridge"):
                raise ValueError("structured bridge anchor differs from raw action")
        if (bridge_actions[0]["end_ns"] > start_action.get("start_ns", -1) or
                bridge_actions[1]["start_ns"] < stop_action.get("end_ns", 1 << 80)):
            raise ValueError("bridge query occurred inside the RUN interval")
        pre_actions.append(bridge_actions[0])
        post_actions.append(bridge_actions[1])
        run_starts.append(start_action)
        run_stops.append(stop_action)
    global_start_ns = min(row["start_ns"] for row in run_starts)
    global_stop_ns = max(row["end_ns"] for row in run_stops)
    if (max(row["end_ns"] for row in pre_actions) > global_start_ns or
            min(row["start_ns"] for row in post_actions) < global_stop_ns):
        raise ValueError("bridge anchors do not enclose the all-board RUN interval")
    runtime_queries = [
        row for row in actions if isinstance(row, dict) and "?" in str(row.get("command", "")) and
        row["end_ns"] > global_start_ns and row["start_ns"] < global_stop_ns]
    if runtime_queries:
        raise ValueError("raw actions contain a query inside the all-board RUN interval")
    models = [
        _host_projection_model(
            trace["board_id"], boards.get(trace["board_id"], {}), trace, host_clock)
        for trace in traces]
    expected_due = list(range(5, 61, 5))
    if [sample.get("due_s") if isinstance(sample, dict) else None for sample in samples] != expected_due:
        raise ValueError("scope due sequence differs from the fixed 5..60 second schedule")
    if len(scope_reports) != len(samples):
        raise ValueError("scope record/sample count differs")
    rows = []
    unique_steady_by_board = {model["board_id"]: 0 for model in models}
    outside_by_board = {model["board_id"]: 0 for model in models}
    in_capture_by_board = {model["board_id"]: 0 for model in models}
    ambiguous_by_board = {model["board_id"]: 0 for model in models}
    for sample, report in zip(samples, scope_reports):
        if report.get("host_clock") != host_clock:
            raise ValueError("scope record host clock differs")
        interval = _scope_record_interval(report, sample)
        relative = sample.get("phase", {}).get("relative_ns") if isinstance(sample, dict) else None
        if (not isinstance(interval, list) or len(interval) != 2 or
                any(isinstance(value, bool) or not isinstance(value, int) for value in interval) or
                interval[1] < interval[0] or not isinstance(relative, list) or len(relative) != 3):
            raise ValueError("scope sample lacks a valid host-time interval")
        for index, model in enumerate(models):
            channel = relative[index]
            if (not isinstance(channel, list) or not channel or
                    any(isinstance(value, bool) or not isinstance(value, (int, float)) or
                        not math.isfinite(value) for value in channel)):
                raise ValueError("scope correlation phase channel is invalid")
            candidates, certain = [], []
            for bin_row in model["bins"]:
                if (interval[1] >= bin_row["host_start_ns"][0] and
                        interval[0] <= bin_row["host_end_ns"][1]):
                    candidates.append(bin_row["bin_index"])
                if (interval[0] >= bin_row["host_start_ns"][1] and
                        interval[1] <= bin_row["host_end_ns"][0]):
                    certain.append(bin_row["bin_index"])
            unique_bin = (certain[0]
                          if len(candidates) == 1 and certain == candidates else None)
            selected = next((row for row in model["bins"]
                             if row["bin_index"] == unique_bin), None)
            before_capture = interval[1] < model["bins"][0]["host_start_ns"][0]
            after_capture = interval[0] > model["bins"][-1]["host_end_ns"][1]
            if unique_bin is not None:
                association = "AFFINE_UNIQUE"
            elif candidates:
                association = "AFFINE_AMBIGUOUS"
            elif before_capture or after_capture:
                association = "OUTSIDE_CAPTURE"
                outside_by_board[model["board_id"]] += 1
            else:
                association = "UNMAPPED"
            if candidates:
                in_capture_by_board[model["board_id"]] += 1
            if association == "AFFINE_AMBIGUOUS":
                ambiguous_by_board[model["board_id"]] += 1
            if unique_bin is not None and selected is not None and not selected["settling"]:
                unique_steady_by_board[model["board_id"]] += 1
            rows.append({
                "due_s": sample.get("due_s"), "board_id": model["board_id"],
                "scope_host_ns": interval,
                "external_relative_ns": [float(min(channel)), float(max(channel))],
                "candidate_bins": candidates, "unique_bin": unique_bin,
                "association": association,
                "outside_direction": (
                    "BEFORE" if before_capture else "AFTER" if after_capture else None),
                "internal_residual_ns": (
                    [selected["residual_min_ns"], selected["residual_max_ns"]]
                    if selected is not None and selected["residual_extrema_valid"] else None),
                "settling_bin": selected["settling"] if selected is not None else None,
                "exact_event_correlation_qualified": False,
                "numeric_error_qualified": False,
            })
    invalid_outside = [
        row for row in rows if row["association"] == "OUTSIDE_CAPTURE" and
        (row["outside_direction"] != "AFTER" or row["due_s"] != 60)]
    if (any(row["association"] == "UNMAPPED" for row in rows) or invalid_outside or
            any(value > 1 for value in outside_by_board.values()) or
            any(value < 10 for value in in_capture_by_board.values()) or
            any(value < 5 for value in unique_steady_by_board.values())):
        raise ValueError("host/raw correlation lacks complete or steady unique associations")
    return {
        "schema": "HAOFV_DPLL_HOST_TIME_CORRELATION_V2",
        "probe_mode": "PASSIVE_SCOPE_PLUS_STOP_ONLY_DOUBLE_BRIDGE",
        "host_clock": host_clock,
        "scope_interval_basis": "ADMISSION_WRITE_START_PLUS_RAW_PRETRIGGER_TO_STOP_REPLY",
        "runtime_board_queries": 0,
        "core1_probe_work_added": False,
        "realtime_resource_seat_added": False,
        "affine_projection_assumption": True,
        "rate_tolerance_ppm": HOST_RAW_RATE_TOLERANCE_PPM,
        "evidence_valid": True,
        "exact_event_correlation_qualified": False,
        "numeric_error_qualified": False,
        "models": models,
        "associations": rows,
        "unique_steady_associations_by_board": unique_steady_by_board,
        "outside_capture_associations_by_board": outside_by_board,
        "in_capture_associations_by_board": in_capture_by_board,
        "ambiguous_associations_by_board": ambiguous_by_board,
    }


def _time_correlation_failure(inner: dict[str, Any], reason: str) -> dict[str, Any]:
    plan = inner.get("plan") if isinstance(inner.get("plan"), dict) else {}
    return {
        "schema": "HAOFV_DPLL_HOST_TIME_CORRELATION_V2",
        "probe_mode": "PASSIVE_SCOPE_PLUS_STOP_ONLY_DOUBLE_BRIDGE",
        "host_clock": inner.get("host_clock"),
        "runtime_board_queries": inner.get("queries_during_acquisition"),
        "core1_probe_work_added": plan.get("core1_probe_work_added"),
        "realtime_resource_seat_added": plan.get("realtime_resource_seat_added"),
        "affine_projection_assumption": True,
        "rate_tolerance_ppm": HOST_RAW_RATE_TOLERANCE_PPM,
        "evidence_valid": False,
        "reasons": [reason],
        "exact_event_correlation_qualified": False,
        "numeric_error_qualified": False,
        "models": [], "associations": [],
        "unique_steady_associations_by_board": {},
        "outside_capture_associations_by_board": {},
        "in_capture_associations_by_board": {},
        "ambiguous_associations_by_board": {},
    }


def _flatten_scope_phase(samples: Any, expected_count: int) -> list[float]:
    if not isinstance(samples, list) or len(samples) != expected_count:
        raise ValueError("scope sample count is incomplete")
    values: list[float] = []
    for row in samples:
        phase = row.get("phase", {}) if isinstance(row, dict) else {}
        relative = phase.get("relative_ns")
        if (row.get("capture_complete") is not True or row.get("missed") or
                not isinstance(relative, list) or len(relative) != 3):
            raise ValueError("scope sample is missing or incomplete")
        for channel in relative:
            if not isinstance(channel, list) or not channel:
                raise ValueError("scope phase channel is empty")
            for value in channel:
                if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                        not math.isfinite(value)):
                    raise ValueError("scope phase value is invalid")
                values.append(float(value))
    if not values:
        raise ValueError("scope phase samples are empty")
    return values


def summarize_joint(root: Path, joint_report_path: Path, input_report_path: Path,
                    *, scope_enabled: bool, seconds: int) -> dict[str, Any]:
    root = root.resolve()
    outer = json.loads(joint_report_path.read_text(encoding="utf-8"))
    inner = json.loads(input_report_path.read_text(encoding="utf-8"))
    errors: list[str] = []
    if (outer.get("passed") is not True or outer.get("complete") is not True or
            outer.get("main_completed") is not True):
        errors.append("outer lifecycle did not complete successfully")
    if outer.get("errors") or outer.get("cleanup_errors") or outer.get("restore_needed"):
        errors.append("outer lifecycle reported execution, cleanup, or restore failure")
    source_tree_sha256 = outer.get("source_tree_sha256")
    build_id = outer.get("receipt_build")
    workflow_inputs = outer.get("workflow_inputs")
    board_uids = (workflow_inputs.get("board_uids")
                  if isinstance(workflow_inputs, dict) else None)
    if (not isinstance(source_tree_sha256, str) or len(source_tree_sha256) != 64 or
            not isinstance(build_id, str) or not build_id.isdigit() or
            not isinstance(board_uids, list) or len(board_uids) != 4 or
            len(set(board_uids)) != 4):
        errors.append("outer source, build, or board identity is invalid")
    verdict = inner.get("joint_verdict", {})
    plan = inner.get("plan", {})
    if (inner.get("passed") is not True or inner.get("joint_internal_health_passed") is not True or
            verdict.get("internal_health_passed") is not True or inner.get("errors")):
        errors.append("four-board internal health did not pass")
    elapsed = inner.get("quiet_elapsed_s")
    if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or elapsed < seconds:
        errors.append("internal observation did not cover the requested duration")

    internal_rows, internal_values, internal_traces, steady_values = [], [], [], []
    boards = inner.get("boards")
    if not isinstance(boards, dict) or len(boards) != 4:
        errors.append("internal report does not contain four boards")
    else:
        if list(boards) != board_uids:
            errors.append("internal board order differs from admitted workflow inputs")
        for index, (uid, board) in enumerate(boards.items()):
            if index == 0:
                continue
            try:
                phase = parse_phase_status(board.get("final_phase_raw"))
                if (phase["schema"] != 1 or phase["active"] != 0 or phase["applied"] <= 0 or
                        phase["source_slot"] != 0 or phase["target_slot"] != index or
                        phase["residual_lo"] > phase["residual_hi"]):
                    raise ValueError("phase endpoint identity or state is invalid")
                maximum = float(max(abs(phase["residual_lo"]), abs(phase["residual_hi"])))
                internal_values.append(maximum)
                internal_rows.append({"board_id": uid,
                    "residual_lo_ns": phase["residual_lo"],
                    "residual_hi_ns": phase["residual_hi"], "max_abs_ns": maximum})
                trace_summary = _internal_trace_summary(
                    root, input_report_path, index, uid, board, phase, seconds)
                internal_traces.append(trace_summary)
                steady_values.append(trace_summary["steady_max_abs_ns"])
            except (AttributeError, TypeError, ValueError) as exc:
                errors.append(f"{uid}: {exc}")
    bindings = {(row["session"], row["generation"]) for row in internal_traces}
    if internal_traces and len(bindings) != 1:
        errors.append("follower internal traces do not share one session/generation")

    raw_blocks: list[dict[str, str]] = []
    scope_records: list[dict[str, str]] = []
    time_correlation = None
    if scope_enabled:
        try:
            external_values = _flatten_scope_phase(inner.get("scope_samples"), seconds // 5)
        except ValueError as exc:
            external_values = []
            errors.append(str(exc))
        if verdict.get("external_status") != "PASS":
            errors.append("scope-enabled run did not report external PASS")
        for path in sorted(input_report_path.parent.glob("sample-*/*.block")):
            raw_blocks.append(evidence_entry(root, path))
        for path in sorted(input_report_path.parent.glob("sample-*/scope.json")):
            scope_records.append(evidence_entry(root, path))
        if len(raw_blocks) != (seconds // 5) * 4:
            errors.append("scope RAW block set is incomplete")
        if len(scope_records) != seconds // 5:
            errors.append("scope record set is incomplete")
        try:
            scope_reports = [json.loads((root / row["path"]).read_text(encoding="utf-8"))
                             for row in scope_records]
            time_correlation = _time_correlate_scope(inner, internal_traces, scope_reports)
        except (KeyError, TypeError, ValueError) as exc:
            time_correlation = _time_correlation_failure(inner, str(exc))
        phase_source = "scope_samples[].phase.relative_ns"
        phase_domain = "external_physical_sampled_windows"
        phase_values = external_values
        external_status = "PASS" if not errors else "ERROR"
    else:
        if verdict.get("external_status") != "SKIPPED" or inner.get("scope_samples"):
            errors.append("scope-absent run must record external SKIPPED without samples")
        # Lock is a convergence property: grade the converged steady endpoint
        # residual, which _internal_trace_summary already binds to the last
        # steady estimation bin. The window residual extrema stay in the
        # evidence as diagnostics: a re-acquisition sample carries the age of
        # the reference event it matched (vdc_priority_match.inc expected_lo),
        # so it reports the matching domain, not the locked phase.
        phase_source = "NO2..NO4 final steady endpoint residual (last steady bin)"
        phase_domain = "internal_steady_endpoint_residual"
        phase_values = internal_values
        external_status = "SKIPPED"

    if not phase_values:
        errors.append("selected phase source contains no samples")
    max_abs_ns = max(map(abs, phase_values), default=0.0)
    severity = phase_severity(max_abs_ns)
    if severity == "ERROR":
        errors.append(f"phase exceeds {PHASE_ERROR_NS:g} ns")
    return {"schema": SCHEMA, "passed": not errors and severity != "ERROR",
        "evidence_status": "ERROR" if errors else "PASS",
        "source_tree_sha256": source_tree_sha256,
        "build_id": build_id,
        "board_uids": board_uids,
        "duration_s": seconds, "scope_enabled": scope_enabled,
        "external_status": external_status, "phase_source": phase_source,
        "phase_domain": phase_domain, "phase_warn_ns": PHASE_WARN_NS,
        "phase_error_ns": PHASE_ERROR_NS, "phase_max_abs_ns": max_abs_ns,
        "phase_severity": severity, "internal_phase_endpoints": internal_rows,
        "phase_window_extrema_ns": max(map(abs, steady_values), default=0.0),
        "internal_phase_traces": internal_traces,
        "internal_summary_interval_ms": INTERNAL_SUMMARY_INTERVAL_MS,
        "internal_settling_seconds_excluded": INTERNAL_SETTLING_SECONDS,
        "scope_resource": plan.get("scope_resource") if scope_enabled else None,
        "scope_serial": plan.get("scope_serial") if scope_enabled else None,
        "time_correlation": time_correlation,
        "joint_report": evidence_entry(root, joint_report_path),
        "joint_input_report": evidence_entry(root, input_report_path),
        "scope_records": scope_records, "scope_raw_blocks": raw_blocks,
        "errors": errors}


def _analyze_scope_memory(report: dict[str, Any], blocks: dict[str, bytes]) -> dict[str, Any]:
    from tools.common.hdo_scope.reader import decode_block
    from tools.vdc_priority_trace.vdc_scope_evidence import analyze_memory
    return analyze_memory(report, blocks, decode_block)


def _validate_scope_decode(root: Path, summary: dict[str, Any], input_path: Path) -> None:
    if summary.get("scope_enabled") is not True:
        if summary.get("scope_records") or summary.get("scope_raw_blocks"):
            raise ValueError("scope-disabled summary contains external artifacts")
        return
    inner = json.loads(input_path.read_text(encoding="utf-8"))
    samples = inner.get("scope_samples", [])
    records = summary.get("scope_records", [])
    if len(records) != len(samples):
        raise ValueError("scope record/sample count differs")
    for index, (entry, sample) in enumerate(zip(records, samples)):
        path = _evidence_path(root, entry, f"scope record {index}")
        report = json.loads(path.read_text(encoding="utf-8"))
        host_clock = inner.get("host_clock")
        interval = sample.get("acquisition_host_ns") if isinstance(sample, dict) else None
        expected_interval = [report.get("trigger_admitted_ns"),
                             report.get("trigger_complete_observed_ns")]
        if (report.get("capture_complete") is not True or
                report.get("host_clock") != host_clock or
                any(isinstance(value, bool) or not isinstance(value, int)
                    for value in expected_interval) or
                expected_interval[1] < expected_interval[0] or
                interval != expected_interval):
            raise ValueError("scope host-time interval differs from bound scope record")
        blocks = {}
        for channel in report.get("channels", []):
            for block in channel.get("blocks", []):
                block_path = path.parent / block["path"]
                blocks[block["path"]] = block_path.read_bytes()
        decoded = _analyze_scope_memory(report, blocks)
        if decoded.get("relative_ns") != sample.get("phase", {}).get("relative_ns"):
            raise ValueError("scope phase differs from offline RAW decode")


def validate_summary(
        root: Path, summary: dict[str, Any], *,
        expected_source_tree_sha256: str | None = None,
        expected_build_id: str | None = None,
        expected_board_uids: list[str] | None = None) -> None:
    if summary.get("schema") != SCHEMA:
        raise ValueError("DPLL short summary schema is invalid")
    for actual, expected, label in (
        (summary.get("source_tree_sha256"), expected_source_tree_sha256, "source"),
        (summary.get("build_id"), expected_build_id, "build"),
        (summary.get("board_uids"), expected_board_uids, "board"),
    ):
        if expected is not None and actual != expected:
            raise ValueError(f"DPLL short {label} identity differs from receipt")
    report_path = _evidence_path(root, summary.get("joint_report"), "joint report")
    input_path = _evidence_path(root, summary.get("joint_input_report"), "joint input report")
    for index, entry in enumerate(summary.get("scope_records", [])):
        _evidence_path(root, entry, f"scope record {index}")
    for index, entry in enumerate(summary.get("scope_raw_blocks", [])):
        _evidence_path(root, entry, f"scope RAW block {index}")
    for index, row in enumerate(summary.get("internal_phase_traces", [])):
        if not isinstance(row, dict):
            raise ValueError("internal trace row is invalid")
        for field in ("native_trace", "decoded_trace", "read_pages"):
            _evidence_path(root, row.get(field), f"internal trace {index} {field}")
    _validate_scope_decode(root, summary, input_path)
    recomputed = summarize_joint(root, report_path, input_path,
        scope_enabled=summary.get("scope_enabled") is True,
        seconds=int(summary.get("duration_s", 0)))
    if recomputed != summary:
        raise ValueError("DPLL short summary differs from raw joint evidence")
    if (summary.get("passed") is not True or summary.get("evidence_status") != "PASS" or
            summary.get("phase_severity") == "ERROR"):
        raise ValueError("DPLL short evidence contains a blocking result")


def validate_provisional_receipt(root: Path, receipt: dict[str, Any],
                                 board_uids: list[str]) -> None:
    if (receipt.get("schema") != PROVISIONAL_SCHEMA or
            receipt.get("joint_capture_only") is not True or
            receipt.get("passed") is not True or
            receipt.get("tdma_board_ids") != board_uids or
            receipt.get("ota_board_ids") != board_uids or
            not str(receipt.get("build_id", "")).isdigit()):
        raise ValueError("invalid P3 joint provisional receipt")
    from tools.hardware_acceptance import p3_hardware_acceptance as gate
    if gate.working_source_fingerprint(root) != (
            receipt.get("source_tree_sha256"), receipt.get("source_file_count")):
        raise ValueError("provisional receipt source fingerprint differs")
    paths = {}
    for name in ("firmware_package", "ota_summary", "trn03_matrix", "tdma_summary",
                 "tdma_stopped_handoff"):
        paths[name] = _evidence_path(root, receipt.get(name), name)
    if gate._read_package_build_id(paths["firmware_package"]) != receipt["build_id"]:
        raise ValueError("provisional package build differs")
    ota = json.loads(paths["ota_summary"].read_text(encoding="utf-8"))
    if ota.get("schema") == "HAOFV_PICOTOOL_FLASH_EVIDENCE_V1":
        # picotool is the inner-loop flashing route: its evidence carries no
        # OTA block capability, so validate what it does guarantee -- every
        # expected board flashed from the one artifact with returncode 0.
        flashed = {row.get("serial_number"): row.get("returncode")
                   for row in ota.get("boards", [])}
        if (ota.get("passed") is not True or ota.get("failed_count") != 0 or
                set(flashed) != set(board_uids) or
                any(code != 0 for code in flashed.values())):
            raise ValueError("provisional picotool flash evidence is invalid")
    else:
        gate.validate_ota(ota, board_uids, receipt["build_id"])
    matrix = json.loads(paths["trn03_matrix"].read_text(encoding="utf-8"))
    tdma = json.loads(paths["tdma_summary"].read_text(encoding="utf-8"))
    if (isinstance(receipt.get("calibration_generation"), bool) or
            not isinstance(receipt.get("calibration_generation"), int) or
            receipt["calibration_generation"] <= 0 or
            matrix.get("calibration_generation") != receipt["calibration_generation"]):
        raise ValueError("provisional calibration generation differs")
    if not gate.training_inputs_read_back(matrix, tdma, board_uids):
        raise ValueError("provisional TRN-03/TDMA readback is invalid")
    proof = json.loads(paths["tdma_stopped_handoff"].read_text(encoding="utf-8"))
    expected = gate.validate_tdma_stopped_handoff(
        root, tdma, board_uids, receipt["build_id"])
    if proof != expected or proof.get("passed") is not True:
        raise ValueError("provisional stopped handoff differs from raw records")
