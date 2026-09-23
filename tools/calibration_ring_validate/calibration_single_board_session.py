#!/usr/bin/env python3
"""Acquire a fresh, configuration-bound single-board TRN prerequisite chain."""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from calibration_ring_validate import calibration_marker_loopback as marker
from calibration_ring_validate import calibration_link_p3 as latency
from calibration_ring_validate.calibration_phase import link_base_delay_ns, phase_delay_samples

CONFIG_FIELDS = (
    "version", "node_count", "local_slot", "reference_slot", "topology_generation",
    "topology_crc", "profile_crc", "schedule_crc", "config_seq", "applied_config_seq",
    "calibration_generation", "sample_period_ns",
)
CONFIG_QUERY = "READ:CAL:SCK:RXPHASE:CONFIG?"


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def identity(exchange, board_id, build):
    idn = exchange("*IDN?")
    actual_build = exchange("SYST:FW:BUILD?").strip('"')
    values = marker.csv_u32(exchange(CONFIG_QUERY))
    if len(values) != len(CONFIG_FIELDS):
        raise ValueError("configuration identity schema mismatch")
    config = dict(zip(CONFIG_FIELDS, values))
    if (idn.split(',')[2:3] != [board_id] or actual_build != build or
            config["version"] != 1 or config["node_count"] != 2 or
            config["local_slot"] != 0 or config["reference_slot"] != 0 or
            config["sample_period_ns"] != 4 or
            config["config_seq"] != config["applied_config_seq"] or
            any(config[key] == 0 for key in
                ("topology_generation", "topology_crc", "profile_crc", "schedule_crc"))):
        raise ValueError("board/build/staged configuration is not a settled dual-slot identity")
    return dict(idn=idn, build=actual_build, config=config)


def same_identity(expected, current):
    if current != expected:
        raise ValueError("configuration or board identity changed; predecessor evidence is stale")


def acquire_latency(exchange, args, epoch, group):
    """One joint P3 START, two distinct endpoint snapshots; no command retry."""
    def wait(states, endpoint=None, expected_epoch=None):
        deadline = time.monotonic() + args.capture_timeout
        while time.monotonic() < deadline:
            command = "READ:CAL:P3?" + (f" {endpoint}" if endpoint else "")
            status = latency.parse_p3_status(exchange(command))
            if status["state"] in states and (expected_epoch is None or status["epoch"] == expected_epoch):
                return status
            time.sleep(.02)
        raise RuntimeError("P3 state did not settle")

    def stop():
        if exchange("CAL:P3:STOP").strip('"') != "OK":
            raise RuntimeError("P3 STOP rejected")
        time.sleep(.02)
        return wait({0})

    trial = dict(epoch=epoch, signal_group=group, frequency_hz=10_000_000, passed=False)
    try:
        stop()
        response = exchange(f"CAL:P3:START 3,10000000,32,256,{epoch},{group}")
        trial["start_response"] = response
        if marker.csv_u32(response) != [3, 10_000_000, 32, 256, epoch, group]:
            raise ValueError("P3 START acknowledgement identity mismatch")
        initiator = wait({2, 3}, 1, epoch)
        responder = wait({2, 3}, 2, epoch)
        trial.update(initiator=initiator, responder=responder)
        trial.update(latency.evaluate_pair(initiator, responder, 10_000_000, args, group))
        if any(s["baud_hz"] != 10_000_000 or s["requested_words"] != 256 or
               s["produced_words"] != 256 or s["pulse_count"] != 32 or
               s["sample_period_ns"] != 4 for s in (initiator, responder)):
            raise ValueError("P3 request/readback mismatch")
        if (initiator["reject_reason"] or responder["reject_reason"] or
                initiator["edge_mask"] != 9 or responder["edge_mask"] != 6):
            raise ValueError("P3 rejected or endpoint edge identity mismatch")
    except (RuntimeError, ValueError, OSError) as exc:
        trial.update(passed=False, error=str(exc))
    finally:
        try:
            trial["cleanup"] = stop()
        except (RuntimeError, ValueError, OSError) as exc:
            trial.update(passed=False, cleanup_error=str(exc))
    return trial


