#!/usr/bin/env python3
"""Validate independent SP8T finite rounds using MANUAL or a continuing IN1 source.

Each round contains eight states including START's first state: N rounds admit
exactly 8*N-1 advances. OUT1..3 carry the code and OUT4 carries status pulses.
The report verifies firmware admission/completion and idle pads, not independently
counted source edges, pulse width, RF switching, multi-board timing, or P3.
"""
from __future__ import annotations

import argparse
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
    Bench, discover, open_serial_port, open_visa_resource, read_io, require, select_transport,
)
from tools.hardware_acceptance.sequence_tdma_cycle_validate import VisaPort
from tools.tdma_ring_monitor import tdma_single_board_loopback as ring


def parse_repeat(response):
    parts = response.split(",")
    require(len(parts) == 3 and all(v.isascii() and v.isdecimal() for v in parts),
            "malformed repeat response")
    values = list(map(int, parts))
    require(values[0] <= 0xffffffff and values[1] <= 0xffffffff and values[2] <= 1,
            "repeat readback outside range")
    return dict(zip(("configured", "active", "finished"), values))


def check_status(row, previous, maximum=None):
    require(row["state"] in ("STARTING", "READY", "BUSY", "STOPPING", "IDLE"),
            "unexpected independent sequence state")
    require(row["count"] == 8 and row["error"] == "NONE" and
            row["faults"] == row["backend_fault"] == row["cancelled"] == 0,
            "independent sequence fault, cancellation, or wrong plan")
    require(row["completed"] <= row["accepted"] <= row["completed"] + 1,
            "admission/completion accounting mismatch")
    if maximum is not None:
        require(row["accepted"] <= maximum, "finite sequence exceeded its hardware quota")
    if previous is not None:
        require((row["run_id"], row["generation"]) ==
                (previous["run_id"], previous["generation"]), "sequence run changed")
        require(row["accepted"] >= previous["accepted"] and row["completed"] >= previous["completed"],
                "sequence counters regressed")
    require(row["current_index"] == row["current_state"] == row["accepted"] % 8,
            "selected SP8T state disagrees with admitted steps")
    if row["completed"]:
        require(row["completed_index"] == row["completed_state"] == row["completed"] % 8,
                "completed SP8T state mismatch")


def stop_ring(port, args, report):
    report["ring_stop_action"] = ring.checked_action(port, "SYSTem:TDMA:RING:STOP", args.timeout)
    deadline = time.monotonic() + args.timeout
    samples = report.setdefault("ring_stop_samples", [])
    while True:
        observation = ring.sample(port, args.timeout)
        samples.append(observation)
        if all(ring.field(observation, index) == 0 for index in
               (ring.RING_ADAPTER_STARTED, ring.RING_UP_RUNNING, ring.RING_DOWN_RUNNING)):
            return
        require(time.monotonic() < deadline, "TDMA did not completely stop")
        time.sleep(args.poll)


def verify_quiet(bench, report, before=None):
    first = bench.status()
    first_io = read_io(bench)
    report["idle_before_quiet"] = {"sequence": first, "io": first_io}
    require(first["state"] == "IDLE", "sequence not IDLE before quiet observation")
    require(all(first_io[k] == 0 for k in ("outputs", "owned", "armed", "busy")),
            "IDLE did not release outputs to zero")
    keys = ("run_id", "generation", "accepted", "completed", "cancelled", "faults", "backend_fault")
    if before is not None:
        require(all(first[k] == before[k] for k in keys), "finite result changed after completion")
    time.sleep(bench.args.quiet)
    after, after_io = bench.status(), read_io(bench)
    report["idle_after_quiet"] = {"sequence": after, "io": after_io}
    require(after["state"] == "IDLE" and all(after[k] == first[k] for k in keys),
            "continuing source advanced sequence after stop")
    require(all(after_io[k] == 0 for k in ("outputs", "owned", "armed", "busy")),
            "outputs changed while idle")


