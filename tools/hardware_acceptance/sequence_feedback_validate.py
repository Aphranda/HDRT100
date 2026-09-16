#!/usr/bin/env python3
"""Reproducible single-board SP8T/IN1 and OUT4->IN2 bench checks.

The MANUAL phase stretches OUT4 pulses for pad readback and covers all SP8T codes.
The continuous phase observes an independent generator, then pauses to reconcile
counters. It does not invent a sent-pulse count or certify VNA role execution,
pulse timing, RF switching, or lossless input capture. Every run saves raw SCPI.
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

from tools.hardware_acceptance.sequence_trigger_acceptance import (
    Bench, TIME_MAX_US, open_serial_port, open_visa_resource, parse_timing,
    require, validate_sample, visa_command,
)


def discover() -> dict:
    from serial.tools.list_ports import comports
    result = {"serial": [{"port": p.device, "serial_number": p.serial_number,
                          "vid": p.vid, "pid": p.pid, "description": p.description}
                         for p in comports()], "visa": [], "visa_error": None}
    try:
        import pyvisa
        manager = pyvisa.ResourceManager()
        try:
            result["visa"] = [r for r in manager.list_resources() if r.startswith("USB")]
        finally:
            manager.close()
    except Exception as exc:
        result["visa_error"] = str(exc)
    return result


def select_transport(args, devices: dict) -> None:
    if args.port or args.visa_resource:
        return
    matches = [p["port"] for p in devices["serial"]
               if p["serial_number"] == args.serial_number and p["vid"] == 0xCAFE]
    if len(matches) == 1:
        args.port = matches[0]
        return
    matches = [r for r in devices["visa"] if args.serial_number in r.split("::")]
    require(len(matches) == 1, "no unique USB device matches requested serial number")
    args.visa_resource = matches[0]


def parse_io(response: str) -> dict:
    parts = response.split(",")
    require(len(parts) == 5 and all(p.isascii() and p.isdecimal() for p in parts),
            "malformed IO snapshot")
    values = list(map(int, parts))
    require(all(v <= 15 for v in values[:3]) and all(v <= 1 for v in values[3:]),
            "IO snapshot outside logical pin range")
    return dict(zip(("inputs", "outputs", "owned", "armed", "busy"), values))


def check_loopback(io: dict, code: int, high: bool) -> None:
    require(io["outputs"] == code | (8 if high else 0), "wrong SP8T/OUT4 pad levels")
    require(bool(io["inputs"] & 2) == high, "OUT4->IN2 cable did not follow output level")


def check_settled(row: dict, baseline: dict, minimum: int) -> None:
    require(row["state"] == "PAUSED", "counters not paused")
    require((row["run_id"], row["generation"]) ==
            (baseline["run_id"], baseline["generation"]), "run changed during observation")
    require(row["count"] == 8 and row["accepted"] == row["completed"],
            "unfinished step or wrong plan")
    require(row["completed"] - baseline["completed"] >= minimum,
            "insufficient IN1 events; check generator output, frequency and wiring")
    count = row["completed"]
    require(row["current_index"] == row["current_state"] == count % 8 and
            row["next_index"] == (count + 1) % 8 and row["cycles"] == count // 8,
            "sequence cursor or wrap accounting mismatch")
    if count:
        require(row["completed_index"] == row["completed_state"] == count % 8,
                "completed cursor mismatch")
    require(row["error"] == "NONE" and all(row[k] == 0 for k in
            ("faults", "backend_fault", "cancelled")), "fault or cancellation")


def read_io(bench: Bench) -> dict:
    return parse_io(bench.command("READ:IO:STAT?"))


def check_rejections(response: str, paused: dict) -> list[int]:
    parts = response.split(",")
    require(len(parts) == 5 and all(p.isascii() and p.isdecimal() for p in parts),
            "malformed rejection snapshot")
    values = list(map(int, parts))
    require(all(v <= 0xffffffff for v in values), "rejection counter overflow")
    require(values[:3] == [paused[k] for k in ("run_id", "generation", "busy_rejected")]
            and values[3] >= paused["notready_rejected"] and values[4] == 0,
            "rejections pending, regressed or belong to another run")
    # A continuous source still produces rejected edges while PAUSED. Separate
    # SCPI reads need not have identical notready_rejected counters.
    return values


def wait_running(bench: Bench, previous: dict | None = None) -> dict:
    deadline = time.monotonic() + bench.args.timeout
    while True:
        row = bench.status()
        if previous is not None:
            require((row["run_id"], row["generation"]) ==
                    (previous["run_id"], previous["generation"]), "run changed on resume")
        if row["state"] in {"READY", "BUSY"}:
            return row
        require(row["state"] == ("PAUSED" if previous is not None else "STARTING")
                and time.monotonic() < deadline, "sequence did not enter running state")
        time.sleep(bench.args.poll)


def check_stop(stopped: dict, before: dict | None) -> None:
    require(stopped["state"] == "IDLE" and stopped["error"] == "NONE" and
            stopped["faults"] == stopped["backend_fault"] == 0,
            "STOP ended with a runtime fault")
    if before is not None:
        for key in ("run_id", "generation", "accepted", "completed", "busy_rejected", "cancelled"):
            require(stopped[key] == before[key], f"STOP changed {key} after settled phase")


def manual_loopback(bench: Bench, phase: dict) -> None:
    bench.args.source = "MANUAL"
    bench.args.settle_us = 10
    bench.args.pulse_us = bench.args.loopback_pulse_us
    bench.configure()
    bench.write("TRIG:START")
    baseline = bench.wait_state("READY")
    require(baseline["accepted"] == baseline["completed"] == 0 and
            baseline["current_index"] == 0 and baseline["next_index"] == 1,
            "START did not prime first state")
    check_loopback(read_io(bench), 0, False)
    phase["timing"] = parse_timing(bench.command("READ:SEQ:TIM?"))
    phase["steps"] = []
    for count in range(1, 10):
        bench.write("TRIG:SEQ:NEXT")
        code = count % 8
        deadline = time.monotonic() + bench.args.timeout
        high = None
        while time.monotonic() < deadline:
            io = read_io(bench)
            if io["outputs"] & 8:
                check_loopback(io, code, True)
                high = io
                break
            time.sleep(bench.args.poll)
        require(high is not None, "OUT4 high not sampled; increase --loopback-pulse-us")
        row = bench.wait_state("READY")
        require(validate_sample(row, baseline, count - 1) == count, "NEXT did not advance once")
        low = read_io(bench)
        check_loopback(low, code, False)
        time.sleep(bench.args.quiet)
        require(bench.status() == row, "OUT4/IN2 or DONE caused unsolicited advance")
        phase["steps"].append({"count": count, "status": row, "high": high, "low": low})
    phase["passed"] = True


def dut_only(bench: Bench, phase: dict) -> None:
    """Check NONE on real pads; no claim about unsampled transient pulses."""
    bench.args.source = "MANUAL"
    bench.args.settle_us = 10
    bench.args.pulse_us = bench.args.loopback_pulse_us
    bench.configure()
    bench.write("CONF:SEQ:OUTPUT 7,0,NONE,10,0")
    config = next(csv.reader([bench.command("READ:SEQ:OUTPUT?")]))
    require(config[:5] == ["7", "0", "NONE", "10", "0"] and config[-1] == "1",
            "DUT-only output configuration not active")
    phase["configuration"] = config
    bench.write("TRIG:START")
    baseline = bench.wait_state("READY")
    require(baseline["accepted"] == baseline["completed"] == 0 and
            baseline["current_index"] == 0 and baseline["next_index"] == 1,
            "START did not prime first DUT state")
    initial = read_io(bench)
    require(initial["owned"] == 7, "DUT-only mode claimed a status output")
    check_loopback(initial, 0, False)
    phase["timing"] = parse_timing(bench.command("READ:SEQ:TIM?"))
    phase["steps"] = []
    for count in range(1, 10):
        bench.write("TRIG:SEQ:NEXT")
        row = bench.wait_state("READY")
        require(validate_sample(row, baseline, count - 1) == count, "DUT NEXT did not advance once")
        io = read_io(bench)
        require(io["owned"] == 7, "DUT step claimed a status output")
        check_loopback(io, count % 8, False)
        time.sleep(bench.args.quiet)
        require(bench.status() == row, "DUT advanced without an input event")
        phase["steps"].append({"status": row, "io": io})
    phase["passed"] = True


def standalone_sp8t(bench: Bench, phase: dict) -> None:
    """Read actual encoded pads for manual positions without starting a plan."""
    bench.write("TRIG:STOP")
    baseline = bench.wait_state("IDLE")
    require(bench.command("SYST:ERR?").startswith("0,"), "pre-existing SCPI error")
    phase["positions"] = []
    # Cover return to position 1; run_phases also resets manual IO on failure.
    for position in (*range(1, 9), 1):
        bench.write(f"CONF:SWITCH1 {position}")
        reply = next(csv.reader([bench.command("READ:SWITCH1?")]))
        require(len(reply) == 5 and reply[:2] == ["1", str(position)],
                "standalone SP8T readback mismatch")
        io = read_io(bench)
        require(io["outputs"] == position - 1 and
                io["owned"] == io["armed"] == io["busy"] == 0,
                "standalone SP8T pad mismatch or sequence resource acquired")
        status = bench.status()
        require(status == baseline, "manual SP8T changed sequence runtime")
        phase["positions"].append({"position": position, "readback": reply,
                                   "io": io, "status": status})
    phase["passed"] = True


def observe(bench: Bench, phase: dict, baseline: dict) -> dict:
    deadline = time.monotonic() + bench.args.duration
    previous = baseline
    phase["samples"] = []
    while time.monotonic() < deadline:
        row = bench.status()
        require(row["state"] in {"READY", "BUSY"}, "unexpected running state")
        require((row["run_id"], row["generation"]) ==
                (baseline["run_id"], baseline["generation"]), "run changed")
        require(row["accepted"] >= previous["accepted"] and
                row["completed"] >= previous["completed"], "counters regressed")
        require(row["error"] == "NONE" and row["faults"] == row["backend_fault"] == 0,
                "runtime fault")
        io = read_io(bench)
        if bench.args.external_status_mode == "NONE":
            require(io["owned"] == 7 and (io["outputs"] & 8) == 0,
                    "external DUT-only run drove or claimed OUT4")
        phase["samples"].append({"status": row, "io": io})
        previous = row
        time.sleep(bench.args.poll)
    bench.write("TRIG:PAUS")
    paused = bench.wait_state("PAUSED")
    phase["paused"] = paused
    phase["rejections"] = check_rejections(bench.command("READ:SEQ:REJ?"), paused)
    check_settled(paused, baseline, bench.args.minimum_events)
    io = read_io(bench)
    phase["paused_io"] = io
    check_loopback(io, paused["completed"] % 8, False)
    time.sleep(bench.args.quiet)
    after = bench.status()
    for field in ("state", "run_id", "generation", "accepted", "completed", "current_index", "next_index"):
        require(after[field] == paused[field], "sequence advanced while paused")
    phase["passed"] = True
    return paused


def continuous(bench: Bench, phase: dict) -> None:
    bench.args.source = "IN1"
    bench.args.settle_us = bench.args.external_settle_us
    bench.args.pulse_us = bench.args.external_pulse_us
    bench.configure()
    if bench.args.external_status_mode == "NONE":
        bench.write(f"CONF:SEQ:OUTPUT 7,0,NONE,{bench.args.external_settle_us},0")
        config = next(csv.reader([bench.command("READ:SEQ:OUTPUT?")]))
        require(config[:5] == ["7", "0", "NONE", str(bench.args.external_settle_us), "0"]
                and config[-1] == "1", "external DUT-only configuration not active")
        phase["configuration"] = config
    bench.write("TRIG:START")
    baseline = wait_running(bench)
    phase["timing"] = parse_timing(bench.command("READ:SEQ:TIM?"))
    phase["before_pause"] = {}
    paused = observe(bench, phase["before_pause"], baseline)
    bench.write("TRIG:CONT")
    wait_running(bench, paused)
    phase["after_resume"] = {}
    observe(bench, phase["after_resume"], paused)
    phase["passed"] = True


def parse_role(response: str, instance_id: int, role: str) -> dict:
    fields = next(csv.reader([response]))
    require(len(fields) == 10 and fields[:2] == [str(instance_id), role], "bad role identity")
    require(all(v.isascii() and v.isdecimal() for v in fields[2:]), "bad role fields")
    values = list(map(int, fields[2:]))
    require(values[0] <= 1 and values[1] <= 1 and all(v <= 0xffffffff for v in values),
            "role fields out of range")
    return dict(zip(("active_enabled", "staged_enabled", "active_resource", "active_io",
                     "active_ip", "staged_resource", "staged_io", "staged_ip"), values))


def role_config(bench: Bench, phase: dict) -> None:
    """Verify real role table transactions; this does not run the VNA gateway."""
    bench.write("TRIG:STOP")
    bench.wait_state("IDLE")
    require(bench.command("SYST:ERR?").startswith("0,"), "pre-existing SCPI error")
    roles = ((bench.args.dut_slot, bench.args.dut_instance, "DUT", 11, 5),
             (bench.args.vna_slot, bench.args.vna_instance, "VNA", 3, 3))
    phase["before"] = {}
    phase["staged"] = {}
    for slot, instance_id, role, io, ip in roles:
        phase["before"][role] = parse_role(bench.command(f"READ:SEQ:NODE:ROLE? {instance_id}"),
                                           instance_id, role)
        reply = bench.command(f"CONF:SEQ:NODE:ROLE {slot},{instance_id},{role}")
        require(next(csv.reader([reply])) == ["STAGED"], "role staging rejected")
    for slot, instance_id, role, io, ip in roles:
        row = parse_role(bench.command(f"READ:SEQ:NODE:ROLE? {instance_id}"), instance_id, role)
        phase["staged"][role] = row
        require(row["staged_enabled"] == 1 and row["staged_resource"] == 152 and
                row["staged_io"] == io and row["staged_ip"] == ip, "wrong staged role declarations")
        for key in ("active_enabled", "active_resource", "active_io", "active_ip"):
            require(row[key] == phase["before"][role][key], "staging changed active role")
    phase["load_status"] = bench.command("READ:SEQ:NODE:LOAD?")
    phase["activation"] = bench.command("CONF:SEQ:NODE:ACT")
    phase["after"] = {}
    for slot, instance_id, role, io, ip in roles:
        phase["after"][role] = parse_role(bench.command(f"READ:SEQ:NODE:ROLE? {instance_id}"),
                                          instance_id, role)
    require(next(csv.reader([phase["activation"]]))[0] == "ACTIVE", "role activation rejected; inspect gates")
    for slot, instance_id, role, io, ip in roles:
        row = phase["after"][role]
        require(row["active_enabled"] == 1 and row["active_resource"] == 152 and
                row["active_io"] == io and row["active_ip"] == ip, "active role differs from staging")
    phase["io"] = read_io(bench)
    require(phase["io"]["owned"] == phase["io"]["outputs"] == 0,
            "configuration unexpectedly acquired or drove IO")
    phase["passed"] = True


def run_phases(bench: Bench, report: dict) -> None:
    phases = {"manual-loopback": manual_loopback, "continuous": continuous, "dut-only": dut_only,
              "role-config": role_config, "standalone-sp8t": standalone_sp8t}
    selected = {key: phases[key] for key in ("manual-loopback", "continuous")} \
        if bench.args.mode == "all" else {bench.args.mode: phases[bench.args.mode]}
    for name, execute in selected.items():
        phase = {"passed": False, "failure": None, "cleanup_failure": None}
        report["phases"][name] = phase
        print(f"Running {name}", flush=True)
        before_stop = None
        try:
            execute(bench, phase)
            require(bench.command("SYST:ERR?").startswith("0,"), "SCPI error after phase")
            before_stop = bench.status()
        except (Exception, KeyboardInterrupt) as exc:
            phase["passed"] = False
            phase["failure"] = str(exc) or type(exc).__name__
        finally:
            # STOP in an already-IDLE sequence does not own/reset manual GPIO.
            # Attempt manual safe output independently, retaining both failures.
            if name == "standalone-sp8t":
                try:
                    bench.write("CONF:SWITCH1 1")
                    phase["manual_cleanup_io"] = read_io(bench)
                    require(phase["manual_cleanup_io"]["outputs"] == 0,
                            "manual SP8T cleanup did not restore code zero")
                except (Exception, KeyboardInterrupt) as exc:
                    phase["passed"] = False
                    phase["cleanup_failure"] = "manual reset: " + (str(exc) or type(exc).__name__)
            try:
                bench.stop()
                phase["stopped_status"] = report["stopped_status"]
                phase["stopped_io"] = report["stopped_io"]
                check_stop(report["stopped_status"], before_stop)
            except (Exception, KeyboardInterrupt) as exc:
                phase["passed"] = False
                detail = str(exc) or type(exc).__name__
                phase["cleanup_failure"] = "; ".join(filter(None, (phase["cleanup_failure"], detail)))
        print(json.dumps({"phase": name, "passed": phase["passed"],
                          "failure": phase["failure"], "cleanup_failure": phase["cleanup_failure"]}), flush=True)
        require(phase["passed"], f"{name}: {phase['failure'] or phase['cleanup_failure']}")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("discover", "manual-loopback", "continuous", "dut-only", "role-config", "standalone-sp8t", "all"),
                        default="all", help="all checks MANUAL loopback and continuous; dut-only requires NONE firmware")
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--build", help="optional expected build; always record actual build")
    parser.add_argument("--dut-slot", type=int, default=2)
    parser.add_argument("--dut-instance", type=int, default=5)
    parser.add_argument("--vna-slot", type=int, default=3)
    parser.add_argument("--vna-instance", type=int, default=7)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=5, help="seconds per external observation, twice")
    parser.add_argument("--minimum-events", type=int, default=9)
    parser.add_argument("--source-hz", type=float, help="user-reported frequency metadata, not measured")
    parser.add_argument("--loopback-pulse-us", type=int, default=300000)
    parser.add_argument("--external-settle-us", type=int, default=10)
    parser.add_argument("--external-pulse-us", type=int, default=10)
    parser.add_argument("--external-status-mode", choices=("PULSE", "NONE"), default="PULSE")
    parser.add_argument("--edge", choices=("RIS", "FALL"), default="RIS")
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--poll", type=float, default=.02)
    parser.add_argument("--quiet", type=float, default=.1)
    args = parser.parse_args(argv)
    if any(not math.isfinite(v) or v <= 0 for v in (args.duration, args.timeout, args.poll, args.quiet)):
        parser.error("observation times must be finite and positive")
    if args.minimum_events < 9:
        parser.error("minimum-events must cover a wrap (>=9)")
    if args.source_hz is not None and (not math.isfinite(args.source_hz) or args.source_hz <= 0):
        parser.error("source-hz must be finite and positive")
    if not (0 <= args.external_settle_us <= TIME_MAX_US and
            0 < args.external_pulse_us <= TIME_MAX_US and 0 < args.loopback_pulse_us <= TIME_MAX_US):
        parser.error("invalid firmware timing range")
    if args.timeout <= max(args.loopback_pulse_us + 10,
                           args.external_settle_us + args.external_pulse_us) / 1e6:
        parser.error("timeout must exceed a full step")
    return args


def main(argv=None) -> int:
    args = parse_args(argv)
    report = {"passed": False, "scope": "single_board_pad_and_sequence_observation",
              "independent_input_count_verified": False, "vna_role_runtime_verified": False,
              "external_waveform_verified": False, "rf_path_verified": False,
              "started_at": datetime.now(timezone.utc).isoformat(),
              "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "settings": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
              "phases": {}, "transcript": [], "failure": None}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("x", encoding="utf-8") as evidence:
        try:
            report["devices"] = discover()
            if args.mode == "discover":
                print(json.dumps(report["devices"], ensure_ascii=True), flush=True)
                report["passed"] = True
            else:
                select_transport(args, report["devices"])
                report["transport"] = args.port or args.visa_resource
                with ExitStack() as stack:
                    if args.port:
                        port = stack.enter_context(open_serial_port(args.port, 115200, args.timeout, .1,
                                                                     read_timeout_s=.02))
                        bench = Bench(port, args, report)
                    else:
                        port = stack.enter_context(open_visa_resource(args.visa_resource, args.timeout))
                        bench = Bench(port, args, report, lambda cmd, timeout: visa_command(port, cmd, timeout))
                    idn = next(csv.reader([bench.command("*IDN?")]))
                    build = bench.command("SYST:FW:BUILD?").strip('"')
                    report["identity"] = {"idn": idn, "build": build}
                    require(len(idn) == 4 and idn[2] == args.serial_number, "unexpected device identity")
                    require(args.build is None or build == args.build, "unexpected firmware build")
                    run_phases(bench, report)
                    report["passed"] = True
        except (Exception, KeyboardInterrupt) as exc:
            report["failure"] = str(exc) or type(exc).__name__
        finally:
            json.dump(report, evidence, indent=2, ensure_ascii=True)
            evidence.write("\n")
    print(json.dumps({"passed": report["passed"], "failure": report["failure"], "evidence": str(args.out)}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
