#!/usr/bin/env python3
"""Validate position-driven SP8T with simulated or physical input edges.

RJ45 TDMA traffic and OUT4-to-IN2 READY remain physical. IN1 edges are
software-injected by default; select an unused --counter-input when IN1 is live.
--external-input uses only the physical IN1 source.
Neither profile certifies an independently measured edge count.
"""
from __future__ import annotations

import argparse
import csv
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance.sequence_feedback_validate import (
    Bench, discover, open_serial_port, open_visa_resource, parse_role, read_io, require, select_transport,
)
from tools.hardware_acceptance.sequence_tdma_cycle_validate import (
    LINK_FIELDS, IDENTITY_FIELDS, VisaPort, gui_batch, ring_snapshot,
)
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError
from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
from tools.tdma_ring_monitor import tdma_single_board_loopback as ring

COUNTER_FIELDS = ("enabled", "slot", "input", "threshold", "events", "positions", "partial",
                  "fault_events", "history_total", "history_retained", "phase", "error")
HISTORY_FIELDS = ("ordinal", "run", "generation", "binding_epoch", "exchange_id",
                  "position", "sequence_index", "sequence_state", "output_code",
                  "threshold_pulses", "observed_pulses", "trigger_ordinal", "ready_ordinal",
                  "position_admitted_tick_ms", "sample_done_tick_ms", "cycle_elapsed_ms",
                  "outcome_flags")
TIMING_FIELDS = ("version", "clock_hz", "ordinal", "run", "generation", "binding_epoch",
                 "exchange_id", "position", "sequence_index", "flags", "request_ticks",
                 "applied_ticks", "offered_ticks", "returned_ticks", "fire_queued_ticks", "done_ticks")
HISTORY_STATUS_FIELDS = ("version", "clock_hz", "capacity", "run", "generation",
                         "binding_epoch", "threshold", "total", "retained", "overwritten")
# Snapshot of sync_io_sequence_fault_t::SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY.
BACKEND_COUNTER_BUSY = 14
# Snapshot of trigger_sequence_link.h::TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY.
HISTORY_CAPACITY = 256


def parse_uints(response, fields):
    parts = response.split(",")
    require(len(parts) == len(fields) and all(p.isascii() and p.isdecimal() for p in parts),
            "malformed position snapshot")
    values = list(map(int, parts))
    require(all(v <= 0xffffffff for v in values), "position field overflow")
    return dict(zip(fields, values))


def sample(bench):
    # COUNTER is read after LINK; subsequent cumulative counts may be larger.
    return {"link": parse_uints(bench.command("READ:SEQ:LINK?"), LINK_FIELDS),
            "counter": parse_uints(bench.command("READ:SEQ:COUNTER?"), COUNTER_FIELDS)}


def configure(bench, report, manual_ready=False, repeat=2, start=True):
    args = bench.args
    report["gui_control"] = {"path": "GUI POSITION builders and batch executor",
        "mode": gui.MODE_TURNTABLE,
        "module_sha256": hashlib.sha256(Path(gui.__file__).read_bytes()).hexdigest()}
    commands = gui.build_mode_configuration(gui.MODE_TURNTABLE, "SP8T", list(range(8)),
        "MANUAL", "RIS", args.settle_us, args.gateway_pulse_us, 7, 0, "NONE",
        "MANUAL" if manual_ready else "IN2", args.gateway_timeout_ms, repeat,
        counter_slot=1, dut_slot=2, vna_slot=3,
        counter_input=getattr(args, "counter_input", "IN1"), counter_threshold=args.threshold,
        operating_level=args.operating_level)
    gui_batch(bench, report, "configure", commands)
    profile = report["operating_profile"] = {"raw": bench.command("SYST:TDMA:OPMODE?")}
    profile["fields"] = fields = parse_uints(profile["raw"], (
        "level", "baud_hz", "cycle_ns", "train_cycles", "flags", "crc",
        "staged_level", "staged_baud_hz", "staged_cycle_ns", "staged_train_cycles",
        "staged_flags", "staged_crc", "stage_count", "apply_count", "reject_count", "last_result"))
    require(fields["level"] == fields["staged_level"] == args.operating_level and
            fields["baud_hz"] > 0 and fields["cycle_ns"] > 0 and fields["last_result"] == 0,
            "TDMA operating profile readback mismatch")
    if getattr(args, "angle_scan", False):
        # Inclusive points with one degree between them. N remains the
        # externally supplied threshold; speed derives from declared source Hz.
        speed = args.source_hz / args.threshold
        gui_batch(bench, report, "configure_angle", [
            f"CONF:ANGLE:SWEEP 0,{repeat - 1},1,{speed:.12g}",
            f"CONF:ANGLE:INPUT IN1,{args.threshold}",
            "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?"])
        report["angle_config"] = verify_angle_configuration(bench, speed)
    report["configured"] = sample(bench)
    row, counter = report["configured"]["link"], report["configured"]["counter"]
    require([counter[k] for k in ("enabled", "slot", "input", "threshold")] ==
            [1, 1, int(getattr(args, "counter_input", "IN1")[-1]), args.threshold],
            "counter configuration readback mismatch")
    require([row[k] for k in ("enabled", "phase", "error", "dutslot", "vnaslot", "input", "outputmask")] ==
            [1, 1, 0, 2, 3, 0 if manual_ready else 2, 8], "LINK configuration mismatch")
    # LINK.repeat is a run snapshot and is zero before START. The first
    # REPEAT? field is the stopped configuration; the remaining fields
    # describe the previous run and must not stand in for that configuration.
    report["repeat_configuration"] = parse_uints(bench.command("READ:SEQ:REPEAT?"),
        ("configured", "run", "finished"))
    require(report["repeat_configuration"]["configured"] == repeat, "configured position count mismatch")
    report["flight_mode"] = bench.command("SYST:TDMA:FLIGHT:MODE?")
    require(report["flight_mode"] == "2", "physical process-image forwarding not active")
    report["roles"] = {str(instance): bench.command(f"READ:SEQ:NODE:ROLE? {instance}")
                       for instance in (2, 5, 7)}
    for instance, name in ((2, "COUNTER"), (5, "DUT"), (7, "VNA")):
        role = parse_role(report["roles"][str(instance)], instance, name)
        require(role["active_enabled"] == 1, f"{name} role not active")
    report["plan"] = bench.command("READ:SEQ? SP8T")
    require(next(csv.reader([report["plan"]]))[-8:] == [str(i) for i in range(8)], "SP8T plan mismatch")
    report["codes"] = {}
    for code in range(8):
        report["codes"][str(code)] = bench.command(f"READ:SEQ:CODE? {code}")
        require(report["codes"][str(code)] == f"{code},{code}", "SP8T code readback mismatch")
    if start:
        gui_batch(bench, report, "start", gui.build_start_commands(gui.MODE_TURNTABLE))


