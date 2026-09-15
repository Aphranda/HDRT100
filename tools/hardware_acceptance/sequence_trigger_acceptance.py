#!/usr/bin/env python3
"""Verify SP8T sequence control and pad readback on one identified board.

BUS mode sends software NEXT commands. IN1..IN4 mode only observes a separately
generated, finite pulse burst during --duration; --input-events is the count
from that source. Use a period longer than settle+pulse+SCPI polling latency.
This report does not certify external waveforms, RF paths, or the P3 gate.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import open_serial_port
from tools.scpi_query.scpi_query import send_command

STATUS_FIELDS = (
    "state", "run_id", "generation", "count", "current_index", "current_state",
    "next_index", "executed_index", "executed_state", "completed_index",
    "completed_state", "cycles", "accepted", "completed", "busy_rejected",
    "notready_rejected", "cancelled", "faults", "error", "written_us",
    "rise_us", "completed_us", "late_us", "backend_fault",
)
STATES = {"IDLE", "STARTING", "READY", "BUSY", "PAUSING", "PAUSED", "STOPPING", "FAULT"}
# Snapshot of SYNC_IO_SEQUENCE_TIME_MAX_US; the firmware validates configuration.
TIME_MAX_US = 0xffffffff // 10


class AcceptanceError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AcceptanceError(message)


def parse_status(response: str) -> dict:
    try:
        values = next(csv.reader([response], strict=True))
    except (csv.Error, StopIteration) as exc:
        raise AcceptanceError("malformed sequence status") from exc
    require(len(values) == len(STATUS_FIELDS), "incomplete sequence status")
    row = dict(zip(STATUS_FIELDS, values))
    require(row["state"] in STATES, "unknown sequence state")
    for key in STATUS_FIELDS:
        if key in {"state", "error"}:
            continue
        text = row[key]
        require(text.isascii() and text.isdecimal() and len(text) <= 20, f"invalid status field {key}")
        row[key] = int(text)
        maximum = (1 << (64 if key in {"written_us", "rise_us", "completed_us"} else 32)) - 1
        require(row[key] <= maximum, f"status overflow {key}")
    return row


def validate_sample(row: dict, baseline: dict, previous: int) -> int:
    require(row["run_id"] == baseline["run_id"] and
            row["generation"] == baseline["generation"], "run changed or device reset")
    require(row["count"] == 8, "SP8T plan no longer has eight states")
    require(row["state"] in {"READY", "BUSY"}, f"unexpected runtime state {row['state']}")
    require(row["error"] == "NONE" and row["faults"] == 0 and
            row["backend_fault"] == 0 and row["cancelled"] == 0, "runtime fault or cancellation")
    completed = row["completed"]
    require(previous <= completed <= previous + 1, "completion regressed or sampling missed a state")
    require(completed <= row["accepted"] <= completed + 1, "accepted/completed accounting mismatch")
    require(row["next_index"] == completed % 8, "incorrect next SP8T index")
    require(row["cycles"] == max(0, (row["accepted"] - 1) // 8), "incorrect wrap count")
    if completed:
        code = (completed - 1) % 8
        require(row["completed_index"] == code and row["completed_state"] == code,
                "incorrect completed SP8T address")
    if row["accepted"]:
        code = (row["accepted"] - 1) % 8
        require(row["current_index"] == code and row["current_state"] == code,
                "incorrect selected SP8T address")
    if row["state"] == "READY":
        require(row["accepted"] == completed, "READY with unfinished accepted step")
    return completed


def parse_timing(response: str) -> dict:
    try:
        fields = next(csv.reader([response], strict=True))
    except (csv.Error, StopIteration) as exc:
        raise AcceptanceError("malformed timing metadata") from exc
    require(len(fields) == 3 and fields[0] == "PIO0" and fields[2] == "0",
            "expected PIO0 receipts without physical timestamps")
    require(fields[1].isascii() and fields[1].isdecimal() and
            0 < int(fields[1]) <= 0xffffffff, "invalid PIO tick")
    return {"backend": fields[0], "tick_ns": int(fields[1]), "physical_timestamps_valid": False}


def validate_totals(row: dict, steps: int, busy: int, input_events: int | None) -> None:
    require(row["state"] in {"READY", "PAUSED"}, "input burst ended with an unfinished step")
    require(row["accepted"] == steps and row["completed"] == steps, "step count mismatch")
    require(row["busy_rejected"] == busy, "busy rejection count mismatch")
    require(row["notready_rejected"] == 0 and row["cancelled"] == 0 and
            row["faults"] == 0 and row["backend_fault"] == 0 and row["error"] == "NONE",
            "unexpected rejection, cancellation or fault")
    if input_events is not None:
        require(row["accepted"] + row["busy_rejected"] == input_events,
                "observed events differ from independent pulse count")


class Bench:
    def __init__(self, serial_port, args, report):
        self.serial = serial_port
        self.args = args
        self.report = report

    def command(self, command: str, timeout: float | None = None) -> str:
        start = time.monotonic()
        response = send_command(self.serial, command, self.args.timeout if timeout is None else timeout)
        self.report["transcript"].append({"command": command, "response": response,
                                           "at": start, "elapsed": time.monotonic() - start})
        require(response != "<timeout>", f"SCPI timeout: {command}")
        return response

    def write(self, command: str) -> None:
        require(self.command(command) == "1", f"SCPI command rejected: {command}")

    def status(self) -> dict:
        return parse_status(self.command("READ:SEQ:NEXT?"))

    def settled_rejections(self, row: dict) -> None:
        fields = next(csv.reader([self.command("READ:SEQ:REJ?")]))
        expected = [str(row[key]) for key in
                    ("run_id", "generation", "busy_rejected", "notready_rejected")] + ["0"]
        require(fields == expected, "rejection counts are pending or changed")
        self.report["rejection_counts_settled"] = True

    def wait_state(self, expected: str) -> dict:
        deadline = time.monotonic() + self.args.timeout
        while time.monotonic() < deadline:
            row = self.status()
            if row["state"] == expected:
                return row
            require(row["state"] != "FAULT", f"runtime fault: {row}")
            time.sleep(self.args.poll)
        raise AcceptanceError(f"timeout waiting for {expected}")

    def identity(self) -> None:
        idn = next(csv.reader([self.command("*IDN?")]))
        build = self.command("SYST:FW:BUILD?").strip('"')
        require(len(idn) == 4 and idn[2] == self.args.serial_number, "unexpected board identity")
        require(build == self.args.build, "unexpected firmware build")
        self.report["identity"] = {"idn": idn, "build": build}

    def configure(self) -> None:
        self.write("TRIG:STOP")
        self.wait_state("IDLE")
        require(self.command("SYST:ERR?").startswith('0,'), "pre-existing SCPI error; inspect error queue")
        for command in (
            "CONF:TRIG 8,0,1,1", "CONF:SEQ SP8T,0,1,2,3,4,5,6,7", "CONF:SEQ:ACT SP8T",
            f"CONF:SEQ:IO 7,OUT4,{self.args.settle_us},{self.args.pulse_us}",
            *(f"CONF:SEQ:CODE {code},{code}" for code in range(8)),
            f"CONF:SEQ:SOUR {self.args.source},{self.args.edge}",
        ):
            self.write(command)
        plan = next(csv.reader([self.command("READ:SEQ? SP8T")]))
        require(plan[-8:] == [str(code) for code in range(8)], "SP8T plan readback mismatch")
        for code in range(8):
            require(self.command(f"READ:SEQ:CODE? {code}") == f"{code},{code}", "code readback mismatch")
        source = next(csv.reader([self.command("READ:SEQ:SOUR?")]))
        require(source == [self.args.source, "RISING" if self.args.edge == "RIS" else "FALLING"],
                "input source readback mismatch")
        io = self.command("READ:SEQ:IO?").split(",")
        require(io[:4] == ["7", "4", str(self.args.settle_us), str(self.args.pulse_us)] and
                io[-1] == "1", "IO configuration readback mismatch")

    def sample_output(self, row: dict) -> None:
        require(row["state"] == "READY", "completion could not be sampled before the next event")
        code = (row["completed"] - 1) % 8
        require(row["executed_index"] == code and row["executed_state"] == code,
                "executed address mismatch")
        require(self.report.get("timing", {}).get("backend") == "PIO0", "missing timing metadata")
        require(all(row[key] == 0 for key in ("written_us", "rise_us", "completed_us", "late_us")),
                "PIO receipts must not claim physical timestamps")
        output = int(self.command("READ:IO:OUTP?"))
        after = self.status()
        require(after == row, "state changed during pad read; use slower external pulses")
        require(output == code, f"actual output {output} differs from SP8T address {code}")
        self.report["steps"].append({"status": row, "output": output})

    def execute(self) -> None:
        self.write("TRIG:START")
        baseline = self.wait_state("READY")
        self.report["timing"] = parse_timing(self.command("READ:SEQ:TIM?"))
        require(baseline["accepted"] == 0 and baseline["completed"] == 0, "START executed without a new event")
        previous = 0
        row = baseline
        if self.args.source == "BUS":
            for _ in range(self.args.steps):
                self.write("CONF:SEQ:NEXT")
                row = self.wait_state("READY")
                completed = validate_sample(row, baseline, previous)
                require(completed == previous + 1, "STEP failed to advance exactly once")
                self.sample_output(row)
                previous = completed
                time.sleep(self.args.quiet)
                require(self.status() == row, "DONE advanced the sequence automatically")
        else:
            print(f"ARMED {self.args.source} {self.args.edge}: observe {self.args.input_events} external edges "
                  f"for {self.args.duration}s", flush=True)
            deadline = time.monotonic() + self.args.duration
            while time.monotonic() < deadline:
                row = self.status()
                completed = validate_sample(row, baseline, previous)
                if completed > previous:
                    self.sample_output(row)
                    previous = completed
                time.sleep(self.args.poll)
            self.write("TRIG:PAUS")
            row = self.wait_state("PAUSED")
            require(row["completed"] == previous, "unobserved completion at pause boundary")
        self.settled_rejections(row)
        validate_totals(row, self.args.steps, self.args.busy, self.args.input_events)
        self.report["final_running_status"] = row
        require(len(self.report["steps"]) == self.args.steps, "missing per-step IO evidence")
        self.identity()
        require(self.command("SYST:ERR?").startswith('0,'), "SCPI error after execution")

    def stop(self) -> None:
        self.write("TRIG:STOP")
        row = self.wait_state("IDLE")
        self.report["stopped_status"] = row
        if "final_running_status" in self.report:
            before = self.report["final_running_status"]
            for key in ("run_id", "generation", "accepted", "completed", "busy_rejected",
                        "notready_rejected", "cancelled", "faults", "backend_fault"):
                require(row[key] == before[key], "events or faults occurred during cleanup")
        io = self.command("READ:IO:STAT?").split(",")
        require(len(io) == 5 and io[1:] == ["0", "0", "0", "0"], "STOP did not release outputs")
        self.report["stopped_io"] = io


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--source", choices=("BUS", "IN1", "IN2", "IN3", "IN4"), default="BUS")
    parser.add_argument("--edge", choices=("RIS", "FALL"), default="RIS")
    parser.add_argument("--steps", type=int, default=9)
    parser.add_argument("--input-events", type=int)
    parser.add_argument("--busy", type=int, default=0)
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--settle-us", type=int, default=100000)
    parser.add_argument("--pulse-us", type=int, default=200000)
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--poll", type=float, default=.02)
    parser.add_argument("--quiet", type=float, default=.05)
    args = parser.parse_args(argv)
    if args.steps < 9 or args.busy < 0:
        parser.error("--steps must cover all eight states and wrap (>=9); --busy must be nonnegative")
    if args.source == "BUS" and (args.input_events is not None or args.busy):
        parser.error("BUS mode does not accept --input-events or --busy")
    if args.source != "BUS" and args.input_events != args.steps + args.busy:
        parser.error("external mode requires --input-events equal to --steps plus --busy")
    if not (0 <= args.settle_us <= TIME_MAX_US and 0 < args.pulse_us <= TIME_MAX_US):
        parser.error("timing must fit the firmware range")
    if any(not math.isfinite(value) or value <= 0 for value in
           (args.timeout, args.poll, args.duration, args.quiet)):
        parser.error("timeouts and intervals must be finite and positive")
    if args.timeout <= (args.settle_us + args.pulse_us) / 1e6:
        parser.error("--timeout must exceed the configured step duration")
    return args


def main(argv=None) -> int:
    args = parse_args(argv)
    report = {"passed": False, "scope": "software_sp8t" if args.source == "BUS" else "external_scpi_observation",
              "external_waveform_verified": False, "rf_path_verified": False, "p3_receipt": False,
              "completion_high_observed": False,
              "started_at": datetime.now(timezone.utc).isoformat(),
              "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
              "steps": [], "transcript": [], "failure": None, "cleanup_failure": None}
    # Refuse to replace earlier evidence, including failed attempts.
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("x", encoding="utf-8") as evidence:
        try:
            with open_serial_port(args.port, 115200, args.timeout, .1, read_timeout_s=.02) as serial_port:
                bench = Bench(serial_port, args, report)
                bench.identity()
                try:
                    bench.configure()
                    bench.execute()
                except (Exception, KeyboardInterrupt) as exc:
                    report["failure"] = str(exc) or type(exc).__name__
                finally:
                    try:
                        bench.stop()
                    except (Exception, KeyboardInterrupt) as exc:
                        report["cleanup_failure"] = str(exc)
            report["passed"] = report["failure"] is None and report["cleanup_failure"] is None
        except (Exception, KeyboardInterrupt) as exc:
            report["failure"] = str(exc) or type(exc).__name__
        finally:
            json.dump(report, evidence, indent=2)
            evidence.write("\n")
    print(json.dumps({"passed": report["passed"], "failure": report["failure"],
                      "cleanup_failure": report["cleanup_failure"], "evidence": str(args.out)}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