def measured_baseline(trials):
    if len(trials) < 6 or any(t.get("passed") is not True for t in trials):
        raise ValueError("fresh repeated CLK/DATA and CS/DATA Latency evidence required")
    if {t["signal_group"] for t in trials} != {0, 1}:
        raise ValueError("both measured signal groups required")
    for group in (0, 1):
        if sum(t["signal_group"] == group for t in trials) < 3:
            raise ValueError("three Latency repeats per group required")
    # This candidate is tied to the bidirectional estimate. It is not pure cable delay.
    delays = [t["delay_estimate_ns"] for t in trials]
    if any(not math.isfinite(d) or d <= 0 for d in delays) or max(delays) != min(delays):
        raise ValueError("Latency baseline did not repeat exactly; retain trials for diagnosis")
    measured = statistics.median(delays)
    if int(measured) != measured:
        raise ValueError("noninteger measured delay cannot seed integer-ns phase request")
    base = link_base_delay_ns(int(measured))
    samples = phase_delay_samples(link_base_ns=base, sample_period_ns=4, node_offset_samples=0)
    return dict(measured_delay_ns=measured, baseline_divisor=2, requested_base_ns=base,
                base_samples=samples, quantized_base_ns=samples * 4,
                quantization_error_ns=samples * 4 - base,
                scope="single_fixture_bidirectional_endpoint_inclusive_estimate")


