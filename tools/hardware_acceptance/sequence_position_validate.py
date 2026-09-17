#!/usr/bin/env python3
"""Validate position-driven SP8T using SCPI-simulated IN1 edge batches.

RJ45 TDMA traffic and OUT4-to-IN2 READY remain physical. IN1 edges are
software-injected acceptance stimuli and are not independent edge evidence.
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
    LINK_FIELDS, IDENTITY_FIELDS, VisaPort, gui_batch,
)
from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
from tools.tdma_ring_monitor import tdma_single_board_loopback as ring

COUNTER_FIELDS = ("enabled", "slot", "input", "threshold", "events", "positions", "partial",
                  "fault_events", "history_total", "history_retained", "phase", "error")
HISTORY_FIELDS = ("ordinal", "run", "generation", "binding_epoch", "exchange_id",
                  "position", "sequence_index", "sequence_state", "output_code",
                  "threshold_pulses", "observed_pulses", "trigger_ordinal", "ready_ordinal",
                  "position_admitted_tick_ms", "sample_done_tick_ms", "cycle_elapsed_ms",
                  "outcome_flags")
# Snapshot of sync_io_sequence_fault_t::SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY.
BACKEND_COUNTER_BUSY = 14


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


def configure(bench, report, manual_ready=False, repeat=2):
    args = bench.args
    report["gui_control"] = {"path": "GUI POSITION builders and batch executor",
        "mode": gui.MODE_TURNTABLE,
        "module_sha256": hashlib.sha256(Path(gui.__file__).read_bytes()).hexdigest()}
    commands = gui.build_mode_configuration(gui.MODE_TURNTABLE, "SP8T", list(range(8)),
        "MANUAL", "RIS", args.settle_us, args.gateway_pulse_us, 7, 0, "NONE",
        "MANUAL" if manual_ready else "IN2", args.gateway_timeout_ms, repeat,
        counter_slot=1, dut_slot=2, vna_slot=3, counter_input="IN1", counter_threshold=args.threshold)
    gui_batch(bench, report, "configure", commands)
    report["configured"] = sample(bench)
    row, counter = report["configured"]["link"], report["configured"]["counter"]
    require([counter[k] for k in ("enabled", "slot", "input", "threshold")] ==
            [1, 1, 1, args.threshold], "counter configuration readback mismatch")
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
    gui_batch(bench, report, "start", gui.build_start_commands(gui.MODE_TURNTABLE))


def inject_counter(bench, report, count, purpose):
    command = f"TRIG:SEQ:INJECT IN1,{count}"
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
        require(((record["sample_done_tick_ms"] - record["position_admitted_tick_ms"]) & 0xffffffff) ==
                record["cycle_elapsed_ms"], "history software timing is incoherent")
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
            require(status["accepted"] == status["completed"] == 15, "terminal step accounting mismatch")
            if status["state"] == "IDLE":
                report["terminal_repeat"] = parse_uints(bench.command("READ:SEQ:REPEAT?"),
                    ("configured", "run", "finished"))
                require(report["terminal_repeat"] == dict(configured=2, run=2, finished=1),
                        "Core1 did not finish both positions")
                break
        require(time.monotonic() < deadline, "terminal owner acknowledgement timed out")
        time.sleep(bench.args.poll)


def run_profile(bench, port, report, manual_ready=False):
    args = bench.args
    report.update(passed=False, samples=[], no_premature_sample_observed=False)
    configure(bench, report, manual_ready)
    report["input_simulation"] = {
        "method": "SCPI", "counter_command": "TRIG:SEQ:INJECT IN1,<count>",
        "ready_command": "TRIG:SEQ:INJECT READY,<count>" if manual_ready else None,
        "physical_edge_count_verified": False, "physical_ready_verified": not manual_ready}
    report["ring_before"] = ring.sample(port, args.timeout)
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
    report["ring_after"] = ring.sample(port, args.timeout)
    if getattr(args, "schedule_evidence", False):
        capture_schedule(bench, report, "after")
    for index in (ring.RING_ADAPTER_TX_COUNT, ring.RING_ADAPTER_RX_COUNT):
        require(ring.delta(report["ring_before"], report["ring_after"], index) > 0,
                "no physical TDMA counter growth")
    report["passed"] = True


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
        "method": "SCPI", "counter_command": "TRIG:SEQ:INJECT IN1,<count>",
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
            rows.append(ring.sample(port, bench.args.timeout))
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
    args = parser.parse_args(argv)
    if not 3 <= args.threshold <= 0xffffffff // 3:
        parser.error("threshold must support partial injection and three boundaries within uint32")
    if any(not math.isfinite(v) or v <= 0 for v in (args.duration, args.timeout, args.poll, args.source_hz)):
        parser.error("times and source frequency must be finite and positive")
    if not 0 <= args.settle_us <= 0xffffffff // 10 or not 1 <= args.gateway_pulse_us <= 0xffffffff // 10 or not 1 <= args.gateway_timeout_ms <= 0x7fffffff or not 1 <= args.position_cycle_target_ms <= 0x7fffffff:
        parser.error("invalid timing configuration")
    return args


def main(argv=None):
    args = parse_args(argv)
    report = {"passed": False, "scope": "single_board_position_two_rounds_and_busy_boundary",
        "software_input_simulation_verified": True,
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