def execute(bench, port, report):
    args = bench.args
    bench.write("TRIG:STOP")
    bench.wait_state("IDLE")
    stop_ring(port, args, report)
    bench.write("CONF:SEQ:LINK OFF")
    bench.configure()
    bench.write(f"CONF:SEQ:REP {args.repeat}")
    report["configured_repeat"] = parse_repeat(bench.command("READ:SEQ:REP?"))
    require(report["configured_repeat"]["configured"] == args.repeat, "repeat configuration mismatch")
    if args.configure_only:
        report["configuration_verified"] = True
        return

    old = bench.status()
    bench.write("TRIG:START")
    target = 8 * args.repeat - 1 if args.repeat else None
    previous = None
    sent = 0
    report["samples"] = []
    deadline = time.monotonic() + args.duration
    while time.monotonic() < deadline:
        row = bench.status()
        sample = {"sequence": row}
        report["samples"].append(sample)
        # The START mailbox may not yet have published a new run.
        if previous is None and (row["run_id"], row["generation"]) == (old["run_id"], old["generation"]):
            require(row["state"] != "FAULT", "START fault")
            time.sleep(args.poll)
            continue
        check_status(row, previous, target)
        if previous is None:
            report["first_observation"] = row
            report["first_state_observed"] = row["accepted"] == 0 and row["current_index"] == 0
            if args.source == "MANUAL":
                require(report["first_state_observed"], "MANUAL START did not expose the first state")
        previous = row
        if args.repeat and row["state"] == "IDLE":
            result = parse_repeat(bench.command("READ:SEQ:REP?"))
            report["repeat_result"] = result
            require(result == {"configured": args.repeat, "active": args.repeat, "finished": 1},
                    "finite sequence stopped without a finished receipt")
            require(row["accepted"] == row["completed"] == target and row["current_index"] == 7,
                    "finite sequence stopped at the wrong step")
            verify_quiet(bench, report, row)
            report["functional_execution_verified"] = True
            break
        require(row["state"] != "IDLE", "continuous run stopped unexpectedly")
        if not args.repeat and row["completed"] >= args.minimum_events:
            report["continuous_observation"] = row
            bench.write("TRIG:STOP")
            bench.wait_state("IDLE")
            verify_quiet(bench, report)
            report["functional_execution_verified"] = True
            break
        if args.source == "MANUAL" and row["state"] == "READY":
            require(row["accepted"] == row["completed"] == sent,
                    "MANUAL step advanced without a NEXT command")
            if target is None or sent < target:
                bench.write("TRIG:SEQ:NEXT")
                sent += 1
        time.sleep(args.poll)
    require(report.get("functional_execution_verified", False), "sequence repeat verification timed out")
    report["software_next_sent"] = sent
    require(bench.command("SYST:ERR?").startswith("0,"), "SCPI error after repeat test")
    bench.identity()


def cleanup(bench, port, report):
    try:
        bench.write("TRIG:STOP")
        bench.wait_state("IDLE")
        verify_quiet(bench, report.setdefault("cleanup", {}))
    except (Exception, KeyboardInterrupt) as exc:
        report["cleanup_failures"].append(f"sequence: {type(exc).__name__}: {exc}")
    try:
        stop_ring(port, bench.args, report.setdefault("cleanup", {}))
    except (Exception, KeyboardInterrupt) as exc:
        report["cleanup_failures"].append(f"TDMA: {type(exc).__name__}: {exc}")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--source", choices=("IN1", "MANUAL"), default="IN1")
    parser.add_argument("--repeat", type=int, default=1, help="complete rounds; 0 explicitly continuous")
    parser.add_argument("--configure-only", action="store_true", help="verify configuration without START")
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--minimum-events", type=int, default=9)
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--poll", type=float, default=.01)
    parser.add_argument("--quiet", type=float, default=.3)
    parser.add_argument("--settle-us", type=int, default=10)
    parser.add_argument("--pulse-us", type=int, default=10)
    parser.add_argument("--edge", choices=("RIS", "FALL"), default="RIS")
    parser.add_argument("--source-hz", type=float, default=50, help="user-reported, not measured")
    args = parser.parse_args(argv)
    if any(not math.isfinite(v) or v <= 0 for v in (args.duration, args.timeout, args.poll, args.quiet, args.source_hz)):
        parser.error("times and source-hz must be finite and positive")
    if not 0 <= args.repeat <= 0xffffffff // 8 or args.minimum_events < 9:
        parser.error("repeat must fit the firmware step counter; minimum-events must be >=9")
    if not 0 <= args.settle_us <= 0xffffffff // 10 or not 0 < args.pulse_us <= 0xffffffff // 10:
        parser.error("invalid timing range")
    return args


def main(argv=None):
    args = parse_args(argv)
    report = {"passed": False, "scope": "single_board_independent_sp8t_repeat",
              "functional_execution_verified": False, "configuration_verified": False,
              "software_input_simulation_verified": args.source == "MANUAL",
              "independent_input_count_verified": False, "external_waveform_verified": False,
              "rf_path_verified": False, "p3_receipt": False,
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
                bench.identity()
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