def run(args):
    from calibration_ring_validate import calibration_rxphase as rx
    from calibration_ring_validate import calibration_data_loopback as data
    args.out_dir.mkdir(parents=True, exist_ok=False)
    output = dict(schema="HAOFV_SINGLE_BOARD_TRN_SESSION_V1", passed=False,
                  trn_training_complete=False, production_training_certified=False,
                  parameter_flash_saved=False, physical_board_count=1, logical_slot_count=2,
                  stages=[], transcript=[])
    args.board_ids, args.keep_open, args.action_timeout = [args.board_id], True, args.timeout
    try:
        board = marker.discover(args).get(args.board_id)
        if board is None or board.build != args.expected_build:
            raise ValueError("target UID/build not found; no mutation performed")
        output["board"] = asdict(board)

        def exchange(command):
            raw = marker.board_command(board, command, args)
            output["transcript"].append(dict(command=command, response=raw, monotonic=time.monotonic()))
            if raw == "<timeout>":
                raise RuntimeError(f"SCPI timeout: {command}")
            return raw

        exchange("SYST:TDMA:RING:STOP")
        deadline = time.monotonic() + args.capture_timeout
        while True:
            state = marker.parse_status_named(exchange("SYST:REFMEM:SYNC:TDMA:STAT?"))
            if not state["ring_up_running"] and not state["ring_down_running"]:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("TDMA STOP not confirmed")
            time.sleep(.02)
        before = identity(exchange, args.board_id, args.expected_build)
        output["identity"] = before
        previous = digest(before)

        def stage(name, acquire):
            nonlocal previous
            same_identity(before, identity(exchange, args.board_id, args.expected_build))
            entry = dict(name=name, predecessor_sha256=previous, identity_sha256=digest(before),
                         transcript_start=len(output["transcript"]), passed=False,
                         result=dict(passed=False, trials=[]))
            output["stages"].append(entry)
            try:
                entry["result"] = acquire()
                same_identity(before, identity(exchange, args.board_id, args.expected_build))
                entry["device_error"] = exchange("SYST:ERR?")
                if not entry["device_error"].startswith("0,"):
                    raise ValueError(f"{name} device error; dependent stage not started")
                if entry["result"].get("passed") is not True:
                    raise ValueError(f"{name} failed; dependent stage not started")
                entry["passed"] = True
            finally:
                entry["transcript_end"] = len(output["transcript"])
                entry["transcript_sha256"] = digest(output["transcript"][
                    entry["transcript_start"]:entry["transcript_end"]])
                previous = digest(entry)
                entry["sha256"] = previous
                (args.out_dir / f"{len(output['stages']):02d}-{name}.json").write_text(
                    json.dumps(entry, indent=2) + "\n", encoding="utf-8")
            return entry["result"]

        epoch = args.epoch
        def latency_stage():
            nonlocal epoch
            trials = []
            for group in (0, 1):
                for _ in range(3):
                    epoch += 1
                    output["stages"][-1]["result"]["trials"] = trials
                    trials.append(acquire_latency(exchange, args, epoch, group))
                    if not trials[-1]["passed"]:
                        return dict(passed=False, trials=trials)
            try:
                baseline = measured_baseline(trials)
            except ValueError as exc:
                return dict(passed=False, trials=trials, error=str(exc))
            return dict(passed=True, trials=trials, baseline=baseline)
        base = stage("latency", latency_stage)["baseline"]

        def marker_stage():
            nonlocal epoch
            trials = []
            output["stages"][-1]["result"]["trials"] = trials
            request = argparse.Namespace(**vars(args))
            request.codebook, request.capture_words, request.fault_idle_high = 1, 2048, False
            request.full_marker = True
            for _ in range(3):
                epoch += 1
                trials.append(marker.run_trial(exchange, request, epoch))
                if not trials[-1]["passed"]:
                    break
            return dict(passed=len(trials) == 3 and all(t["passed"] for t in trials), trials=trials)
        stage("marker", marker_stage)
        # T0 receiver phase is measured on CS itself, independently of T1 SCK.
        def marker_phase_stage():
            nonlocal epoch
            request = argparse.Namespace(**vars(args))
            request.codebook, request.capture_words, request.fault_idle_low = 1, 256, False
            request.base_ns, request.direction = base["requested_base_ns"], 0
            trials = []
            output["stages"][-1]["result"]["trials"] = trials
            for offset in (-1, 0, 1):
                request.offset_samples = offset
                for _ in range(3):
                    epoch += 1
                    trials.append(rx.run_trial(exchange, request, epoch, protocol=rx.MARK_PROTOCOL))
                    if not trials[-1]["passed"]:
                        return dict(passed=False, trials=trials)
            scan = rx.evaluate_scan(trials, (0,), (-1, 0, 1), 3, protocol=rx.MARK_PROTOCOL)
            return dict(passed=scan["passed"], trials=trials, baseline=base, scan=scan,
                        signal="MARK", training_accepted=False)
        marker_scanned = stage("marker-rxphase", marker_phase_stage)
        marker_selection = rx.select_offsets(marker_scanned["trials"], marker_scanned["scan"],
                                              protocol=rx.MARK_PROTOCOL)
        output["marker_selection"] = marker_selection
        if not marker_selection["passed"]:
            raise ValueError("no repeatable MARK receiver offset; dependent SCK not started")

        def marker_residual_stage():
            nonlocal epoch
            request = argparse.Namespace(**vars(args))
            request.codebook, request.capture_words, request.fault_idle_low = 1, 256, False
            request.base_ns, request.direction = base["requested_base_ns"], 0
            request.offset_samples = marker_selection["selected_by_direction"]["0"]
            trials = []
            output["stages"][-1]["result"]["trials"] = trials
            for _ in range(3):
                epoch += 1
                trials.append(rx.run_trial(exchange, request, epoch, protocol=rx.MARK_PROTOCOL))
                if not trials[-1]["passed"]:
                    return dict(passed=False, trials=trials)
            result = rx.evaluate_residual(trials, marker_selection["selected_by_direction"], 3,
                scan_generation_identities=marker_selection["scan_generation_identities"],
                expected_configuration=marker_selection["configuration"], protocol=rx.MARK_PROTOCOL)
            return dict(passed=result["passed"], trials=trials, evaluation=result,
                        selected_by_direction=marker_selection["selected_by_direction"],
                        requested_base_ns=base["requested_base_ns"], training_accepted=False)
        stage("marker-rxphase-residual", marker_residual_stage)
        # RXPHASE provides physical loading evidence. It does not manufacture DATA windows.
        def rx_stage():
            nonlocal epoch
            request = argparse.Namespace(**vars(args))
            request.codebook, request.capture_words, request.fault_idle_low = 1, 256, False
            request.base_ns = base["requested_base_ns"]
            trials = []
            output["stages"][-1]["result"]["trials"] = trials
            for direction in (0, 1):
                request.direction = direction
                for offset in (-1, 0, 1):
                    request.offset_samples = offset
                    for _ in range(3):
                        epoch += 1
                        trials.append(rx.run_trial(exchange, request, epoch))
                        if not trials[-1]["passed"]:
                            return dict(passed=False, trials=trials)
            scan = rx.evaluate_scan(trials, (0, 1), (-1, 0, 1), 3)
            return dict(passed=scan["passed"], trials=trials, baseline=base, scan=scan,
                        destination_phase_loaded=True, training_accepted=False)
        scanned = stage("rxphase", rx_stage)
        selection = rx.select_offsets(scanned["trials"], scanned["scan"])
        output["selection"] = selection
        if not selection["passed"]:
            raise ValueError("no repeatable receiver offset; residual stage not started")

        def residual_stage():
            nonlocal epoch
            request = argparse.Namespace(**vars(args))
            request.codebook, request.capture_words, request.fault_idle_low = 1, 256, False
            request.base_ns = base["requested_base_ns"]
            trials = []
            output["stages"][-1]["result"]["trials"] = trials
            for direction in (0, 1):
                request.direction = direction
                request.offset_samples = selection["selected_by_direction"][str(direction)]
                for _ in range(3):
                    epoch += 1
                    trials.append(rx.run_trial(exchange, request, epoch))
                    if not trials[-1]["passed"]:
                        return dict(passed=False, trials=trials)
            result = rx.evaluate_residual(trials, selection["selected_by_direction"], 3,
                scan_generation_identities=selection["scan_generation_identities"],
                expected_configuration=selection["configuration"])
            return dict(passed=result["passed"], trials=trials, evaluation=result,
                        selected_by_direction=selection["selected_by_direction"],
                        requested_base_ns=base["requested_base_ns"], training_accepted=False)
        stage("rxphase-residual", residual_stage)

        def append_data_trial(trials, request, current_epoch):
            trial = data.run_trial(exchange, request, current_epoch)
            trials.append(trial)
            # Bind actual per-acquisition readback to the predecessor chain,
            # even if the configuration returns to its old value at a boundary.
            if trial.get("passed") is True and trial.get("configuration") != before["config"]:
                trial.update(passed=False, error="DATA acquisition configuration differs from predecessor")

        def data_scan_stage():
            nonlocal epoch
            request = argparse.Namespace(**vars(args))
            request.codebook, request.fault, request.gap_samples = 1, 0, 1000
            request.base_ns = base["requested_base_ns"]
            request.mark_offset = marker_selection["selected_by_direction"]["0"]
            trials = []
            output["stages"][-1]["result"]["trials"] = trials
            for offset in (-4, -2, 0, 2, 4):
                request.data_offset = offset
                for _ in range(3):
                    epoch += 1
                    append_data_trial(trials, request, epoch)
                    if not trials[-1]["passed"]:
                        return dict(passed=False, trials=trials)
            scan = data.evaluate_scan(trials, (-4, -2, 0, 2, 4), 3)
            return dict(passed=scan["passed"], trials=trials, scan=scan, baseline=base,
                        mark_offset=request.mark_offset,
                        sck_predecessor_offsets=selection["selected_by_direction"],
                        training_accepted=False)
        data_scanned = stage("data-window", data_scan_stage)
        # This experiment may load a bounded diagnostic window candidate.
        # Strict fixed-offset/zero-residual results remain separate evidence;
        # neither policy grants a production calibration matrix.
        data_selection = data.select_offset(data_scanned["trials"], data_scanned["scan"],
                                            diagnostic_window=True)
        output["data_selection"] = data_selection
        if not data_selection["passed"]:
            raise ValueError("no repeatable DATA receiver offset; residual stage not started")

        def data_residual_stage():
            nonlocal epoch
            request = argparse.Namespace(**vars(args))
            request.codebook, request.fault, request.gap_samples = 1, 0, 1000
            request.base_ns = base["requested_base_ns"]
            request.mark_offset = marker_selection["selected_by_direction"]["0"]
            request.data_offset = data_selection["selected_offset"]
            trials = []
            output["stages"][-1]["result"]["trials"] = trials
            for _ in range(3):
                epoch += 1
                append_data_trial(trials, request, epoch)
                if not trials[-1]["passed"]:
                    return dict(passed=False, trials=trials)
            result = data.evaluate_residual(trials, request.data_offset, 3,
                scan_generation_identities=data_selection["scan_generation_identities"],
                expected_configuration=data_selection["configuration"],
                diagnostic_window=True, residual_policy=data_selection["residual_policy"],
                scan_trials=data_scanned["trials"], scan=data_scanned["scan"])
            return dict(passed=result["passed"], trials=trials, evaluation=result,
                        selected_offset=request.data_offset, mark_offset=request.mark_offset,
                        sck_predecessor_offsets=selection["selected_by_direction"],
                        residual_policy=data_selection["residual_policy"],
                        training_accepted=False)
        stage("data-window-residual", data_residual_stage)
        output["prerequisites_acquired"] = True
        output["remaining"] = ["accepted single-board MARK/residence/SCK/DATA matrix",
                               "TRN-03 applied matrix and formal feedback"]
        output["final_idn"] = exchange("*IDN?")
        output["final_build"] = exchange("SYST:FW:BUILD?").strip('"')
        output["final_error"] = exchange("SYST:ERR?")
        output["passed"] = (output["final_idn"] == board.idn and
                            output["final_build"] == args.expected_build and
                            output["final_error"].startswith("0,"))
    except (OSError, ValueError, RuntimeError, KeyError) as exc:
        output["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        marker.close_persistent_connections()
        (args.out_dir / "summary.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--epoch", default=10, type=int)
    parser.add_argument("--baud", default=115200, type=int)
    parser.add_argument("--timeout", default=3., type=float)
    parser.add_argument("--capture-timeout", default=5., type=float)
    parser.add_argument("--settle", default=.05, type=float)
    args = parser.parse_args()
    # Every request uses a new uint8 epoch: 6+3+9+3+18+6+15+3 captures.
    if not 1 <= args.epoch <= 255 - 63 or any(not math.isfinite(v) or v <= 0 for v in
                                       (args.timeout, args.capture_timeout, args.settle)):
        parser.error("invalid epoch or timeout")
    args.frequency_tolerance_percent, args.duty_tolerance_percent = 5., 10.
    result = run(args)
    print(f"prerequisites_passed={result['passed']} training_complete={result['trn_training_complete']} out_dir={args.out_dir}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