def verify_angle_configuration(bench, speed):
    positions = getattr(bench.args, "positions", 2)
    replies = {name: next(csv.reader([bench.command(f"READ:ANGLE:{name}?")]))
               for name in ("SWEEP", "INPUT", "SPEED", "POSITION")}
    sweep, source = replies["SWEEP"], replies["INPUT"]
    require(len(sweep) == 6 and list(map(float, sweep[:3])) == [0, positions - 1, 1] and
            math.isclose(float(sweep[3]), speed, rel_tol=1e-8) and sweep[4:] == [str(positions), "1"],
            "angle sweep/speed not bound to requested positions")
    require(len(source) == 7 and source[0] == "IN1" and
            float(source[1]) == bench.args.threshold and
            math.isclose(float(source[2]), bench.args.source_hz, rel_tol=1e-8) and
            int(source[3]) == bench.args.threshold and source[6] == "1" and
            math.isclose(float(source[4]), 1 / speed, rel_tol=1e-8) and
            math.isclose(float(source[5]), speed, rel_tol=1e-8), "angle input conversion mismatch")
    require(len(replies["SPEED"]) == 1 and
            math.isclose(float(replies["SPEED"][0]), speed, rel_tol=1e-8), "angle speed mismatch")
    position = replies["POSITION"]
    require(len(position) == 11 and position[0] == "POSITION" and
            position[1:3] == ["0", str(positions)] and position[7:] == ["0", "1", "0", "1"],
            "angle position must be invalid before the first input threshold")
    return replies


def verify_angle_terminal(bench):
    positions = getattr(bench.args, "positions", 2)
    row = next(csv.reader([bench.command("READ:ANGLE:POSITION?")]))
    require(len(row) == 11 and row[0:3] == ["POSITION", str(positions), str(positions)] and
            float(row[3]) == positions - 1 and row[7:] == ["1", "0", "0", "1"],
            "final angle cursor or finite sweep completion mismatch")
    require(int(row[5]) >= positions * bench.args.threshold, "angle raw pulse count missing")
    return row


def inject_counter(bench, report, count, purpose):
    command = f"TRIG:SEQ:INJECT {getattr(bench.args, 'counter_input', 'IN1')},{count}"
    started = time.monotonic()
    response = bench.command(command)
    require(response == "1", f"SCPI counter injection rejected: {purpose}")
    report.setdefault("injections", []).append({
        "command": command, "count": count, "purpose": purpose,
        "issued_at": started, "response": response})


def inject_ready(bench, report, count, purpose):
    command = f"TRIG:SEQ:INJECT READY,{count}"
    started = time.monotonic()
    response = bench.command(command)
    require(response == "1", f"SCPI READY injection rejected: {purpose}")
    report["ready_injection"] = {
        "command": command, "count": count, "purpose": purpose,
        "issued_at": started, "response": response}


def check_history(records, final, threshold, positions=2, target_ms=200):
    require(len(records) == positions * 8, "expected every position state record")
    summaries = []
    for ordinal, record in enumerate(records, 1):
        position, index = (ordinal - 1) // 8 + 1, (ordinal - 1) % 8
        require([record[k] for k in ("ordinal", "run", "generation", "binding_epoch", "position",
                                     "sequence_index", "sequence_state", "output_code")] ==
                [ordinal, final["run"], final["generation"], final["binding_epoch"], position,
                 index, index, index], "history identity, state or output mismatch")
        require(record["exchange_id"] > 0 and
                (ordinal == 1 or record["exchange_id"] ==
                 records[ordinal - 2]["exchange_id"] + 1),
                "history exchange sequence is not contiguous")
        require(record["threshold_pulses"] == position * threshold and
                position * threshold <= record["observed_pulses"] < (position + 1) * threshold,
                "history pulse threshold mismatch")
        require(record["trigger_ordinal"] == record["ready_ordinal"] == ordinal,
                "history VNA trigger/READY ordinal mismatch")
        # Absolute timestamps and the full-resolution TIMER1 delta are each
        # rounded down to ms; subtracting the timestamps can be one ms larger.
        rounded_delta = (record["sample_done_tick_ms"] - record["position_admitted_tick_ms"]) & 0xffffffff
        require(rounded_delta - record["cycle_elapsed_ms"] in (0, 1),
                "history timing is incoherent")
        require(0 < record["cycle_elapsed_ms"] <= target_ms,
                "position sequence exceeded the configured cycle target")
        require(record["outcome_flags"] == 7, "requested state was not applied and sampled")
    require(all(a["observed_pulses"] <= b["observed_pulses"] for a, b in zip(records, records[1:])),
            "history observed pulse counts regressed")
    for position in range(1, positions + 1):
        group = records[(position - 1) * 8:position * 8]
        require(len({row["position_admitted_tick_ms"] for row in group}) == 1,
                "position records do not share one admitted pulse boundary")
        require(all(a["cycle_elapsed_ms"] <= b["cycle_elapsed_ms"] for a, b in zip(group, group[1:])),
                "position completion timing regressed")
        summaries.append({"position": position, "target_ms": target_ms,
                          "elapsed_ms": group[-1]["cycle_elapsed_ms"],
                          "threshold_pulse_ordinal": position * threshold})
    return summaries


