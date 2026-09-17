#!/usr/bin/env python3
"""Observe independent START status on one board, optionally without IO wiring.

Stretched settle/pulse intervals permit SCPI pad sampling. This verifies sampled
levels and firmware accounting, not physical pulse widths or independent counts.
All configuration and mutation responses are retained, including failed runs.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import csv
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
    Bench, TIME_MAX_US, check_loopback, discover, open_serial_port,
    open_visa_resource, read_io, require, select_transport,
)
from tools.hardware_acceptance.sequence_repeat_validate import cleanup, parse_repeat, stop_ring, verify_quiet
from tools.hardware_acceptance.sequence_tdma_cycle_validate import VisaPort
from tools.tdma_ring_monitor import tdma_single_board_loopback as ring

PLAN = (5, 2, 7, 0, 1, 3, 4, 6)


def configure(bench, phase, mode, singleton=False, abort=False):
    bench.write("TRIG:STOP")
    bench.wait_state("IDLE")
    require(bench.command("SYST:ERR?").startswith("0,"), "pre-existing SCPI error")
    plan = (PLAN[0],) if singleton else PLAN
    pulse_us = bench.args.abort_pulse_us if abort else bench.args.pulse_us
    pulse_us = pulse_us if mode == "PULSE" else 0
    mask = 0 if mode == "NONE" else 8
    repeat = 1 if singleton else 0
    for command in (
        # A singleton plan selects state 5 once without shrinking SP8T state space.
        "CONF:TRIG 8,0,1,1",
        "CONF:SEQ SP8T," + ",".join(map(str, plan)), "CONF:SEQ:ACT SP8T",
        f"CONF:SEQ:OUTPUT 7,{mask},{mode},{bench.args.settle_us},{pulse_us}",
        *(f"CONF:SEQ:CODE {code},{code}" for code in PLAN),
        "CONF:SEQ:SOUR MANUAL,RIS", f"CONF:SEQ:REP {repeat}",
    ):
        bench.write(command)
    actual_plan = next(csv.reader([bench.command("READ:SEQ? SP8T")]))
    require(len(actual_plan) == 5 + len(plan) and actual_plan[:2] == ["SP8T", "4294967295"]
            and actual_plan[2].isascii() and actual_plan[2].isdecimal()
            and actual_plan[3:5] == [str(len(plan)), "0"]
            and actual_plan[5:] == list(map(str, plan)), "plan readback mismatch")
    output = next(csv.reader([bench.command("READ:SEQ:OUTPUT?")]))
    require(len(output) == 7 and output[5].isascii() and output[5].isdecimal()
            and output[:5] == ["7", str(mask), mode, str(bench.args.settle_us), str(pulse_us)]
            and output[-1] == "1", "output configuration readback mismatch")
    for code in PLAN:
        require(bench.command(f"READ:SEQ:CODE? {code}") == f"{code},{code}", "code readback mismatch")
    source = next(csv.reader([bench.command("READ:SEQ:SOUR?")]))
    require(source == ["MANUAL", "RISING"], "manual source readback mismatch")
    repeats = parse_repeat(bench.command("READ:SEQ:REP?"))
    require(repeats["configured"] == repeat, "repeat configuration mismatch")
    phase["configuration"] = {"plan": actual_plan, "output": output, "source": source, "repeat": repeats}
    return plan


def check_result(row, plan, advances, baseline=None):
    require(row["error"] == "NONE" and all(row[key] == 0 for key in
            ("faults", "backend_fault", "cancelled")), "runtime fault or cancellation")
    require(row["accepted"] == row["completed"] == advances,
            "START or feedback changed advancement counters")
    index = advances % len(plan)
    require(row["count"] == len(plan) and row["current_index"] == index and
            row["current_state"] == plan[index], "wrong sequence position")
    if advances:
        require(row["completed_index"] == index and row["completed_state"] == plan[index],
                "wrong completed position")
    if baseline is not None:
        require((row["run_id"], row["generation"]) ==
                (baseline["run_id"], baseline["generation"]), "run changed unexpectedly")


def sample_level(bench, phase, name, code, high, owned):
    samples = phase.setdefault(name, [])
    deadline = time.monotonic() + bench.args.timeout
    mismatch_deadline = None
    while time.monotonic() < deadline:
        io = read_io(bench)
        samples.append(io)
        require(time.monotonic() < deadline, f"{name}: pad observation exceeded deadline")
        if io["owned"] == owned and io["outputs"] == code | (8 if high else 0):
            if not bench.args.output_only:
                # Inputs precede outputs in the SCPI read and can straddle an
                # edge. Preserve that sample; require agreement within 100 ms.
                if bool(io["inputs"] & 2) != high:
                    if mismatch_deadline is None:
                        mismatch_deadline = time.monotonic() + .1
                    require(time.monotonic() < mismatch_deadline,
                            "OUT4->IN2 cable did not follow output level")
                    time.sleep(bench.args.poll)
                    continue
                check_loopback(io, code, high)
            require(mismatch_deadline is None or time.monotonic() < mismatch_deadline,
                    "OUT4->IN2 cable confirmation exceeded observation window")
            return io
        require(mismatch_deadline is None or time.monotonic() < mismatch_deadline,
                "OUT4->IN2 cable did not follow output level")
        time.sleep(bench.args.poll)
    raise RuntimeError(f"{name}: expected pad level not sampled; inspect timing/wiring")


def wait_result(bench, phase, name, old, state, advances):
    samples = phase.setdefault(name, [])
    deadline = time.monotonic() + bench.args.timeout
    while time.monotonic() < deadline:
        row = bench.status()
        samples.append(row)
        require(row["state"] != "FAULT", "runtime fault")
        # A START mailbox can briefly return the previous run's IDLE/READY.
        if ((row["run_id"], row["generation"]) != (old["run_id"], old["generation"])
                and row["state"] == state and row["completed"] == advances):
            return row
        time.sleep(bench.args.poll)
    raise RuntimeError(f"{name}: new run did not reach {state} with {advances} advances")


def run_phase(bench, phase, mode, singleton=False, abort=False):
    plan = configure(bench, phase, mode, singleton, abort)
    old = bench.status()
    bench.write("TRIG:START")
    owned = 7 if mode == "NONE" else 15
    # A nonzero first code distinguishes armed settling from stopped outputs.
    sample_level(bench, phase, "settling_low", plan[0], False, owned)
    if mode != "NONE":
        sample_level(bench, phase, "startup_high", plan[0], True, owned)
    if abort:
        phase["before_stop"] = bench.status()
        check_result(phase["before_stop"], plan, 0)
        # Capture another high immediately before issuing STOP; this is sampled
        # evidence of an interrupted startup, not a precise STOP latency claim.
        sample_level(bench, phase, "high_before_stop", plan[0], True, owned)
        bench.write("TRIG:STOP")
        row = bench.wait_state("IDLE")
        phase["stopped"] = row
        check_result(row, plan, 0, phase["before_stop"])
        verify_quiet(bench, phase.setdefault("quiet", {}), row)
    else:
        row = wait_result(bench, phase, "startup_result", old, "IDLE" if singleton else "READY", 0)
        check_result(row, plan, 0)
        phase["repeat_result"] = parse_repeat(bench.command("READ:SEQ:REP?"))
        require(phase["repeat_result"] == {"configured": int(singleton), "active": int(singleton),
                                          "finished": int(singleton)}, "startup repeat accounting mismatch")
        if singleton:
            verify_quiet(bench, phase.setdefault("quiet", {}), row)
        else:
            sample_level(bench, phase, "startup_settled", plan[0], mode == "LEVEL", owned)
            time.sleep(bench.args.quiet)
            quiet = bench.status()
            phase["startup_quiet"] = quiet
            check_result(quiet, plan, 0, row)
            require(quiet["state"] == "READY", "startup did not stay READY")
            bench.write("TRIG:SEQ:NEXT")
            if mode == "PULSE":
                sample_level(bench, phase, "next_high", plan[1], True, owned)
            advanced = wait_result(bench, phase, "next_result", old, "READY", 1)
            check_result(advanced, plan, 1, row)
            sample_level(bench, phase, "next_settled", plan[1], mode == "LEVEL", owned)
            time.sleep(bench.args.quiet)
            after = bench.status()
            phase["next_quiet"] = after
            check_result(after, plan, 1, row)
            require(after["state"] == "READY", "NEXT did not stay READY")
    require(bench.command("SYST:ERR?").startswith("0,"), "SCPI error after START phase")
    phase["passed"] = True


def execute(bench, port, report):
    bench.write("TRIG:STOP")
    bench.wait_state("IDLE")
    stop_ring(port, bench.args, report)
    bench.write("CONF:SEQ:LINK OFF")
    for name, mode, singleton, abort in (
        ("pulse", "PULSE", False, False), ("level", "LEVEL", False, False),
        ("none", "NONE", False, False), ("pulse_reload", "PULSE", False, False),
        ("singleton", "PULSE", True, False), ("stop_startup", "PULSE", False, True),
        ("restart_after_stop", "PULSE", False, False),
    ):
        phase = {"passed": False, "failure": None}
        report["phases"][name] = phase
        print(f"Running {name}", flush=True)
        try:
            run_phase(bench, phase, mode, singleton, abort)
        except (Exception, KeyboardInterrupt) as exc:
            phase["failure"] = f"{type(exc).__name__}: {exc}"
            raise
    bench.identity()
    report["output_pad_sequence_verified"] = True
    report["io_loopback_verified"] = not bench.args.output_only


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--output-only", action="store_true",
                        help="sample output pads only; do not require or certify OUT4-to-IN2 wiring")
    parser.add_argument("--settle-us", type=int, default=300000)
    parser.add_argument("--pulse-us", type=int, default=300000)
    parser.add_argument("--abort-pulse-us", type=int, default=1500000)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--poll", type=float, default=.01)
    parser.add_argument("--quiet", type=float, default=.1)
    args = parser.parse_args(argv)
    if any(not math.isfinite(v) or v <= 0 for v in (args.timeout, args.poll, args.quiet)):
        parser.error("times must be finite and positive")
    if any(not 0 < v <= TIME_MAX_US for v in (args.settle_us, args.pulse_us, args.abort_pulse_us)):
        parser.error("settle and pulse times must be positive and fit firmware timing range")
    if args.timeout <= (args.settle_us + max(args.pulse_us, args.abort_pulse_us)) / 1e6:
        parser.error("timeout must exceed a full startup")
    return args


def main(argv=None):
    args = parse_args(argv)
    report = {"passed": False, "scope": "single_board_start_status_samples",
              "output_pad_sequence_verified": False, "io_loopback_verified": False,
              "external_waveform_verified": False, "independent_pulse_count_verified": False,
              "rf_path_verified": False, "p3_receipt": False,
              "started_at": datetime.now(timezone.utc).isoformat(),
              "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "settings": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
              "phases": {}, "transcript": [], "transport_transcript": [],
              "failure": None, "cleanup_failures": []}
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
