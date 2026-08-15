#!/usr/bin/env python3
"""Validate ModelTurntable LOAD through RefMem NodeLoad staging."""

from __future__ import annotations

import argparse
import csv
import json
import re
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]


@dataclass
class Step:
    name: str
    command: str
    response: str
    passed: bool
    reason: str


def parse_csv_response(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except csv.Error:
        return []


def parse_ints(response: str) -> list[int]:
    values: list[int] = []
    for part in parse_csv_response(response):
        try:
            values.append(int(part.strip().strip('"'), 0))
        except ValueError:
            pass
    return values


def read_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = bytearray()
        while time.monotonic() < deadline:
            ch = ser.read(1)
            if not ch:
                continue
            raw.extend(ch)
            if ch == b"\n":
                break
        if not raw:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"[') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if line in {'"OK"', "OK", 'OK"'} or line.startswith('"OK[') or line.startswith("OK["):
            return '"OK"'
        return re.sub(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+.*$', "", line).strip()
    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_line(ser, timeout_s)


def check_unloaded(response: str) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 3:
        return False, f"load response too short: {response}"
    if fields[0] != 0 or fields[1] != 0xFFFFFFFF:
        return False, f"turntable already loaded: {fields}"
    return True, "OK"


def check_loaded(response: str, slot_id: int, output_index: int) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 3:
        return False, f"load response too short: {response}"
    if fields[0] != 1 or fields[1] != slot_id or fields[2] != output_index:
        return False, f"turntable load mismatch: {fields}"
    return True, "OK"


def check_config_ack_ready(response: str) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 12:
        return False, f"config ACK response too short: {response}"
    target = fields[2]
    ack = fields[3]
    nack = fields[4]
    busy = fields[5]
    timeout = fields[6]
    reason = fields[7]
    if target == 0 or ack != target or nack != 0 or busy != 0 or timeout != 0 or reason != 0:
        return False, (
            f"config ACK not ready: target={target} ack={ack} "
            f"nack={nack} busy={busy} timeout={timeout} reason={reason}"
        )
    return True, "OK"


def check_command_ack_node_load(response: str, slot_id: int, output_index: int) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 28:
        return False, f"command ACK response too short: {response}"
    version = fields[0]
    state = fields[1]
    source_instance = fields[4]
    target = fields[5]
    required = fields[6]
    command_type = fields[7]
    command_class = fields[8]
    payload_kind = fields[9]
    payload_ref = fields[10]
    payload_size = fields[11]
    taken = fields[17]
    ack = fields[18]
    nack = fields[19]
    busy = fields[20]
    timeout = fields[21]
    last_reason = fields[22]
    if version != 1:
        return False, f"command ACK schema version mismatch: {version}"
    target_mask = 1 << slot_id
    if (
        state != 4
        or source_instance != 10
        or target != target_mask
        or required != target_mask
        or command_type != 14
        or command_class != 1
        or payload_kind != 1
        or payload_ref != output_index
        or payload_size != 8
        or taken != target_mask
        or ack != target_mask
        or nack != 0
        or busy != 0
        or timeout != 0
        or last_reason != 0
    ):
        return False, f"unexpected command ACK fields: {fields}"
    return True, "OK"


def check_command_nack_none(response: str) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 7:
        return False, f"command NACK response too short: {response}"
    if fields[0] != 1 or fields[2] != 0:
        return False, f"unexpected command NACK reason fields: {fields}"
    if "NONE" not in response:
        return False, f"command NACK reason name missing: {response}"
    return True, "OK"


def check_refmem_load_staging(response: str, slot_id: int) -> tuple[bool, str]:
    fields = parse_ints(response)
    if len(fields) < 22:
        return False, f"RefMem load status too short: {response}"
    version = fields[0]
    source = fields[2]
    mode = fields[3]
    staging_state = fields[4]
    staging_node = fields[14]
    staging_instance = fields[15]
    staging_role = fields[16]
    staging_persona = fields[17]
    staging_enabled = fields[18]
    staging_required = fields[19]
    last_error = fields[21]
    if version != 1 or source != 2 or mode != 0 or staging_state != 2:
        return False, f"unexpected load lifecycle fields: {fields[:5]}"
    if staging_node != slot_id or staging_instance != 10:
        return False, f"unexpected staged node/instance: node={staging_node} instance={staging_instance}"
    if staging_role != 0xC0 or staging_persona != 0x10 or staging_enabled != 1 or staging_required != 0:
        return False, (
            "unexpected staged role/persona/enabled/required: "
            f"{staging_role},{staging_persona},{staging_enabled},{staging_required}"
        )
    if last_error != 0:
        return False, f"RefMem load last_error={last_error}"
    return True, "OK"


def run_step(steps: list[Step],
             ser: serial.Serial,
             name: str,
             command: str,
             timeout_s: float,
             check) -> bool:
    response = query(ser, command, timeout_s)
    ok, reason = check(response)
    steps.append(Step(name, command, response, ok, reason))
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--output", type=int, default=0)
    parser.add_argument("--expected-build")
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or ROOT / "build-rtos-multicore-smoke" / f"model_turntable_load_{args.port}_{started}"
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    steps: list[Step] = []
    failures: list[str] = []

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        build = query(ser, "SYSTem:FW:BUILD?", args.timeout).strip('"')
        build_ok = args.expected_build is None or build == args.expected_build
        steps.append(Step("build",
                          "SYSTem:FW:BUILD?",
                          build,
                          build_ok,
                          "OK" if build_ok else f"build {build!r} != {args.expected_build!r}"))
        if not build_ok:
            failures.append(steps[-1].reason)

        if not run_step(steps,
                        ser,
                        "before_unloaded",
                        "READ:MODEl:TURNtable:LOAD?",
                        args.timeout,
                        check_unloaded):
            failures.append(steps[-1].reason)

        if not run_step(steps,
                        ser,
                        "before_config_ack",
                        "SYSTem:CONFigure:ACK?",
                        args.timeout,
                        check_config_ack_ready):
            failures.append(steps[-1].reason)

        load_command = f"CONFigure:MODEl:TURNtable:LOAD {args.slot},{args.output}"
        load_response = query(ser, load_command, args.timeout)
        load_ok = load_response == '"OK"'
        steps.append(Step("load_command",
                          load_command,
                          load_response,
                          load_ok,
                          "OK" if load_ok else "LOAD command was not accepted"))
        if not load_ok:
            failures.append(steps[-1].reason)

        if not run_step(steps,
                        ser,
                        "after_loaded",
                        "READ:MODEl:TURNtable:LOAD?",
                        args.timeout,
                        lambda response: check_loaded(response, args.slot, args.output)):
            failures.append(steps[-1].reason)

        if not run_step(steps,
                        ser,
                        "command_ack_node_load",
                        "SYSTem:COMMand:ACK?",
                        args.timeout,
                        lambda response: check_command_ack_node_load(response, args.slot, args.output)):
            failures.append(steps[-1].reason)

        if not run_step(steps,
                        ser,
                        "command_nack_none",
                        "SYSTem:COMMand:NACK?",
                        args.timeout,
                        check_command_nack_none):
            failures.append(steps[-1].reason)

        if not run_step(steps,
                        ser,
                        "refmem_load_staging",
                        "SYSTem:REFMEM:LOAD:STATus?",
                        args.timeout,
                        lambda response: check_refmem_load_staging(response, args.slot)):
            failures.append(steps[-1].reason)

        if not run_step(steps,
                        ser,
                        "after_config_ack",
                        "SYSTem:CONFigure:ACK?",
                        args.timeout,
                        check_config_ack_ready):
            failures.append(steps[-1].reason)

        err = query(ser, "SYSTem:ERRor?", args.timeout)
        err_ok = err.startswith("0,") or "No error" in err
        steps.append(Step("error_queue",
                          "SYSTem:ERRor?",
                          err,
                          err_ok,
                          "OK" if err_ok else "SCPI error queue not empty"))
        if not err_ok:
            failures.append(steps[-1].reason)

    report = {
        "started": started,
        "port": args.port,
        "slot": args.slot,
        "output": args.output,
        "passed": not failures,
        "failures": failures,
        "steps": [asdict(step) for step in steps],
    }
    (out_dir / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    lines = [
        f"passed={not failures}",
        f"port={args.port}",
        f"out_dir={out_dir}",
    ] + [f"{step.name}: {'PASS' if step.passed else 'FAIL'} {step.reason}" for step in steps]
    lines.extend(f"failure={failure}" for failure in failures)
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