def verify_terminal(bench, report, final, manual_ready):
    """LINK DONE is a submitted FINISH, not the Core1 completion receipt."""
    report["terminal_owner_samples"] = samples = []
    deadline = time.monotonic() + bench.args.timeout
    while True:
        status = bench.status()
        samples.append(status)
        require((status["run_id"], status["generation"]) == (final["run"], final["generation"]),
                "terminal owner identity mismatch")
        require(status["count"] == 8, "terminal plan size mismatch")
        if manual_ready:
            require(status["accepted"] == status["completed"] == 0, "busy fault advanced the sequence")
            if status["state"] in ("IDLE", "FAULT"):
                driver_fault = status["backend_fault"] == BACKEND_COUNTER_BUSY
                link_stop = (status["state"] == "IDLE" and status["backend_fault"] == 0 and
                             status["error"] == "NONE" and status["faults"] == 0)
                require(driver_fault or link_stop, "unexpected owner fault during busy-boundary test")
                if driver_fault:
                    require(status["error"] == "BACKEND_FAULT" and status["faults"] > 0,
                            "COUNTER_BUSY missing owner fault attribution")
                report["busy_fault_detection_layer"] = "Core1 counter driver" if driver_fault else "Core1 link coordinator"
                break
        else:
            require(status["error"] == "NONE" and status["faults"] == status["backend_fault"] == 0,
                    "owner fault after LINK DONE")
            positions = getattr(bench.args, "positions", 2)
            require(status["accepted"] == status["completed"] == positions * 8 - 1, "terminal step accounting mismatch")
            if status["state"] == "IDLE":
                report["terminal_repeat"] = parse_uints(bench.command("READ:SEQ:REPEAT?"),
                    ("configured", "run", "finished"))
                require(report["terminal_repeat"] == dict(configured=positions, run=positions, finished=1),
                        "Core1 did not finish requested positions")
                break
        require(time.monotonic() < deadline, "terminal owner acknowledgement timed out")
        time.sleep(bench.args.poll)


def run_profile(bench, port, report, manual_ready=False):
    args = bench.args
    report.update(passed=False, samples=[], no_premature_sample_observed=False)
    configure(bench, report, manual_ready)
    report["input_simulation"] = {
        "method": "SCPI", "counter_command": f"TRIG:SEQ:INJECT {getattr(args, 'counter_input', 'IN1')},<count>",
        "ready_command": "TRIG:SEQ:INJECT READY,<count>" if manual_ready else None,
        "physical_edge_count_verified": False, "physical_ready_verified": not manual_ready}
    report["ring_before"] = ring_snapshot(port, args, report, "before")
    if getattr(args, "schedule_evidence", False):
        capture_schedule(bench, report, "before")
    deadline = time.monotonic() + args.duration
    previous = None
    observed_wait_ready = False
    injected = 0
    busy_batch_issued = False
    while time.monotonic() < deadline:
        entry = sample(bench)
        report["samples"].append(entry)
        row, counter = entry["link"], entry["counter"]
        require(counter["enabled"] == 1 and counter["threshold"] == args.threshold,
                "counter binding changed")
        if row["phase"] == 1 and previous is None:
            time.sleep(args.poll)
            continue
        require(row["run"] != 0 and row["generation"] != 0, "missing run identity")
        require(row["repeat"] == 2, "run position count mismatch")
        if previous:
            require(all(row[k] == previous["link"][k] for k in IDENTITY_FIELDS), "run identity changed")
            require(counter["events"] >= previous["counter"]["events"], "pulse counter regressed")
        require(counter["events"] <= injected,
                "position counter contains events outside SCPI injection evidence")
        if counter["events"] < args.threshold:
            require(row["triggers"] == row["ready"] == row["completed"] == 0,
                    "measurement started before the first position")
            report["no_premature_sample_observed"] = True
        if injected == 0 and row["phase"] == 9 and counter["events"] == 0:
            inject_counter(bench, report, args.threshold - 1, "pre-threshold proof")
            injected = args.threshold - 1
        elif injected == args.threshold - 1 and counter["events"] == injected:
            require(row["triggers"] == row["ready"] == row["completed"] == 0,
                    "measurement started before simulated threshold")
            inject_counter(bench, report, 1, "first position boundary")
            injected += 1
        if manual_ready and row["phase"] == 3 and row["triggers"] == 1:
            observed_wait_ready = True
            if not busy_batch_issued:
                inject_counter(bench, report, args.threshold, "busy-boundary fault")
                injected += args.threshold
                busy_batch_issued = True
        if manual_ready and row["phase"] == 7:
            require(row["error"] == counter["error"] == 5 and counter["fault_events"] >= 2 * args.threshold,
                    "expected COUNTER_BUSY at the next position boundary")
            require(observed_wait_ready and row["triggers"] == 1 and row["ready"] == row["completed"] == 0,
                    "busy-position test did not hold exactly the first measurement")
            report["expected_busy_fault"] = entry
            break
        if manual_ready and counter["phase"] == 7 and counter["error"] == 5:
            # Separate SCPI reads can straddle the fault publication; keep the
            # evidence and obtain a coherent terminal pair on the next poll.
            previous = entry
            time.sleep(args.poll)
            continue
        require(row["error"] == counter["error"] == 0 and row["phase"] in (2, 3, 4, 5, 8, 9, 10, 11),
                "unexpected position runtime fault")
        require(row["triggers"] <= counter["positions"] * 8, "more measurements than admitted positions")
        if (not manual_ready and row["phase"] == 9 and counter["positions"] == 1 and
                injected == args.threshold):
            inject_counter(bench, report, args.threshold, "second position boundary")
            injected += args.threshold
        if not manual_ready and row["phase"] == 8:
            require(row["triggers"] == row["ready"] == 16 and row["completed"] == 15 and
                    counter["positions"] == 2 and counter["history_total"] == counter["history_retained"] == 16,
                    "two-position full-plan accounting mismatch")
            require("ready_injection" not in report,
                    "physical READY profile unexpectedly injected READY credits")
            report["history"] = records = []
            for ordinal in range(1, 17):
                record = parse_uints(bench.command(f"READ:SEQ:HIST? {ordinal}"), HISTORY_FIELDS)
                records.append(record)
            report["position_cycles"] = check_history(
                records, row, args.threshold, target_ms=args.position_cycle_target_ms)
            break
        previous = entry
        time.sleep(args.poll)
    else:
        raise RuntimeError("position profile timed out")
    require(report["no_premature_sample_observed"], "no pre-threshold observation; increase N")
    verify_terminal(bench, report, row, manual_ready)
    report["ring_after"] = ring_snapshot(port, args, report, "after")
    if getattr(args, "schedule_evidence", False):
        capture_schedule(bench, report, "after")
    for index in (ring.RING_ADAPTER_TX_COUNT, ring.RING_ADAPTER_RX_COUNT):
        require(ring.delta(report["ring_before"], report["ring_after"], index) > 0,
                "no physical TDMA counter growth")
    report["passed"] = True


