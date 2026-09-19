#!/usr/bin/env python3
"""Repeat stopped sequence-role configuration without starting acquisition.

This checks configuration transactions only, not timing, physical transport,
multi-board operation, or P3 acceptance. Every mutation is issued once.
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
    Bench, discover, open_serial_port, open_visa_resource, parse_role, read_io,
    require, select_transport,
)
from tools.hardware_acceptance.sequence_tdma_cycle_validate import VisaPort
from tools.tdma_ring_monitor import tdma_single_board_loopback as ring

ACTIVATION_FIELDS = ("attempt_seq", "result", "registry_error", "evaluated_mask",
    "failed_mask", "unavailable_mask", "staging_crc32", "staging_seq", "quality_state",
    "quality_reason", "reject_count", "overrun_count", "timeout_count", "last_error")
# Snapshot of sequence-role template resource, IO and IP declarations.
ROLES = ((0, 2, "COUNTER", 152, 1, 1), (2, 5, "DUT", 152, 11, 5),
         (3, 7, "VNA", 152, 3, 3))


def command_once(bench, record, command, accepted=None, cleanup=False):
    action = {"command": command}
    record.setdefault("actions", []).append(action)
    try:
        action["error_before"] = bench.command("SYST:ERR?")
        pending = not action["error_before"].lstrip().startswith("0,")
        require(not pending or cleanup, f"pending SCPI error before {command}")
        action["response"] = bench.command(command)
        action["error_after"] = bench.command("SYST:ERR?")
        require(not pending, f"pending SCPI error before cleanup {command}")
        require(action["error_after"].lstrip().startswith("0,"), f"SCPI error from {command}")
        if accepted is not None:
            fields = next(csv.reader([action["response"]]))
            require(bool(fields) and fields[0] == accepted, f"{command} rejected: {action['response']}")
        return action["response"]
    except (Exception, KeyboardInterrupt) as exc:
        action["failure"] = f"{type(exc).__name__}: {exc}"
        if "error_after" not in action:
            try:
                action["error_after_failure"] = bench.command("SYST:ERR?")
            except (Exception, KeyboardInterrupt) as error_exc:
                action["error_read_failure"] = f"{type(error_exc).__name__}: {error_exc}"
        raise


def read_activation(bench, record, key):
    diagnostic = record[key] = {}
    diagnostic["raw"] = command_once(bench, record, "SYST:REFMEM:LOAD:ACT:STAT?")
    fields = diagnostic["raw"].split(",")
    require(len(fields) == len(ACTIVATION_FIELDS) and
            all(field.isascii() and field.isdecimal() for field in fields),
            "malformed activation diagnostic")
    values = list(map(int, fields))
    require(all(value <= 0xffffffff for value in values), "activation diagnostic overflow")
    diagnostic.update(zip(ACTIVATION_FIELDS, values))
    require(diagnostic["result"] <= 11 and diagnostic["quality_state"] <= 3,
            "unsupported activation diagnostic result")
    return diagnostic


def wait_ring_stopped(bench, port, record):
    samples = record.setdefault("ring_stop_samples", [])
    deadline = time.monotonic() + bench.args.timeout
    while True:
        snapshot = {"error_before": bench.command("SYST:ERR?")}
        samples.append(snapshot)
        require(snapshot["error_before"].lstrip().startswith("0,"), "SCPI error before stopped ring readback")
        try:
            snapshot["raw"] = ring.sample(port, bench.args.timeout)
        except (Exception, KeyboardInterrupt) as exc:
            snapshot["failure"] = f"{type(exc).__name__}: {exc}"
            try:
                snapshot["error_after_failure"] = bench.command("SYST:ERR?")
            except (Exception, KeyboardInterrupt) as error_exc:
                snapshot["error_read_failure"] = f"{type(error_exc).__name__}: {error_exc}"
            raise
        snapshot["error_after"] = bench.command("SYST:ERR?")
        require(snapshot["error_after"].lstrip().startswith("0,"), "SCPI error from stopped ring readback")
        if all(ring.field(snapshot["raw"], index) == 0 for index in
               (ring.RING_ENABLED, ring.RING_ADAPTER_STARTED)):
            return
        require(time.monotonic() < deadline, "ring did not stop before configuration")
        time.sleep(bench.args.poll)


def stop_sequence(bench, record, cleanup=False):
    command_once(bench, record, "TRIG:STOP", "1", cleanup)
    record["owner"] = bench.wait_state("IDLE")
    record["io"] = io = read_io(bench)
    require(all(io[key] == 0 for key in ("outputs", "owned", "armed", "busy")),
            "stopped sequence did not release IO")


def stop_ring(bench, port, record, cleanup=False):
    command_once(bench, record, "SYST:TDMA:RING:STOP", "OK", cleanup)
    wait_ring_stopped(bench, port, record)


def role_readbacks(bench, attempt, key, active):
    rows = attempt[key] = {}
    for slot, instance, name, resource, io, ip in ROLES:
        entry = rows[name] = {"slot": slot, "instance": instance}
        entry["raw"] = command_once(bench, attempt, f"READ:SEQ:NODE:ROLE? {instance}")
        entry["parsed"] = parsed = parse_role(entry["raw"], instance, name)
        prefix = "active" if active else "staged"
        require([parsed[f"{prefix}_{field}"] for field in ("enabled", "resource", "io", "ip")] ==
                [1, resource, io, ip], f"{name} {prefix} declaration mismatch")
        if active:
            require(all(parsed[f"active_{field}"] == parsed[f"staged_{field}"] for field in
                        ("enabled", "resource", "io", "ip")), f"{name} active/staged mismatch")


def run_attempts(bench, port, report):
    report["attempts"] = []
    for ordinal in range(1, bench.args.attempts + 1):
        attempt = {"ordinal": ordinal, "passed": False, "actions": []}
        report["attempts"].append(attempt)
        try:
            stop_sequence(bench, attempt)
            stop_ring(bench, port, attempt)
            command_once(bench, attempt, "CONF:SEQ:LINK OFF", "1")
            for slot, instance, name, *_ in ROLES:
                command_once(bench, attempt, f"CONF:SEQ:NODE:ROLE {slot},{instance},{name}", "STAGED")
            role_readbacks(bench, attempt, "staged_roles", False)
            before = read_activation(bench, attempt, "activation_before")
            attempt["activation_response"] = command_once(bench, attempt, "CONF:SEQ:NODE:ACT", "ACTIVE")
            after = read_activation(bench, attempt, "activation_after")
            require(after["attempt_seq"] == (before["attempt_seq"] + 1) & 0xffffffff,
                    "activation diagnostic is not from this attempt")
            require(after["result"] == after["registry_error"] == after["failed_mask"] ==
                    after["unavailable_mask"] == 0 and after["quality_state"] == 2 and
                    after["evaluated_mask"] == 0xff,
                    "activation diagnostics did not confirm all gates passed")
            role_readbacks(bench, attempt, "active_roles", True)
            attempt["final_io"] = io = read_io(bench)
            require(all(io[key] == 0 for key in ("outputs", "owned", "armed", "busy")),
                    "configuration acquired or drove IO")
            attempt["passed"] = True
        except (Exception, KeyboardInterrupt) as exc:
            attempt["failure"] = f"{type(exc).__name__}: {exc}"
            try:
                read_activation(bench, attempt, "activation_at_failure")
            except (Exception, KeyboardInterrupt) as diagnostic_exc:
                attempt["diagnostic_failure"] = f"{type(diagnostic_exc).__name__}: {diagnostic_exc}"
            raise
        print(json.dumps({"configuration_attempt": ordinal, "passed": True}), flush=True)


def cleanup(bench, port, report):
    record = report["cleanup"] = {}
    for name, operation in (("sequence stop", lambda: stop_sequence(bench, record, True)),
                            ("ring stop", lambda: stop_ring(bench, port, record, True))):
        try:
            operation()
        except (Exception, KeyboardInterrupt) as exc:
            report["cleanup_failures"].append(f"{name}: {type(exc).__name__}: {exc}")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--attempts", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--poll", type=float, default=.05)
    args = parser.parse_args(argv)
    if not 1 <= args.attempts <= 100:
        parser.error("attempts must be between 1 and 100")
    if any(not math.isfinite(value) or value <= 0 for value in (args.timeout, args.poll)):
        parser.error("timeout and poll must be finite and positive")
    return args


def main(argv=None):
    args = parse_args(argv)
    report = {"passed": False, "scope": "single_board_idle_sequence_role_configuration",
        "started_at": datetime.now(timezone.utc).isoformat(), "p3_receipt": False,
        "active_rejection_verified": False, "physical_loop_verified": False,
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
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
                    run_attempts(bench, port, report)
                    bench.identity()
                finally:
                    cleanup(bench, port, report)
                require(not report["cleanup_failures"], "configuration cleanup failed")
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
