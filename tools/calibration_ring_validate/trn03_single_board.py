#!/usr/bin/env python3
"""Single-board diagnostic handoff; never synthesizes a production phase matrix."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from calibration_ring_validate import calibration_marker_loopback as marker  # noqa: E402
from calibration_ring_validate import calibration_sck_loopback as sck  # noqa: E402
import tdma_single_board_loopback as tdma  # noqa: E402


SCHEMAS = {
    "marker": "HAOFV_MARKER_LOOP_DIAGNOSTIC_V1",
    "sck": "HAOFV_SCK_LOOP_DIAGNOSTIC_V1",
    "data": "HAOFV_DATA_LOOP_DIAGNOSTIC_V1",
}
NON_HARDWARE_FLAGS = ("dry_run", "replay", "replay_only", "simulated", "synthetic",
                      "forced_continue", "diagnostic_forced_continue")


def ensure_hardware_report(value, path="report"):
    if isinstance(value, dict):
        for key, item in value.items():
            if key in NON_HARDWARE_FLAGS and item:
                raise ValueError(f"{path}.{key} is not hardware acceptance evidence")
            ensure_hardware_report(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            ensure_hardware_report(item, f"{path}[{index}]")


def evaluate_trial(kind, trial):
    if kind == "data":
        raise ValueError("DATA raw evaluator unavailable; no phase-matrix fallback permitted")
    module = marker if kind == "marker" else sck
    saved = trial["status"]
    fields = marker.V2_FIELDS if kind == "marker" else sck.FIELDS
    status = module.parse_status(",".join(str(saved[field]) for field in fields))
    # Reassemble u64 edges from the raw status fields, not cached derived timestamps.
    if any(saved.get(field) != value for field, value in status.items()):
        raise ValueError(f"{kind} status fields or derived timestamps disagree")
    capture = trial["capture"]
    expected_schema = f"HAOFV_{'MARKER' if kind == 'marker' else 'SCK'}_LOOP_RAW_V1"
    if (capture.get("schema") != expected_schema or capture.get("epoch") != trial["epoch"] or
            capture.get("capture_generation") != status["capture_generation"] or
            not status["capture_generation"] or capture.get("gpio_base") != 24 or
            capture.get("sample_bits") != 8 or capture.get("sample_period_ns") != 4):
        raise ValueError(f"{kind} raw capture identity mismatch")
    words = capture.get("words")
    if (not isinstance(words, list) or not words or
            any(type(word) is not int or not 0 <= word <= 0xFFFFFFFF for word in words)):
        raise ValueError(f"{kind} raw words missing or invalid")
    kwargs = dict(epoch=trial["epoch"], codebook=status["codebook"],
                  requested_words=status["requested_words"])
    if kind == "marker":
        result = marker.evaluate(status, words, **kwargs, require_full_marker=True,
                                 fault_idle_high=False)
    else:
        result = sck.evaluate(status, words, **kwargs, offset_samples=trial["offset_samples"],
                              fault_idle_low=False)
    if result.get("passed") is not True:
        raise ValueError(f"{kind} raw revalidation failed: {result.get('failures')}")
    return result


def validate_acquisition_transcript(report: dict, kind: str) -> dict:
    """Consume each acquisition exactly once, including all downloaded raw words."""
    if kind == "data":
        raise ValueError("DATA raw evaluator unavailable; no phase-matrix fallback permitted")
    module = marker if kind == "marker" else sck
    aliases = {"CALIBRATION": "CAL", "MARKER": "MARK", "SYSTEM": "SYST",
               "STATUS": "STAT"}
    entries = []
    for item in report["transcript"]:
        if (not isinstance(item, dict) or not isinstance(item.get("command"), str) or
                not isinstance(item.get("response"), str) or item.get("exception")):
            raise ValueError("invalid acquisition transcript entry")
        parts = item["command"].strip().split(maxsplit=1)
        if not parts:
            raise ValueError("empty transcript command")
        header = ":".join(aliases.get(token.rstrip("?"), token.rstrip("?")) +
                          ("?" if token.endswith("?") else "")
                          for token in parts[0].upper().split(":"))
        entries.append((header, parts[1] if len(parts) == 2 else "", item["response"]))
    cursor = 0

    def take(header, argument=""):
        nonlocal cursor
        if cursor >= len(entries) or entries[cursor][:2] != (header, argument):
            raise ValueError(f"transcript[{cursor}] expected {header} {argument}")
        response = entries[cursor][2]
        cursor += 1
        return response

    prefix = f"CAL:{'MARK' if kind == 'marker' else 'SCK'}:LOOP"
    status_query = f"READ:{prefix}?"

    def wait_status(states, expected=None):
        while True:
            status = module.parse_status(take(status_query))
            if status["state"] in states:
                if expected is not None and status != expected:
                    raise ValueError(f"transcript[{cursor - 1}] status/report mismatch")
                return status

    def stop(expected=None):
        if take(f"{prefix}:STOP").strip('"') != "OK":
            raise ValueError("transcript STOP acknowledgement mismatch")
        return wait_status({0}, expected)

    take("SYST:TDMA:RING:STOP")
    while True:
        ring = marker.parse_status_named(take("SYST:REFMEM:SYNC:TDMA:STAT?"))
        if ring["ring_up_running"] == 0 and ring["ring_down_running"] == 0:
            break
    bindings = []
    for index, trial in enumerate(report["trials"]):
        start_index = cursor
        status = trial["status"]
        stop()
        request = [status["codebook"], trial["epoch"], status["requested_words"]]
        if kind == "sck":
            request.append(trial["offset_samples"])
        request.append(0)
        response = take(f"{prefix}:START", ",".join(map(str, request)))
        if [int(value) for value in next(csv.reader([response]))] != request:
            raise ValueError("transcript START acknowledgement mismatch")
        wait_status({2, 3}, status)
        downloaded = []
        words = trial["capture"]["words"]
        while len(downloaded) < len(words):
            offset = len(downloaded)
            count = min(32, len(words) - offset)
            argument = ",".join(map(str, (trial["epoch"], offset, count,
                                         status["capture_generation"])))
            response = take(f"READ:{prefix}:DATA?", argument)
            downloaded.extend(marker.parse_data(response, epoch=trial["epoch"],
                                                total=status["produced_words"],
                                                offset=offset, count=count))
        if downloaded != words:
            raise ValueError(f"trial {index} transcript/raw words mismatch")
        if module.parse_status(take(status_query)) != status:
            raise ValueError(f"trial {index} transcript post-download snapshot mismatch")
        stop(trial["cleanup"])
        bindings.append(dict(trial=index, transcript_start=start_index,
                             transcript_end=cursor, downloaded_words=len(downloaded)))
    if take("*IDN?") != report["final_idn"]:
        raise ValueError("transcript final IDN mismatch")
    if take("SYST:FW:BUILD?").strip('"') != report["final_build"]:
        raise ValueError("transcript final build mismatch")
    if take("SYST:ERR?") != report["final_error"]:
        raise ValueError("transcript final error mismatch")
    if cursor != len(entries):
        raise ValueError("unbound trailing transcript entries")
    return dict(passed=True, bound_entries=cursor, trials=bindings)


def preflight_input(path: Path, kind: str, board_id: str, build: str) -> dict:
    evidence = {"path": str(path.resolve()), "kind": kind, "passed": False, "trials": []}
    try:
        payload = path.read_bytes()
        evidence["sha256"] = hashlib.sha256(payload).hexdigest()
        report = json.loads(payload.decode("utf-8"))
        if not isinstance(report, dict):
            raise ValueError(f"{kind} report must be an object")
        ensure_hardware_report(report)
        if (report.get("schema") != SCHEMAS[kind] or report.get("passed") is not True or
                report.get("physical_board_count") != 1 or report.get("logical_slot_count") != 2 or
                report.get("production_training_certified") is not False or
                report.get("parameter_flash_saved") is not False):
            raise ValueError(f"{kind} report is not passed single-board dual-slot diagnostic evidence")
        board = report.get("board", {})
        if not isinstance(board, dict):
            raise ValueError(f"{kind} board identity must be an object")
        if board.get("address") != board_id or board.get("build") != build:
            raise ValueError(f"{kind} board UID/build mismatch")
        idn = board.get("idn", "")
        if (len(idn.split(',')) < 3 or idn.split(',')[2].strip() != board_id or
                report.get("final_idn") != idn or report.get("final_build") != build or
                not str(report.get("final_error", "")).startswith('0,')):
            raise ValueError(f"{kind} final identity/error verification missing")
        if not isinstance(report.get("transcript"), list) or not report["transcript"]:
            raise ValueError(f"{kind} acquisition transcript missing")
        if report.get("fault_idle_high") or report.get("fault_idle_low"):
            raise ValueError(f"{kind} negative fault run cannot seed T3")
        trials = report.get("trials")
        if not isinstance(trials, list) or not trials:
            raise ValueError(f"{kind} trials missing")
        identities = set()
        for index, trial in enumerate(trials):
            if not isinstance(trial, dict):
                raise ValueError(f"{kind} trial {index} must be an object")
            if (trial.get("passed") is not True or trial.get("error") or trial.get("cleanup_error") or
                    trial.get("cleanup", {}).get("state") != 0):
                raise ValueError(f"{kind} trial {index} failed or cleanup unconfirmed")
            identity = (trial["epoch"], trial["status"].get("capture_generation"))
            if identity in identities:
                raise ValueError(f"{kind} duplicate acquisition identity")
            identities.add(identity)
            evidence["trials"].append({"index": index, "epoch": trial["epoch"],
                                        "revalidated": evaluate_trial(kind, trial)})
        evidence["transcript_validation"] = validate_acquisition_transcript(report, kind)
        evidence.update(passed=True, board=board, trial_count=len(trials))
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as exc:
        evidence["error"] = f"{type(exc).__name__}: {exc}"
    return evidence


def child_command(args, port: str, out_dir: Path) -> list[str]:
    return [sys.executable, str(ROOT / "tools/tdma_ring_monitor/tdma_single_board_loopback.py"),
            port, "--expected-serial-number", args.board_id, "--expected-build", args.expected_build,
            "--node-count", "2", "--local-slot", "0", "--reference-slot", "0",
            "--operating-level", str(args.operating_level),
            "--probe-phase-cycles", str(args.probe_phase_cycles),
            "--duration-s", str(args.duration_s), "--poll-interval-s", str(args.poll_interval_s),
            "--out-dir", str(out_dir)]


def evaluate_child(summary: dict, exit_code: int, args) -> dict:
    setup = summary.get("ring_setup", {})
    gates = {
        "child_exit": exit_code == 0,
        "child_summary": summary.get("passed") is True,
        "identity": summary.get("board_address") == args.board_id and
                    summary.get("build") == args.expected_build,
        "logical_topology": setup.get("enabled") is True and
                            setup.get("node_count") == 2 and setup.get("local_slot") == 0 and
                            setup.get("reference_slot") == 0,
        "requested_setup": setup.get("operating_level") == args.operating_level and
                           setup.get("probe_phase_cycles") == args.probe_phase_cycles,
        "electrical_data_loopback": summary.get("electrical_data_loopback_passed") is True,
        "formal_feedback_evidence": summary.get("formal_feedback_evidence") is True,
        "samples": isinstance(summary.get("samples"), list) and len(summary["samples"]) >= 2,
        "no_child_failures": summary.get("failures") == [],
    }
    raw = validate_raw_samples(summary)
    gates.update(raw["gates"])
    return {"passed": all(gates.values()), "gates": gates,
            "failures": [name for name, passed in gates.items() if not passed],
            "raw_validation": raw}


def validate_raw_samples(summary: dict) -> dict:
    """Score every saved sample using the child parser and index definitions."""
    errors = []
    samples = summary.get("samples", [])
    gates = dict(raw_shape=True, raw_electrical=True, raw_counters=True,
                 raw_formal_feedback=True, raw_feedback_pair=True, raw_cleanup=True)
    if not isinstance(samples, list) or len(samples) < 2:
        return {"gates": {name: False for name in gates}, "errors": ["raw_samples_missing"]}
    advancing = (tdma.RING_SEQ, tdma.RING_UP_TX_SEQUENCE, tdma.RING_DOWN_RX_SEQUENCE,
                 tdma.RING_ADAPTER_TX_COUNT, tdma.RING_ADAPTER_RX_COUNT)
    running = (tdma.RING_ENABLED, tdma.RING_ADAPTER_STARTED,
               tdma.RING_UP_RUNNING, tdma.RING_DOWN_RUNNING)
    pins = dict(baud_hz=10000000, tx_csn_pin=26, tx_sck_pin=25, tx_pin=29,
                rx_csn_pin=27, rx_sck_pin=28, rx_pin=24)
    topology = {tdma.TDMA_FIELDS.index(name): value for name, value in
                (("ring_node_count", 2), ("ring_local_slot_id", 0),
                 ("ring_reference_slot_id", 0))}
    previous = None
    first = None
    for index, sample in enumerate(samples):
        try:
            parsed = tdma.tdma_status_fields(sample["raw_tdma"])
            physical = tdma.parse_named(sample["raw_phys"], tdma.PHYS_FIELDS)
            if (len(parsed) != len(tdma.TDMA_FIELDS) or parsed != sample["tdma"] or
                    physical != sample["phys"] or
                    len(next(csv.reader([sample["raw_phys"]]))) != len(tdma.PHYS_FIELDS) or
                    any(value < 0 for value in physical.values())):
                raise ValueError("raw/parsed snapshot mismatch")
            if (any(tdma.field(sample, field) != 1 for field in running) or
                    tdma.field(sample, tdma.RING_ADAPTER_LAST_ERROR) != 0 or
                    tdma.field(sample, tdma.RING_LAST_ERROR) not in (0, 2, 5) or
                    any(tdma.field(sample, field) != value for field, value in topology.items()) or
                    any(physical.get(pin) != value for pin, value in pins.items())):
                gates["raw_electrical"] = False
                errors.append(f"sample[{index}]:electrical")
            if (tdma.field(sample, tdma.SIMULTANEOUS) != 1 or
                    tdma.field(sample, tdma.RING_LAST_ERROR) != 0):
                gates["raw_formal_feedback"] = False
                errors.append(f"sample[{index}]:formal_feedback")
            # A cached evidence flag is insufficient: a later TX used to overwrite
            # an older correlated pair while leaving that flag asserted. Latest
            # TX/RX sequences need not match, so do not invent sequence provenance
            # that the public snapshot does not expose.
            raw = dict(zip(tdma.TDMA_FIELDS, parsed))
            pair_fields = ("ring_reference_tx_timestamp_ns_lo", "ring_reference_tx_timestamp_ns_hi",
                           "ring_feedback_rx_timestamp_ns_lo", "ring_feedback_rx_timestamp_ns_hi",
                           "ring_feedback_round_trip_ns", "ring_feedback_timeout_ns",
                           "ring_timestamp_resolution_ns", "ring_timestamp_flags")
            widths_valid = all(0 <= raw[name] <= 0xFFFFFFFF for name in pair_fields)
            tx = raw[pair_fields[0]] | (raw[pair_fields[1]] << 32)
            rx = raw[pair_fields[2]] | (raw[pair_fields[3]] << 32)
            rtt, timeout, resolution, flags = (raw[name] for name in pair_fields[4:])
            if not (widths_valid and tx > 0 and rx >= tx and rx - tx == rtt and
                    timeout > 0 and rtt <= timeout and 0 < resolution <= 100 and
                    flags & 2 and not flags & 1):
                gates["raw_feedback_pair"] = False
                errors.append(f"sample[{index}]:feedback_pair")
            if previous is not None:
                if (any(tdma.delta(previous, sample, field) < 0 for field in advancing) or
                        tdma.delta(previous, sample, tdma.RING_ADAPTER_RX_BAD_COUNT) != 0 or
                        any(tdma.phys_delta(previous, sample, field) != 0 for field in
                            ("rx_bad_count", "rx_ring_overrun_count"))):
                    gates["raw_counters"] = False
                    errors.append(f"sample[{index}]:counter_regression_or_error")
            first = first or sample
            previous = sample
        except (ValueError, TypeError, KeyError, IndexError, AttributeError,
                AssertionError, csv.Error) as exc:
            gates["raw_shape"] = False
            errors.append(f"sample[{index}]:{exc}")
    if first is None or previous is None or any(
            tdma.delta(first, previous, field) <= 0 for field in advancing):
        gates["raw_counters"] = False
        errors.append("counters_not_advancing")
    try:
        stopped = summary["ring_setup"]["cleanup_sample"]
        if (tdma.tdma_status_fields(stopped["raw_tdma"]) != stopped["tdma"] or
                tdma.field(stopped, tdma.RING_UP_RUNNING) != 0 or
                tdma.field(stopped, tdma.RING_DOWN_RUNNING) != 0):
            raise ValueError("cleanup did not confirm stopped raw state")
    except (ValueError, TypeError, KeyError, IndexError, AttributeError, AssertionError) as exc:
        gates["raw_cleanup"] = False
        errors.append(str(exc))
    return {"gates": gates, "errors": errors, "sample_count": len(samples)}


def run(args) -> dict:
    args.out_dir.mkdir(parents=True, exist_ok=False)
    output = dict(schema="HAOFV_TRN03_SINGLE_BOARD_DIAGNOSTIC_V1", phase="TRN-03",
                  passed=False, diagnostic_only=True, active_candidate_allowed=False,
                  production_training_certified=False, calibration_phase_matrix_applied=False,
                  parameter_flash_saved=False, physical_board_count=1, logical_slot_count=2,
                  board_id=args.board_id, expected_build=args.expected_build,
                  input_validation={}, child_started=False)
    try:
        for kind in SCHEMAS:
            output["input_validation"][kind] = preflight_input(
                getattr(args, f"{kind}_summary"), kind, args.board_id, args.expected_build)
        if not all(item["passed"] for item in output["input_validation"].values()):
            raise ValueError("input evidence rejected; no board operation performed")
        port = args.port or output["input_validation"]["marker"]["board"].get("port")
        if not port:
            raise ValueError("CDC port required")
        child_dir = args.out_dir / "tdma-loopback"
        command = child_command(args, port, child_dir)
        output.update(child_started=True, child_command=command,
                      child_summary_path=str(child_dir / "summary.json"))
        log_path = args.out_dir / "tdma-loopback.log"
        with log_path.open("w", encoding="utf-8") as log:
            # The child owns STOP/probe cleanup; do not kill it at a parent timeout.
            completed = subprocess.run(command, cwd=ROOT, stdout=log,
                                       stderr=subprocess.STDOUT, check=False)
        output["child_exit_code"] = completed.returncode
        payload = (child_dir / "summary.json").read_bytes()
        summary = json.loads(payload.decode("utf-8"))
        ensure_hardware_report(summary, "child")
        output["child_summary_sha256"] = hashlib.sha256(payload).hexdigest()
        output["child_summary"] = summary
        output["closed_loop"] = evaluate_child(summary, completed.returncode, args)
        output["passed"] = output["closed_loop"]["passed"]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        output["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        (args.out_dir / "summary.json").write_text(
            json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--expected-build", required=True)
    for kind in SCHEMAS:
        parser.add_argument(f"--{kind}-summary", type=Path, required=True)
    parser.add_argument("--port")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--duration-s", type=float, default=3.)
    parser.add_argument("--poll-interval-s", type=float, default=.25)
    parser.add_argument("--operating-level", type=int, default=7)
    parser.add_argument("--probe-phase-cycles", type=int, default=2)
    args = parser.parse_args()
    if (any(not math.isfinite(value) or value <= 0 for value in
            (args.duration_s, args.poll_interval_s)) or
            args.poll_interval_s >= args.duration_s or not 1 <= args.probe_phase_cycles <= 31):
        parser.error("invalid bounded observation parameters")
    result = run(args)
    print(f"passed={result['passed']} child_started={result['child_started']} out_dir={args.out_dir}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