def read_timing(bench, record):
    parts = bench.command(f"READ:SEQ:HIST:TIM? {record['ordinal']}").split(",")
    require(len(parts) == len(TIMING_FIELDS) and
            all(p.isascii() and p.isdecimal() for p in parts), "malformed timing snapshot")
    values = list(map(int, parts))
    require(all(v <= 0xffffffff for v in values[:10]) and
            all(v <= 0xffffffffffffffff for v in values[10:]), "timing field overflow")
    timing = dict(zip(TIMING_FIELDS, values))
    require(timing["version"] == 1 and timing["clock_hz"] > 0 and timing["flags"] == 63,
            "timing incomplete or unsupported")
    require(all(timing[k] == record[k] for k in
                ("ordinal", "run", "generation", "binding_epoch", "exchange_id", "position", "sequence_index")),
            "timing history identity mismatch")
    require(values[10:] == sorted(values[10:]), "timing boundaries regressed")
    require(timing["done_ticks"] * 1000 // timing["clock_hz"] == record["cycle_elapsed_ms"],
            "timing does not match completed history")
    return timing


def collect_completed_history(bench, records, row, counter):
    """Drain completed records before the board's bounded ring overwrites them."""
    total, retained = counter["history_total"], counter["history_retained"]
    require(total - retained <= len(records), "position history overwritten before collection")
    for ordinal in range(len(records) + 1, min(total, row["ready"]) + 1):
        record = parse_uints(bench.command(f"READ:SEQ:HIST? {ordinal}"), HISTORY_FIELDS)
        require(record["ordinal"] == ordinal and record["run"] == row["run"] and
                record["generation"] == row["generation"] and
                record["binding_epoch"] == row["binding_epoch"], "history collection identity mismatch")
        if record["outcome_flags"] != 7:
            break  # READY snapshot may precede publication of SAMPLE_DONE.
        if getattr(getattr(bench, "args", None), "timing_evidence", False):
            record["timing"] = read_timing(bench, record)
        records.append(record)


def run_external_profile(bench, port, report):
    """Finite positions from the real source; never inject COUNTER or READY."""
    if getattr(bench.args, "quiet_capture", False):
        return run_quiet_profile(bench, port, report)
    args = bench.args
    positions = getattr(args, "positions", 2)
    measurements = positions * 8
    report.update(passed=False, samples=[], no_premature_sample_observed=False,
                  input_simulation={"method": "physical IN1", "physical_ready_verified": True,
                                    "physical_edge_count_verified": False})
    records = report["history"] = []
    configure(bench, report, repeat=positions)
    report["ring_before"] = ring_snapshot(port, args, report, "before")
    deadline = time.monotonic() + args.duration
    identity = None
    previous_events = 0
    reported_positions = 0
    while time.monotonic() < deadline:
        entry = sample(bench)
        report["samples"].append(entry)
        if getattr(args, "diagnostic_stress", False):
            capture_diagnostic_stress(bench, port, report)
        row, counter = entry["link"], entry["counter"]
        require(row["error"] == counter["error"] == counter["fault_events"] == 0,
                "physical position run fault")
        if row["phase"] == 1:
            time.sleep(args.poll)
            continue
        current_identity = tuple(row[key] for key in IDENTITY_FIELDS)
        if identity is None:
            identity = current_identity
        require(current_identity == identity, "physical position run identity changed")
        require(counter["enabled"] == 1 and counter["threshold"] == args.threshold and
                counter["events"] >= previous_events, "counter configuration changed or count regressed")
        previous_events = counter["events"]
        if counter["events"] < args.threshold:
            require(row["triggers"] == row["ready"] == row["completed"] == counter["positions"] == 0,
                    "sample before first physical threshold")
            report["no_premature_sample_observed"] = True
        require(row["triggers"] <= measurements and row["ready"] <= row["triggers"] and
                row["completed"] < measurements and counter["positions"] <= positions,
                "physical position quota exceeded")
        collect_completed_history(bench, records, row, counter)
        completed_positions = len(records) // 8
        if completed_positions >= reported_positions + 20 or completed_positions == positions:
            print(json.dumps({"completed_positions": completed_positions,
                "target_positions": positions, "history_records": len(records),
                "input_pulses": counter["events"], "link_error": row["error"]}), flush=True)
            reported_positions = completed_positions
        if row["phase"] == 8:
            require(row["triggers"] == row["ready"] == measurements and row["completed"] == measurements - 1 and
                    counter["positions"] == positions, "physical positions not fully sampled")
            require(counter["history_total"] == measurements and
                    counter["history_retained"] == min(measurements, HISTORY_CAPACITY),
                    "unexpected physical position history count")
            verify_terminal(bench, report, row, False)
            if getattr(args, "angle_scan", False):
                report["angle_terminal"] = verify_angle_terminal(bench)
            report["position_cycles"] = check_history(records, row, args.threshold,
                positions=positions, target_ms=args.position_cycle_target_ms)
            if getattr(args, "timing_evidence", False):
                report["backend_timing"] = next(csv.reader([bench.command("READ:SEQ:TIM?")]))
                require(report["backend_timing"] == ["PIO0", "4", "0"], "expected 4 ns PIO0 backend")
            report["ring_after"] = ring_snapshot(port, args, report, "after")
            for index in (ring.RING_ADAPTER_TX_COUNT, ring.RING_ADAPTER_RX_COUNT):
                require(ring.delta(report["ring_before"], report["ring_after"], index) > 0,
                        "no physical TDMA counter growth")
            require(report["no_premature_sample_observed"], "first threshold was not observed; increase N")
            report["passed"] = True
            return
        time.sleep(args.poll)
    raise AcceptanceError("physical input did not complete requested positions before timeout")


