#!/usr/bin/env python3
"""Switch one identified board's USB mode and verify its re-enumerated identity.

This is a functional maintenance report, not a hardware acceptance receipt.
No commands are replayed after a lost write response.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance.sequence_feedback_validate import discover, parse_io
from tools.hardware_acceptance.sequence_trigger_acceptance import (
    open_visa_resource, parse_status, visa_command,
)
from tools.scpi_common.board_identity import normalize_build_response, parse_idn_response
from tools.scpi_common.scpi_serial import open_serial_port
from tools.scpi_query.scpi_query import send_command
from tools.tdma_ring_monitor.tdma_field_parse import RUNTIME_FIELDS


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def parse_mode(response):
    mode = response.strip().strip('"').upper()
    require(mode in {"CDC", "USBTMC"},
            f"USB runtime mode query failed or unsupported: {response!r}")
    return mode


def parse_ring(response):
    fields = response.split(",")
    require(len(fields) == len(RUNTIME_FIELDS) and
            all(v.isascii() and v.isdecimal() for v in fields),
            "invalid TDMA runtime snapshot")
    return dict(zip(RUNTIME_FIELDS, map(int, fields), strict=True))


def candidates(devices, uid, mode=None):
    result = []
    if mode in (None, "CDC"):
        result.extend(("CDC", p["port"]) for p in devices["serial"]
                      if p["serial_number"] == uid and p["vid"] == 0xCAFE)
    if mode in (None, "USBTMC"):
        for resource in devices["visa"]:
            fields = resource.split("::")
            if (len(fields) >= 5 and fields[0].startswith("USB") and
                    fields[3] == uid and fields[-1] == "INSTR"):
                try:
                    vendor = int(fields[1], 0)
                except ValueError:
                    continue
                if vendor == 0xCAFE:
                    result.append(("USBTMC", resource))
    return result


@contextmanager
def connection(endpoint, args):
    mode, resource = endpoint
    if mode == "CDC":
        with open_serial_port(resource, 115200, args.timeout, .2) as device:
            yield device
    else:
        with open_visa_resource(resource, args.timeout) as device:
            yield device


class Session:
    def __init__(self, endpoint, device, args, report):
        self.endpoint, self.device, self.args, self.report = endpoint, device, args, report

    def command(self, command, *, allow_timeout=False):
        entry = {"time": datetime.now(timezone.utc).isoformat(),
                 "endpoint": self.endpoint, "command": command}
        self.report["exchanges"].append(entry)
        try:
            response = (send_command(self.device, command, self.args.timeout)
                        if self.endpoint[0] == "CDC" else
                        visa_command(self.device, command, self.args.timeout))
            entry["response"] = response
            require(allow_timeout or response != "<timeout>", f"timeout: {command}")
            return response
        except Exception as exc:
            entry["error"] = f"{type(exc).__name__}: {exc}"
            # VISA reports the same optional missing STOP payload as an
            # exception. Only its timeout code is tolerated; STOP still must
            # be proven by the subsequent runtime owner snapshot.
            if allow_timeout and getattr(exc, "error_code", None) == -1073807339:
                return "<timeout>"
            raise

    def identity(self):
        identity = parse_idn_response(self.command("*IDN?"))
        require(identity.serial_number == self.args.serial_number, "board UID mismatch")
        build = normalize_build_response(self.command("SYST:FW:BUILD?"))
        require(build == self.args.build, f"board build mismatch: {build}")
        mode = parse_mode(self.command("SYST:USB:MODE?"))
        return {"idn": identity.idn, "build": build, "mode": mode,
                "endpoint": self.endpoint}

    def stop(self):
        require(self.command("TRIG:STOP") == "1", "sequence STOP not acknowledged")
        deadline = time.monotonic() + self.args.timeout
        while True:
            state = parse_status(self.command("TRIG:SEQ:NEXT?"))
            if state["state"] == "IDLE":
                break
            require(time.monotonic() < deadline, "sequence failed to become IDLE")
            time.sleep(.1)
        io = parse_io(self.command("READ:IO:STAT?"))
        require(all(io[k] == 0 for k in ("outputs", "owned", "armed", "busy")),
                "sequence STOP did not release IO")
        # STOP may have no payload; the authoritative owner readback is required.
        self.command("SYST:TDMA:RING:STOP", allow_timeout=True)
        deadline = time.monotonic() + self.args.timeout
        while True:
            fields = parse_ring(self.command("SYST:TDMA:RING:STAT?"))
            if fields["ring_enabled"] == fields["ring_adapter_started"] == 0:
                break
            require(time.monotonic() < deadline, "TDMA failed to stop")
            time.sleep(.1)
        require(self.command("SYST:ERR?").startswith("0,"), "SCPI error before USB switch")

    def boot(self):
        entry = {"endpoint": self.endpoint, "command": "SYST:USB:BOOT"}
        self.report["exchanges"].append(entry)
        try:
            if self.endpoint[0] == "CDC":
                self.device.write(b"SYST:USB:BOOT\n")
                self.device.flush()
            else:
                self.device.write("SYST:USB:BOOT")
            entry["sent"] = True
        except Exception as exc:
            # A disconnect is only tolerated here and never proves success.
            entry["disconnect_or_write_error"] = f"{type(exc).__name__}: {exc}"


def await_target(args, report):
    deadline = time.monotonic() + args.enumeration_timeout
    attempts = report["enumeration"] = []
    while True:
        devices = discover()
        matches = candidates(devices, args.serial_number, args.target)
        attempt = {"devices": devices, "matches": matches}
        attempts.append(attempt)
        require(len(matches) <= 1, "ambiguous target USB enumeration")
        if matches:
            try:
                with connection(matches[0], args) as device:
                    identity = Session(matches[0], device, args, report).identity()
                require(identity["mode"] == args.target, "target mode readback mismatch")
                return identity
            except Exception as exc:
                attempt["error"] = f"{type(exc).__name__}: {exc}"
        require(time.monotonic() < deadline, "target USB did not re-enumerate with expected UID/build/mode")
        time.sleep(.5)


def run(args, report):
    if args.port:
        endpoint = ("CDC", args.port)
    elif args.visa_resource:
        endpoint = ("USBTMC", args.visa_resource)
    else:
        devices = report["initial_discovery"] = discover()
        matches = candidates(devices, args.serial_number)
        require(len(matches) == 1, "no unique USB device matches requested UID")
        endpoint = matches[0]
    with connection(endpoint, args) as device:
        session = Session(endpoint, device, args, report)
        initial = report["initial"] = session.identity()
        if args.verify_only:
            require(initial["mode"] == endpoint[0], "configured mode differs from active transport")
            if args.target:
                require(initial["mode"] == args.target, "current mode differs from target")
            report["final"] = initial
            return
        session.stop()
        if initial["mode"] != args.target:
            require(session.command(f"SYST:USB:MODE {args.target}").strip().strip('"') == "OK",
                    "USB MODE not acknowledged")
        require(parse_mode(session.command("SYST:USB:MODE?")) == args.target,
                "USB mode write readback mismatch")
        if endpoint[0] == args.target:
            report["final"] = session.identity()
            return
        session.boot()
    report["final"] = await_target(args, report)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port")
    transport.add_argument("--visa-resource")
    parser.add_argument("--serial-number", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--target", choices=("CDC", "USBTMC"))
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--timeout", type=float, default=3)
    parser.add_argument("--enumeration-timeout", type=float, default=45)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    for key in ("timeout", "enumeration_timeout"):
        require(math.isfinite(getattr(args, key)) and getattr(args, key) > 0,
                f"{key} must be finite and positive")
    if not args.verify_only and args.target is None:
        args.target = "USBTMC"
    return args


def main(argv=None):
    args = parse_args(argv)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    # Reserve before touching hardware; never overwrite an earlier result.
    with args.out.open("x", encoding="utf-8") as output:
        report = {"started": datetime.now(timezone.utc).isoformat(),
                  "serial_number": args.serial_number, "build": args.build,
                  "target": args.target, "verify_only": args.verify_only,
                  "passed": False, "exchanges": []}
        try:
            run(args, report)
            report["passed"] = True
        except (Exception, KeyboardInterrupt) as exc:
            report["error"] = f"{type(exc).__name__}: {exc}"
        finally:
            report["finished"] = datetime.now(timezone.utc).isoformat()
            json.dump(report, output, ensure_ascii=False, indent=2)
            output.write("\n")
    print(json.dumps({"passed": report["passed"], "out": str(args.out),
                      "error": report.get("error")}, ensure_ascii=False))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
