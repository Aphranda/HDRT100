#!/usr/bin/env python3
"""Exercise the single-board RJ45 DUT/VNA cycle through the physical RJ45 return.

READY may come from IN1 or the explicit SCPI NEXT command; OUT4 is the VNA
trigger. RJ45 output must be cabled to RJ45 input. This is functional evidence,
not a multi-board, source-edge-count, pulse-width, RF or TDMA stability receipt.
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
    Bench, discover, open_serial_port, open_visa_resource, read_io, require,
    parse_role, role_config, select_transport, visa_command,
)
from tools.tdma_ring_monitor import tdma_single_board_loopback as ring
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError

LINK_FIELDS = ("enabled", "phase", "error", "binding_epoch", "model_epoch", "run",
               "generation", "step", "txfragments", "rxmessages", "rejected", "triggers",
               "ready", "completed", "dutslot", "vnaslot", "input", "outputmask",
               "pulseus", "timeoutms", "falling", "repeat", "exchange_id")
IDENTITY_FIELDS = ("binding_epoch", "model_epoch", "run", "generation")
COUNTERS = ("step", "txfragments", "rxmessages", "rejected", "triggers", "ready", "completed")
TRANSPORT_FIELDS = ("enabled", "seen", "mailbox_seq16", "last_reject", "matches", "published",
                    "snapshot_quality")
TRANSPORT_SNAPSHOT_QUALITIES = ("UNAVAILABLE", "FRESH", "CACHED")
TRANSPORT_REJECTIONS = (
    "NONE", "DISABLED", "LAYOUT", "ROUTE", "MAILBOX",
    "TX_SEQUENCE_IDENTITY_OR_SIZE", "MAILBOX_BYTES", "HEADER_BYTES",
    "ORIGIN_NOT_FEEDBACK", "STALE_MAILBOX_SEQUENCE",
)


def parse_transport(response: str) -> dict:
    """Decode facts only: the last rejection is not a proven root cause."""
    parts = response.split(",")
    require(len(parts) == len(TRANSPORT_FIELDS) and
            all(part.isascii() and part.isdecimal() for part in parts),
            "malformed LINK transport snapshot")
    values = list(map(int, parts))
    require(all(value <= 0xffffffff for value in values), "LINK transport field overflow")
    row = dict(zip(TRANSPORT_FIELDS, values))
    require(row["enabled"] <= 1 and row["seen"] <= 1 and row["mailbox_seq16"] <= 0xffff and
            row["snapshot_quality"] < len(TRANSPORT_SNAPSHOT_QUALITIES),
            "LINK transport field outside range")
    reason = row["last_reject"]
    row["last_reject_name"] = TRANSPORT_REJECTIONS[reason] if reason < len(TRANSPORT_REJECTIONS) else "UNKNOWN"
    row["snapshot_quality_name"] = TRANSPORT_SNAPSHOT_QUALITIES[row["snapshot_quality"]]
    return row


def parse_link(response: str) -> dict:
    parts = response.split(",")
    require(len(parts) == len(LINK_FIELDS) and
            all(p.isascii() and p.isdecimal() for p in parts), "malformed LINK snapshot")
    values = list(map(int, parts))
    require(all(v <= 0xffffffff for v in values), "LINK field overflow")
    row = dict(zip(LINK_FIELDS, values))
    require(row["enabled"] <= 1 and row["falling"] <= 1 and row["phase"] <= 8 and
            row["input"] <= 4 and row["outputmask"] <= 15, "LINK field outside range")
    return row


class VisaPort:
    """Serial-shaped response buffer so the existing EvidencePort works with VISA.

    One write invokes exactly one existing VISA exchange; it never retries a
    mutating command. The physical VISA resource stays open for the whole run.
    """
    def __init__(self, instrument, timeout):
        self.instrument, self.timeout = instrument, timeout
        self.buffer = b""

    def reset_input_buffer(self):
        self.buffer = b""

    def write(self, data):
        reply = visa_command(self.instrument, data.decode("ascii").strip(), self.timeout)
        self.buffer = (reply + "\n").encode("utf-8")
        return len(data)

    def flush(self):
        pass

    def read(self, count=1):
        chunk, self.buffer = self.buffer[:count], self.buffer[count:]
        return chunk


def link(bench: Bench) -> dict:
    return parse_link(bench.command("READ:SEQ:LINK?"))


def prepare_ring(port, args, report, before_arm=None):
    setup = report.setdefault("ring_setup", {})
    try:
        ring.prepare_single_board_ring(port, args, setup, before_arm=before_arm)
    except RuntimeError as exc:
        setup["exception"] = str(exc)
        # Defer only the already-known down_running observation after START.
        # All configuration/ARM/TRAIN/START failures remain failures.
        steps = setup.get("steps", [])
        require(str(exc).startswith("ring state wait expired:") and steps and
                steps[-1]["command"] == "SYSTem:TDMA:RING:START",
                f"TDMA setup failed: {exc}")
        observation = ring.sample(port, args.timeout)
        setup["after_wait_failure"] = observation
        require(all(ring.field(observation, index) == 1 for index in
                    (ring.RING_ENABLED, ring.RING_ADAPTER_STARTED, ring.RING_UP_RUNNING)) and
                ring.field(observation, ring.RING_DOWN_RUNNING) == 0,
                "TDMA setup failed for a reason other than deferred down_running")
        report["deferred_observations"].append({"kind": "down_running", "failure": str(exc)})


def configure(bench: Bench, port, report):
    args = bench.args
    if args.gui_control:
        configure_gui(bench, report)
        verify_link_configuration(bench, report)
        return
    bench.write("TRIG:STOP")
    bench.wait_state("IDLE")
    report["initial_ring_stop"] = ring.checked_action(port, "SYSTem:TDMA:RING:STOP", args.timeout)
    role_config(bench, report.setdefault("roles", {}))
    args.source, args.edge, args.settle_us, args.pulse_us = "BUS", "RIS", 10, 10
    bench.configure()
    bench.write("CONF:SEQ:OUTPUT 7,0,NONE,10,0")
    bench.write(f"CONF:SEQ:REPEAT {args.repeat}")
    def bind_link():
        report["flight_mode"] = ring.checked_action(port, "SYST:TDMA:FLIGHT:MODE 1", args.timeout)
        report["flight_mode_readback"] = ring.query(port, "SYST:TDMA:FLIGHT:MODE?", args.timeout)
        require(report["flight_mode_readback"] == "2", "TDMA process-image forwarding mode not active")
        bench.write(f"CONF:SEQ:LINK LOOPBACK,{args.dut_slot},{args.vna_slot},IN1,OUT4,"
                    f"{args.gateway_pulse_us},{args.gateway_timeout_ms},RIS")
    prepare_ring(port, args, report, before_arm=bind_link)
    verify_link_configuration(bench, report)


def verify_link_configuration(bench: Bench, report):
    args = bench.args
    row = link(bench)
    report["link_configuration"] = row
    require(row["enabled"] == 1 and row["phase"] == 1 and row["error"] == 0 and
            [row[k] for k in ("dutslot", "vnaslot", "input", "outputmask", "pulseus", "timeoutms", "falling")] ==
            [args.dut_slot, args.vna_slot, 1, 8, args.gateway_pulse_us, args.gateway_timeout_ms, 0],
            "LINK configuration readback mismatch")


def gui_batch(bench: Bench, report, stage: str, commands: list[str]):
    from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
    evidence = report.setdefault("gui_control", {"path": "GUI command builders and batch executor",
        "mode": gui.MODE_RJ45, "module_sha256": hashlib.sha256(Path(gui.__file__).read_bytes()).hexdigest()})
    record = evidence[stage] = {"commands": commands, "exchanges": [], "completed": False}
    def exchange(command):
        try:
            return bench.command(command)
        except AcceptanceError:
            # The GUI verifies asynchronous ring actions through their error
            # queue and status even when the action ACK times out. Preserve
            # the exact recorded timeout, and let that same executor decide.
            transcript = bench.report["transcript"]
            if (command.split(maxsplit=1)[0].upper() in gui.RING_ACK_ONLY and transcript and
                    transcript[-1]["command"] == command and transcript[-1]["response"] == "<timeout>"):
                return "<timeout>"
            raise
    gui.execute_command_batch(commands, exchange,
        lambda command, response: record["exchanges"].append({"command": command, "response": response}))
    record["completed"] = True


def configure_gui(bench: Bench, report):
    from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
    args = bench.args
    gui_batch(bench, report, "configure", gui.build_mode_configuration(
        gui.MODE_RJ45, "SP8T", list(range(8)), "BUS", "RIS", 10,
        args.gateway_pulse_us, 7, 0, "NONE", "IN1", args.gateway_timeout_ms, args.repeat))
    report["flight_mode_readback"] = bench.command("SYST:TDMA:FLIGHT:MODE?")
    require(report["flight_mode_readback"] == "2", "TDMA process-image forwarding mode not active")
    plan = next(csv.reader([bench.command("READ:SEQ? SP8T")]))
    require(plan[-8:] == [str(code) for code in range(8)], "SP8T plan readback mismatch")
    for code in range(8):
        require(bench.command(f"READ:SEQ:CODE? {code}") == f"{code},{code}", "code readback mismatch")
    require(next(csv.reader([bench.command("READ:SEQ:SOUR?")])) == ["BUS", "RISING"],
            "input source readback mismatch")
    io = next(csv.reader([bench.command("READ:SEQ:OUTPUT?")]))
    require(io[:5] == ["7", "0", "NONE", "10", "0"] and io[-1] == "1",
            "IO configuration readback mismatch")
    roles = report.setdefault("roles", {})["after"] = {}
    for instance, role, io_mask, ip in ((5, "DUT", 11, 5), (7, "VNA", 3, 3)):
        row = parse_role(bench.command(f"READ:SEQ:NODE:ROLE? {instance}"), instance, role)
        roles[role] = row
        require([row[k] for k in ("active_enabled", "active_resource", "active_io", "active_ip")] ==
                [1, 152, io_mask, ip], "wrong active role declarations")


def check_progress(row: dict, previous: dict | None, accounting_base: dict | None = None):
    require(row["enabled"] == 1 and row["phase"] in (2, 3, 4, 5, 8) and row["error"] == 0,
            "LINK did not enter a healthy running phase")
    require(row["run"] != 0 and row["generation"] != 0 and row["exchange_id"] != 0,
            "missing sequence run or exchange identity")
    counts = {k: row[k] - (accounting_base[k] if accounting_base else 0)
              for k in ("completed", "ready", "triggers")}
    require(0 <= counts["completed"] <= counts["ready"] <= counts["triggers"] <= counts["completed"] + 1,
            "measurement/READY/step accounting mismatch")
    if previous is not None:
        require(all(row[k] == previous[k] for k in IDENTITY_FIELDS), "LINK run or binding changed")
        require(all(row[k] >= previous[k] for k in COUNTERS), "LINK counters regressed")


def check_sequence_status(status, row, states):
    require(status["state"] in states and status["error"] == "NONE" and
            status["faults"] == status["backend_fault"] == 0, "sequence runtime fault")
    require((status["run_id"], status["generation"]) == (row["run"], row["generation"]),
            "sequence and LINK identities disagree")


def drive_scpi_next(bench: Bench, report: dict, row: dict) -> None:
    if not bench.args.scpi_next or row["phase"] != 3:
        return
    identity = [row[key] for key in ("run", "generation", "step", "exchange_id")]
    records = report.setdefault("scpi_next", [])
    if any(record.get("identity") == identity for record in records):
        return
    response = bench.command("TRIG:SEQ:NEXT")
    records.append({"identity": identity, "response": response})
    require(response == "1", "TRIG:SEQ:NEXT was not accepted")


def pause_resume(bench: Bench, report, before: dict) -> tuple[dict, dict]:
    """Verify software counters and sampled pads, not unsampled pulse absence.

    PAUSE cancels any pending gateway measurement. CONT remeasures the settled
    DUT state, so post-resume accounting starts at the paused counters.
    """
    args = bench.args
    record = report["pause_resume"] = {"passed": False, "before": before,
        "pause_command": "TRIG:PAUS", "resume_command": "TRIG:CONT",
        "pulse_stop_evidence": "stable trigger count and sampled OUT4 low; not waveform capture",
        "settling": [], "quiet_samples": [], "resume_samples": []}
    bench.write("TRIG:PAUS")
    deadline = time.monotonic() + args.timeout
    while True:
        status, row = bench.status(), link(bench)
        record["settling"].append({"sequence": status, "link": row})
        require(row["error"] == 0 and row["enabled"] == 1 and row["repeat"] == 0,
                "LINK fault while pausing")
        require(all(row[k] == before[k] for k in IDENTITY_FIELDS), "LINK run or binding changed while pausing")
        require(all(row[k] >= before[k] for k in COUNTERS), "LINK counters regressed while pausing")
        check_sequence_status(status, row, ("READY", "BUSY", "PAUSING", "PAUSED"))
        if status["state"] == "PAUSED" and row["phase"] == 6:
            break
        require(time.monotonic() < deadline, "timeout waiting for PAUSED and LINK paused")
        time.sleep(args.poll)
    paused, paused_status = row, status
    first_io = read_io(bench)
    record["paused"] = {"sequence": paused_status, "link": paused, "io": first_io}
    require(first_io["outputs"] & paused["outputmask"] == 0, "gateway output remained high while paused")
    require(first_io["outputs"] & 7 == paused_status["current_state"], "paused DUT encoding mismatch")
    # Source frequency is a user declaration, not an independent edge count.
    quiet = max(args.quiet, 3 / args.source_hz, 2 * args.gateway_pulse_us / 1e6)
    record["quiet_duration_requested_s"] = quiet
    quiet_start = time.monotonic()
    while True:
        time.sleep(min(args.poll, quiet))
        status, row, io = bench.status(), link(bench), read_io(bench)
        record["quiet_samples"].append({"sequence": status, "link": row, "io": io})
        require(row["phase"] == 6 and row["error"] == 0 and row["enabled"] == 1 and row["repeat"] == 0,
                "LINK left paused phase")
        check_sequence_status(status, row, ("PAUSED",))
        require(all(row[k] == paused[k] for k in (*IDENTITY_FIELDS, *COUNTERS)),
                "LINK activity continued while paused")
        require(all(status[k] == paused_status[k] for k in
                    ("current_index", "current_state", "accepted", "completed")),
                "DUT sequence advanced while paused")
        require(io["outputs"] == first_io["outputs"] and io["outputs"] & row["outputmask"] == 0,
                "outputs changed while paused")
        if time.monotonic() - quiet_start >= quiet:
            break
    record["quiet_elapsed_s"] = time.monotonic() - quiet_start
    bench.write("TRIG:CONT")  # Registered firmware command; RESUME is not an alias.
    deadline = time.monotonic() + args.timeout
    previous = paused
    while True:
        row, status = link(bench), bench.status()
        record["resume_samples"].append({"link": row, "sequence": status})
        drive_scpi_next(bench, report, row)
        require(all(row[k] == paused[k] for k in IDENTITY_FIELDS), "LINK run or binding changed on resume")
        require(row["repeat"] == 0, "run repeat configuration changed on resume")
        if row["phase"] != 6:
            require(row["exchange_id"] != paused["exchange_id"],
                    "LINK exchange identity did not rotate on resume")
            check_progress(row, previous, paused)
            require(row["phase"] != 8, "continuous run unexpectedly ended on resume")
            check_sequence_status(status, row, ("READY", "BUSY"))
            previous = row
            if all(row[k] > paused[k] for k in ("completed", "ready", "triggers")):
                record["exchange_rotated"] = True
                record["passed"] = True
                return paused, row
        else:
            require(row["error"] == 0 and row["enabled"] == 1 and
                    all(row[k] == paused[k] for k in COUNTERS), "LINK changed before resume")
            check_sequence_status(status, row, ("PAUSED", "READY", "BUSY"))
        require(time.monotonic() < deadline, "no completed RJ45 cycle after resume")
        time.sleep(args.poll)


def execute(bench: Bench, port, report):
    args = bench.args
    configure(bench, port, report)
    report["ring_before"] = ring.sample(port, args.timeout)
    if args.gui_control:
        from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
        gui_batch(bench, report, "start", gui.build_start_commands(gui.MODE_RJ45))
    else:
        bench.write("TRIG:START")
    previous = None
    accounting_base = None
    completed_target = args.minimum_events
    deadline = time.monotonic() + args.duration
    report["samples"] = []
    while time.monotonic() < deadline:
        row = link(bench)
        entry = {"link": row}
        report["samples"].append(entry)  # preserve the offending snapshot too
        if row["phase"] == 1 and previous is None:
            require(row["error"] == 0, "LINK start error")
            time.sleep(args.poll)
            continue
        drive_scpi_next(bench, report, row)
        check_progress(row, previous, accounting_base)
        require(row["repeat"] == args.repeat, "run repeat configuration mismatch")
        require(args.repeat != 0 or row["phase"] != 8, "continuous run unexpectedly ended")
        status = bench.status()
        entry["sequence"] = status
        check_sequence_status(status, row, ("READY", "BUSY", "STOPPING", "IDLE"))
        previous = row
        if args.repeat and row["phase"] == 8:
            require(row["ready"] == row["triggers"] == 8 * args.repeat and
                    row["completed"] == 8 * args.repeat - 1, "finite measurement count mismatch")
            bench.wait_state("IDLE")
            report["repeat_result"] = bench.command("READ:SEQ:REP?")
            require(report["repeat_result"] == f"{args.repeat},{args.repeat},1", "finite run not finished")
            break
        if not args.repeat and row["completed"] >= completed_target:
            if args.pause_resume and accounting_base is None:
                accounting_base, previous = pause_resume(bench, report, row)
                completed_target = accounting_base["completed"] + args.minimum_events
                continue
            break
        time.sleep(args.poll)
    require(previous is not None and
            ((args.repeat and previous["phase"] == 8) or
             (not args.repeat and previous["completed"] >= completed_target)),
            "insufficient completed RJ45 cycles; inspect READY source and transcript")
    require(not args.pause_resume or report.get("pause_resume", {}).get("passed", False),
            "pause/resume verification incomplete")
    report["ring_after"] = ring.sample(port, args.timeout)
    for index, name in ((ring.RING_ADAPTER_TX_COUNT, "tx"), (ring.RING_ADAPTER_RX_COUNT, "rx")):
        require(ring.delta(report["ring_before"], report["ring_after"], index) > 0,
                f"no physical TDMA {name} counter growth")
    report["functional_cycle_verified"] = True
    bench.identity()


def cleanup(bench, port, report):
    failures = report.setdefault("cleanup_failures", [])
    diagnostics = report.setdefault("transport_before_cleanup", {})
    for command in ("READ:SEQ:LINK:TRANSPORT?", "SYST:TDMA:FLIGHT:PROCESS?",
                    "SYST:TDMA:FLIGHT:FIFO?", "SYST:REFMEM:SYNC:FLIGHT?"):
        try:
            record = diagnostics[command] = {"error_before": bench.command("SYST:ERR?")}
            record["response"] = bench.command(command)
            record["error_after"] = bench.command("SYST:ERR?")
            if not record["error_before"].lstrip().startswith("0,"):
                failures.append(f"SCPI error pending before {command}: {record['error_before']}")
            if not record["error_after"].lstrip().startswith("0,"):
                failures.append(f"SCPI error from {command}: {record['error_after']}")
            if command == "READ:SEQ:LINK:TRANSPORT?":
                try:
                    record["parsed"] = parse_transport(record["response"])
                    if record["parsed"]["snapshot_quality"] == 0:
                        failures.append("LINK transport snapshot unavailable")
                except AcceptanceError as exc:
                    record["parse_failure"] = str(exc)
        except (Exception, KeyboardInterrupt) as exc:
            diagnostics[command] = {"failure": f"{type(exc).__name__}: {exc}"}
    try:
        report["ring_before_cleanup"] = ring.sample(port, bench.args.timeout)
    except (Exception, KeyboardInterrupt) as exc:
        report["diagnostic_failure"] = f"{type(exc).__name__}: {exc}"
    try:
        bench.write("TRIG:STOP")
        stopped = bench.wait_state("IDLE")
        first = link(bench)
        first_io = read_io(bench)
        report["stopped"] = {"sequence": stopped, "link": first, "io": first_io}
        require(all(first_io[k] == 0 for k in ("outputs", "owned", "armed", "busy")),
                "STOP did not release outputs to zero")
        time.sleep(bench.args.quiet)
        last, last_io = link(bench), read_io(bench)
        report["after_stop_quiet"] = {"link": last, "io": last_io}
        require(all(first[k] == last[k] for k in (*IDENTITY_FIELDS, *COUNTERS)),
                "LINK counters changed after STOP")
        require(all(last_io[k] == 0 for k in ("outputs", "owned", "armed", "busy")),
                "outputs changed after STOP")
    except (Exception, KeyboardInterrupt) as exc:
        failures.append(f"sequence stop: {type(exc).__name__}: {exc}")
    try:
        report["ring_stop"] = ring.checked_action(port, "SYSTem:TDMA:RING:STOP", bench.args.timeout)
        observations = report["ring_stop_readbacks"] = []
        deadline = time.monotonic() + bench.args.timeout
        while True:
            stopped_ring = ring.sample(port, bench.args.timeout)
            observations.append(stopped_ring)
            if all(ring.field(stopped_ring, index) == 0 for index in
                   (ring.RING_ENABLED, ring.RING_ADAPTER_STARTED)):
                break
            require(time.monotonic() < deadline, "TDMA STOP readback did not confirm stopped owner")
            time.sleep(bench.args.poll)
    except (Exception, KeyboardInterrupt) as exc:
        failures.append(f"TDMA stop: {type(exc).__name__}: {exc}")
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
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--minimum-events", type=int, default=9)
    parser.add_argument("--repeat", type=int, default=1, help="full rounds; 0 explicitly continuous")
    parser.add_argument("--pause-resume", action="store_true",
                        help="verify PAUSE/CONT quiet state and resumed cycles; requires --repeat 0")
    parser.add_argument("--gui-control", action="store_true",
                        help="execute the actual GUI RJ45 configuration/start builders and batch executor")
    parser.add_argument("--scpi-next", action="store_true",
                        help="supply each READY event with TRIG:SEQ:NEXT instead of an external input")
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--poll", type=float, default=.05)
    parser.add_argument("--quiet", type=float, default=.2)
    parser.add_argument("--source-hz", type=float, default=50, help="user-reported, not measured")
    parser.add_argument("--gateway-pulse-us", type=int, default=1000)
    parser.add_argument("--gateway-timeout-ms", type=int, default=5000)
    parser.add_argument("--dut-slot", type=int, default=2)
    parser.add_argument("--dut-instance", type=int, default=5)
    parser.add_argument("--vna-slot", type=int, default=3)
    parser.add_argument("--vna-instance", type=int, default=7)
    parser.add_argument("--operating-level", type=int, default=7)
    parser.add_argument("--node-count", type=int, default=2)
    parser.add_argument("--local-slot", type=int, default=0)
    parser.add_argument("--reference-slot", type=int, default=0)
    parser.add_argument("--probe-phase-cycles", type=int, default=10)
    parser.add_argument("--train-cycles", type=int, default=4096)
    parser.add_argument("--arm-wait", type=float, default=3)
    parser.add_argument("--start-wait", type=float, default=2)
    args = parser.parse_args(argv)
    if args.pause_resume and args.repeat != 0:
        parser.error("--pause-resume requires --repeat 0 (continuous mode)")
    if args.gui_control and (args.dut_slot, args.vna_slot, args.dut_instance, args.vna_instance,
            args.operating_level, args.node_count, args.local_slot, args.reference_slot,
            args.probe_phase_cycles, args.train_cycles) != (2, 3, 5, 7, 7, 2, 0, 0, 10, 4096):
        parser.error("GUI control uses fixed role slots 2/3, instances 5/7 and the GUI topology/training defaults")
    if any(not math.isfinite(v) or v <= 0 for v in
           (args.duration, args.timeout, args.poll, args.quiet, args.source_hz, args.arm_wait, args.start_wait)):
        parser.error("times and source-hz must be finite and positive")
    if not 0 <= args.repeat <= 0xffffffff // 8 or args.minimum_events < 9 or not 0 < args.gateway_pulse_us <= 0xffffffff // 10 or \
            not 0 < args.gateway_timeout_ms <= 0x7fffffff:
        parser.error("invalid event count or gateway timing")
    return args


def main(argv=None):
    args = parse_args(argv)
    report = {"passed": False, "scope": "single_board_rj45_dut_vna_functional_cycle",
              "functional_cycle_verified": False, "tdma_stability_verified": False,
              "known_down_running_optimization_deferred": True, "multi_board_verified": False,
              "independent_input_count_verified": False, "waveform_verified": False,
              "rf_path_verified": False, "p3_receipt": False, "deferred_observations": [],
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
                    physical = stack.enter_context(open_serial_port(args.port, 115200, args.timeout, .1,
                                                                    read_timeout_s=.02))
                else:
                    instrument = stack.enter_context(open_visa_resource(args.visa_resource, args.timeout))
                    physical = VisaPort(instrument, args.timeout)
                port = ring.EvidencePort(physical, report["transport_transcript"])
                bench = Bench(physical, args, report, lambda cmd, timeout: ring.query(port, cmd, timeout))
                bench.identity()  # absolutely no writes/cleanup before identity matches
                try:
                    execute(bench, port, report)
                finally:
                    cleanup(bench, port, report)
                require(not report["cleanup_failures"], "cleanup verification failed")
                report["passed"] = True
        except (Exception, KeyboardInterrupt) as exc:
            report["failure"] = f"{type(exc).__name__}: {exc}"
        finally:
            json.dump(report, evidence, indent=2, ensure_ascii=True)
            evidence.write("\n")
    print(json.dumps({"passed": report["passed"], "failure": report["failure"], "evidence": str(args.out)}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