def quiet_wait_seconds(args):
    return args.positions * args.threshold / args.source_hz + args.position_cycle_target_ms / 1000 + 1


def read_history_status(bench, report, point):
    record = report[point] = {"raw": bench.command("READ:SEQ:HIST:STAT?")}
    record.update(parse_uints(record["raw"], HISTORY_STATUS_FIELDS))
    require(record["version"] == 1 and record["clock_hz"] > 0 and record["capacity"] > 0,
            "history vector capability unavailable or unsupported")
    require(record["retained"] == min(record["total"], record["capacity"]) and
            record["overwritten"] == record["total"] - record["retained"],
            "incoherent history vector accounting")
    return record


def run_quiet_profile(bench, port, report):
    args = bench.args
    measurements = args.positions * 8
    wait_s = quiet_wait_seconds(args)
    require(args.duration >= wait_s, "duration shorter than quiet capture window")
    require(isinstance(port, ring.EvidencePort), "quiet capture requires raw transport evidence")
    report.update(passed=False, probe_mode="post_run_vector", samples=[], history=[],
        no_premature_sample_observed=False,
        input_simulation={"method": "physical IN1", "physical_ready_verified": True,
                          "physical_edge_count_verified": False},
        observation_limits={"live_prethreshold_observation": False,
                            "physical_edge_timing_verified": False,
                            "host_silence_scope": "this tool's EvidencePort; excludes other clients",
                            "scope": "post-run retained history and natural terminal state"})
    configure(bench, report, repeat=args.positions, start=False)
    start_commands = gui.build_start_commands(gui.MODE_TURNTABLE)
    require(start_commands[-1] == "TRIG:START" and start_commands.count("TRIG:START") == 1,
            "unexpected GUI start command sequence")
    gui_batch(bench, report, "prepare_ring", start_commands[:-1])
    report["ring_before"] = ring_snapshot(port, args, report, "before")
    before = read_history_status(bench, report, "history_vector_before")
    require(measurements <= before["capacity"], "quiet capture exceeds firmware history capacity")
    report["error_before"] = bench.command("SYST:ERR?")
    require(report["error_before"].lstrip().startswith("0,"), "SCPI error before quiet START")
    silence = report["quiet_capture"] = {"planned_wait_s": wait_s,
        "source_hz_is_declared": True, "command_ordinal_base": 1,
        "start_command_ordinal": len(port.transcript) + 1}
    silence["start_response"] = bench.command("TRIG:START")
    require(silence["start_response"] == "1", "quiet START rejected")
    silence.update(start_response_at=datetime.now(timezone.utc).isoformat(),
        start_response_monotonic=time.monotonic(),
        transcript_count_after_start=len(port.transcript))
    deadline = silence["start_response_monotonic"] + wait_s
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        time.sleep(min(1.0, remaining))
    silence.update(first_query_at=datetime.now(timezone.utc).isoformat(),
        elapsed_s=time.monotonic() - silence["start_response_monotonic"],
        transcript_count_before_first_query=len(port.transcript),
        first_query_ordinal=len(port.transcript) + 1)
    silence["commands_during_wait"] = (silence["transcript_count_before_first_query"] -
                                        silence["transcript_count_after_start"])
    require(silence["commands_during_wait"] == 0, "device command issued during quiet capture")
    entry = sample(bench)
    report["samples"].append(entry)
    row, counter = entry["link"], entry["counter"]
    require(row["phase"] == counter["phase"] == 8 and
            row["error"] == counter["error"] == counter["fault_events"] == 0,
            "quiet capture did not naturally finish without error")
    require(row["run"] > 0 and row["generation"] > 0 and row["repeat"] == args.positions and
            counter["enabled"] == 1 and counter["threshold"] == args.threshold,
            "quiet terminal configuration or identity mismatch")
    require(row["triggers"] == row["ready"] == measurements and row["completed"] == measurements - 1 and
            counter["positions"] == args.positions and
            counter["history_total"] == counter["history_retained"] == measurements,
            "quiet terminal position or history count mismatch")
    owner = bench.status()
    report["terminal_owner_samples"] = [owner]
    require(owner["state"] == "IDLE" and owner["error"] == "NONE" and
            owner["faults"] == owner["backend_fault"] == 0 and owner["count"] == 8 and
            (owner["run_id"], owner["generation"]) == (row["run"], row["generation"]) and
            owner["accepted"] == owner["completed"] == measurements - 1,
            "quiet owner did not naturally finish in IDLE")
    report["terminal_repeat"] = parse_uints(bench.command("READ:SEQ:REPEAT?"),
                                            ("configured", "run", "finished"))
    require(report["terminal_repeat"] == dict(configured=args.positions, run=args.positions, finished=1),
            "quiet finite repeat did not finish")
    if args.angle_scan:
        report["angle_terminal"] = verify_angle_terminal(bench)
    vector = read_history_status(bench, report, "history_vector_after")
    require(vector["capacity"] == before["capacity"] and vector["clock_hz"] == before["clock_hz"] and
            vector["run"] != before["run"] and
            all(vector[k] == row[k] for k in ("run", "generation", "binding_epoch")) and
            vector["threshold"] == args.threshold and vector["total"] == vector["retained"] == measurements and
            vector["overwritten"] == 0, "quiet history vector identity changed or history lost")
    collect_completed_history(bench, report["history"], row, counter)
    after_drain = read_history_status(bench, report, "history_vector_after_drain")
    require(all(after_drain[key] == vector[key] for key in HISTORY_STATUS_FIELDS),
            "quiet history vector changed during post-run readback")
    require(all(record["timing"]["clock_hz"] == vector["clock_hz"] for record in report["history"]),
            "quiet timing vector clock mismatch")
    report["position_cycles"] = check_history(report["history"], row, args.threshold,
        positions=args.positions, target_ms=args.position_cycle_target_ms)
    report["history_thresholds_verified"] = True
    report["backend_timing"] = next(csv.reader([bench.command("READ:SEQ:TIM?")]))
    require(report["backend_timing"] == ["PIO0", "4", "0"], "expected 4 ns PIO0 backend")
    report["ring_after"] = ring_snapshot(port, args, report, "after")
    for index in (ring.RING_ADAPTER_TX_COUNT, ring.RING_ADAPTER_RX_COUNT):
        require(ring.delta(report["ring_before"], report["ring_after"], index) > 0,
                "no physical TDMA counter growth")
    report["error_after"] = bench.command("SYST:ERR?")
    require(report["error_after"].lstrip().startswith("0,"), "SCPI error after quiet capture")
    report["passed"] = True


def capture_diagnostic_stress(bench, port, report):
    record = {"poll_ordinal": len(report["samples"])}
    report.setdefault("diagnostic_stress", []).append(record)
    try:
        record["error_before"] = bench.command("SYST:ERR?")
        require(record["error_before"].lstrip().startswith("0,"),
                "preexisting error before diagnostic stress snapshot")
        record["snapshot"] = ring.sample(port, bench.args.timeout)
        record["error_after"] = bench.command("SYST:ERR?")
        require(record["error_after"].lstrip().startswith("0,"),
                "diagnostic stress snapshot command failed")
    except (Exception, KeyboardInterrupt) as exc:
        record["failure"] = f"{type(exc).__name__}: {exc}"
        raise


def capture_schedule(bench, report, point):
    """Retain actual scheduler observations without turning them into WCET proof."""
    record = report.setdefault("scheduler", {})[point] = {}
    record["error_before"] = bench.command("SYST:ERR?")
    require(record["error_before"].startswith("0,"), "preexisting error before scheduler snapshot")
    record["raw"] = raw = bench.command("SYST:TDMA:SCHED?")
    record["error_after"] = bench.command("SYST:ERR?")
    require(record["error_after"].startswith("0,"), "scheduler snapshot command failed")
    fields = raw.split(",")
    require(len(fields) >= 8 and all(s.isascii() and s.isdecimal() for s in fields),
            "malformed scheduler snapshot")
    values = list(map(int, fields))
    require(all(v <= 0xffffffff for v in values), "scheduler field overflow")
    version, hz, cycles, phases, enabled, quarantined, count, misses = values[:8]
    require(hz > 0 and cycles > 0 and 0 < phases <= 32 and len(values) == 8 + 11 * phases,
            "scheduler schema/clock mismatch")
    record.update(version=version, clock_hz=hz, cycle_cycles=cycles,
                  enabled=enabled, quarantined=quarantined, cycle_count=count,
                  schedule_misses=misses)
    names = ("start", "end", "budget", "last_start", "last_runtime", "max_runtime",
             "runs", "skips", "start_misses", "overruns", "deadline_misses")
    record["phases"] = [dict(zip(names, values[8 + i * 11:8 + (i + 1) * 11]))
                        for i in range(phases)]
    record["strict_realtime_verified"] = False


def lifecycle_observation(bench, report, name, predicate, *, restarting=False):
    records = report.setdefault(name, [])
    deadline = time.monotonic() + bench.args.duration
    restart_identity = None
    while True:
        entry = sample(bench)
        entry["owner"] = owner = bench.status()
        records.append(entry)
        require(entry["link"]["error"] == entry["counter"]["error"] == 0 and
                owner["error"] == "NONE" and owner["faults"] == owner["backend_fault"] == 0,
                f"{name}: unexpected fault")
        require((owner["run_id"], owner["generation"]) ==
                (entry["link"]["run"], entry["link"]["generation"]), f"{name}: identity mismatch")
        identity = {key: entry["link"][key] for key in IDENTITY_FIELDS}
        baseline = report.get("lifecycle_identity")
        pending_start = ((baseline is None and owner["state"] in ("IDLE", "STARTING")) or
                         (restarting and baseline is not None and identity["run"] == baseline["run"] and
                          owner["state"] in ("IDLE", "STARTING")))
        if pending_start:
            require(time.monotonic() < deadline, f"{name}: start acknowledgement timed out")
            time.sleep(bench.args.poll)
            continue
        if baseline is None:
            baseline = report["lifecycle_identity"] = identity
        require(all(identity[key] == baseline[key] for key in IDENTITY_FIELDS
                    if not (restarting and key == "run")), f"{name}: lifecycle identity changed")
        if restarting:
            require(identity["run"] > baseline["run"], "restart did not create a new run")
            if restart_identity is None:
                restart_identity = identity
            require(identity == restart_identity, "restart identity changed before acknowledgement")
        if predicate(entry):
            require(time.monotonic() < deadline, f"{name}: observation exceeded deadline")
            return entry
        require(time.monotonic() < deadline, f"{name}: timed out")
        time.sleep(bench.args.poll)


def run_lifecycle(bench, port, report):
    """Continuous mode: count through PAUSE, complete one position, STOP/restart."""
    report["passed"] = False
    report["input_simulation"] = {
        "method": "SCPI", "counter_command": f"TRIG:SEQ:INJECT {getattr(bench.args, 'counter_input', 'IN1')},<count>",
        "ready_command": None,
        "physical_edge_count_verified": False, "physical_ready_verified": True}
    configure(bench, report, repeat=0)
    first = lifecycle_observation(bench, report, "waiting",
        lambda e: e["link"]["phase"] == 9 and e["owner"]["state"] == "READY")
    require(first["counter"]["positions"] == first["link"]["triggers"] == 0,
            "lifecycle needs a larger N to pause before first threshold")
    inject_counter(bench, report, 1, "lifecycle initial partial")
    first = lifecycle_observation(bench, report, "initial_partial",
        lambda e: e["link"]["phase"] == 9 and e["counter"]["events"] == 1)
    bench.write("TRIG:PAUS")
    paused = lifecycle_observation(bench, report, "paused",
        lambda e: e["owner"]["state"] == "PAUSED" and e["link"]["phase"] == 6)
    paused_batch = min(3, bench.args.threshold - 2)
    inject_counter(bench, report, paused_batch, "lifecycle paused partial")
    counted = lifecycle_observation(bench, report, "counting_while_paused",
        lambda e: e["counter"]["events"] == paused["counter"]["events"] + paused_batch)
    require(counted["owner"]["state"] == "PAUSED" and
            counted["counter"]["positions"] == counted["link"]["triggers"] == 0,
            "pause sampled or stopped counting")
    bench.write("TRIG:CONT")
    resumed = lifecycle_observation(bench, report, "resumed",
        lambda e: e["owner"]["state"] == "READY" and e["link"]["phase"] == 9)
    require(resumed["counter"]["events"] >= counted["counter"]["events"], "resume reset counter")
    inject_counter(bench, report, bench.args.threshold - counted["counter"]["events"],
                   "lifecycle position boundary")
    done = lifecycle_observation(bench, report, "position_complete",
        position_finished_observed)
    require(done["link"]["repeat"] == 0 and done["link"]["triggers"] == done["link"]["ready"] == 8 and
            done["owner"]["accepted"] == done["owner"]["completed"] == 7,
            "continuous mode did not complete exactly one full position")
    require(all(done["owner"][key] == 7 for key in
                ("current_index", "current_state", "completed_index", "completed_state")),
            "continuous mode ended at wrong state")
    report["history"] = records = [parse_uints(bench.command(f"READ:SEQ:HIST? {ordinal}"),
        HISTORY_FIELDS) for ordinal in range(1, 9)]
    report["position_cycles"] = check_history(records, done["link"], bench.args.threshold,
        positions=1, target_ms=bench.args.position_cycle_target_ms)
    for name in ("stop", "restart_stop"):
        if name == "restart_stop":
            bench.write("TRIG:START")
            restarted = lifecycle_observation(bench, report, "restarted",
                lambda e: e["link"]["phase"] == 9 and e["owner"]["state"] == "READY", restarting=True)
            require(restarted["link"]["run"] != done["link"]["run"] and
                    restarted["counter"]["positions"] == restarted["counter"]["history_total"] == 0 and
                    restarted["counter"]["events"] < bench.args.threshold,
                    "restart retained old run or position")
        bench.write("TRIG:STOP")
        report[name] = state = bench.wait_state("IDLE")
        require(state["error"] == "NONE" and state["faults"] == state["backend_fault"] == 0,
                "lifecycle STOP fault")
        report[name + "_io"] = io = read_io(bench)
        require(all(io[k] == 0 for k in ("outputs", "owned", "armed", "busy")), "STOP did not release IO")
    report["passed"] = True


def position_finished_observed(entry):
    # These are separate SCPI reads. WAIT_COUNT before the first threshold
    # followed by positions=1 in WAIT_COUNTER_RETURN is not a completed round.
    return (entry["link"]["phase"] == entry["counter"]["phase"] == 9 and
            entry["counter"]["positions"] == 1)


def cleanup(bench, port, report):
    failures = report.setdefault("cleanup_failures", [])
    if getattr(bench.args, "schedule_evidence", False):
        try:
            capture_schedule(bench, report, "before_cleanup")
        except (Exception, KeyboardInterrupt) as exc:
            failures.append(f"scheduler evidence: {type(exc).__name__}: {exc}")
    try:
        bench.write("TRIG:STOP")
        report["stopped"] = bench.wait_state("IDLE")
        report["stopped_io"] = io = read_io(bench)
        require(all(io[k] == 0 for k in ("outputs", "owned", "armed", "busy")), "outputs not released")
    except (Exception, KeyboardInterrupt) as exc:
        failures.append(f"sequence stop: {type(exc).__name__}: {exc}")
    try:
        report["ring_stop"] = ring.checked_action(port, "SYSTem:TDMA:RING:STOP", bench.args.timeout)
        report["ring_stopped_samples"] = rows = []
        deadline = time.monotonic() + bench.args.timeout
        while True:
            rows.append(ring_snapshot(port, bench.args, report, "cleanup"))
            if all(ring.field(rows[-1], i) == 0 for i in (ring.RING_ENABLED, ring.RING_ADAPTER_STARTED)):
                break
            require(time.monotonic() < deadline, "ring stop readback did not confirm stopped owner")
            time.sleep(bench.args.poll)
    except (Exception, KeyboardInterrupt) as exc:
        failures.append(f"ring stop: {type(exc).__name__}: {exc}")
    try:
        report["probe_disable"] = ring.checked_action(port, "CALibration:TOPology:PROBe 0", bench.args.timeout)
    except (Exception, KeyboardInterrupt) as exc:
        failures.append(f"probe disable: {type(exc).__name__}: {exc}")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--threshold", type=int, default=100)
    parser.add_argument("--counter-input", choices=("IN1", "IN3", "IN4"), default="IN1",
                        help="counter input; use an unused input for software injection, IN2 is READY")
    parser.add_argument("--positions", type=int, default=2, help="finite external-input position count")
    parser.add_argument("--timing-evidence", action="store_true",
                        help="collect versioned raw TIMER1 stage offsets for each completed state")
    parser.add_argument("--diagnostic-stress", action="store_true",
                        help="query TDMA/PHY and check SCPI errors after each external-input poll")
    parser.add_argument("--quiet-capture", action="store_true",
                        help="no queries while running; drain retained history after natural finish (implies timing-evidence)")
    parser.add_argument("--external-input", action="store_true",
                        help="finite positions using physical IN1 and OUT4-to-IN2 only; no injections")
    parser.add_argument("--angle-scan", action="store_true",
                        help="bind an ANGLE sweep and verify real configuration/cursor")
    parser.add_argument("--lifecycle", action="store_true", help="also verify continuous PAUSE/CONT and STOP/restart")
    parser.add_argument("--schedule-evidence", action="store_true",
                        help="retain scheduler snapshots; functional PASS does not imply realtime PASS")
    parser.add_argument("--source-hz", type=float, default=50, help="declared, not measured")
    parser.add_argument("--duration", type=float, default=30, help="timeout per profile")
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--poll", type=float, default=.002)
    parser.add_argument("--settle-us", type=int, default=10)
    parser.add_argument("--gateway-pulse-us", type=int, default=1000)
    parser.add_argument("--gateway-timeout-ms", type=int, default=10000)
    parser.add_argument("--position-cycle-target-ms", type=int, default=200)
    parser.add_argument("--operating-level", type=int, default=7,
                        help="TDMA operating profile: level 7 is 1 ms, level 14 is 100 us")
    args = parser.parse_args(argv)
    if not 1 <= args.positions <= 0xffffffff // 8 or args.positions * args.threshold >= gui.COUNTER_THRESHOLD_MAX + 1:
        parser.error("position count or cumulative pulse count exceeds firmware range")
    if args.positions != 2 and not args.external_input:
        parser.error("custom positions requires external-input")
    if args.angle_scan and not args.external_input:
        parser.error("angle-scan requires external-input")
    if args.timing_evidence and not args.external_input:
        parser.error("timing-evidence requires external-input")
    if args.diagnostic_stress and not args.external_input:
        parser.error("diagnostic-stress requires external-input")
    if args.quiet_capture and (not args.external_input or args.diagnostic_stress):
        parser.error("quiet-capture requires external-input and excludes diagnostic-stress")
    if args.quiet_capture:
        args.timing_evidence = True
    if args.external_input and args.lifecycle:
        parser.error("external-input does not run the software-injected lifecycle profile")
    if args.external_input and args.counter_input != "IN1":
        parser.error("external-input acceptance uses the declared physical IN1 source")
    if not 3 <= args.threshold <= 0xffffffff // 3:
        parser.error("threshold must support partial injection and three boundaries within uint32")
    if any(not math.isfinite(v) or v <= 0 for v in (args.duration, args.timeout, args.poll, args.source_hz)):
        parser.error("times and source frequency must be finite and positive")
    if not 0 <= args.settle_us <= gui.TIME_MAX_US or not 1 <= args.gateway_pulse_us <= gui.TIME_MAX_US or not 1 <= args.gateway_timeout_ms <= 0x7fffffff or not 1 <= args.position_cycle_target_ms <= 0x7fffffff:
        parser.error("invalid timing configuration")
    if not 0 <= args.operating_level <= 0xffffffff:
        parser.error("operating-level must be uint32")
    if args.quiet_capture and args.duration < quiet_wait_seconds(args):
        parser.error("duration shorter than quiet capture window")
    return args


def main(argv=None):
    args = parse_args(argv)
    report = {"passed": False, "scope": "single_board_position_external_finite_positions" if args.external_input
              else "single_board_position_two_rounds_and_busy_boundary",
        "software_input_simulation_verified": not args.external_input,
        "probe_mode": "post_run_vector" if args.quiet_capture else "streaming",
        "waveform_verified": False, "independent_input_count_verified": False,
        "rf_path_verified": False, "multi_board_verified": False, "p3_receipt": False,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "settings": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
        "transcript": [], "transport_transcript": [], "failure": None, "cleanup_failures": []}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("x", encoding="utf-8") as evidence:
        try:
            report["devices"] = discover()
            select_transport(args, report["devices"])
            report["transport"] = args.port or args.visa_resource
            with ExitStack() as stack:
                if args.port:
                    physical = stack.enter_context(open_serial_port(args.port, 115200, args.timeout, .1, read_timeout_s=.02))
                else:
                    instrument = stack.enter_context(open_visa_resource(args.visa_resource, args.timeout))
                    physical = VisaPort(instrument, args.timeout)
                port = ring.EvidencePort(physical, report["transport_transcript"])
                bench = Bench(physical, args, report, lambda cmd, timeout: ring.query(port, cmd, timeout))
                bench.identity()  # No mutation, including cleanup, before exact UID/build match.
                try:
                    if args.external_input:
                        report["two_positions"] = {}
                        run_external_profile(bench, port, report["two_positions"])
                    else:
                        for name, manual in (("two_positions", False), ("busy_boundary", True)):
                            report[name] = {}
                            run_profile(bench, port, report[name], manual)
                    if args.lifecycle:
                        report["lifecycle"] = {}
                        run_lifecycle(bench, port, report["lifecycle"])
                    bench.identity()
                finally:
                    cleanup(bench, port, report)
                require(not report["cleanup_failures"], "cleanup verification failed")
                report["passed"] = True
        except (Exception, KeyboardInterrupt) as exc:
            report["failure"] = f"{type(exc).__name__}: {exc}"
        finally:
            json.dump(report, evidence, indent=2, ensure_ascii=False)
            evidence.write("\n")
    print(json.dumps({"passed": report["passed"], "failure": report["failure"], "evidence": str(args.out)}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
