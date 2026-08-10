#!/usr/bin/env python3
"""Validate RP2350_TRIG dual-core (RTOS + AMP) smoke on the bench over SCPI USB CDC.

Checks:
- *IDN?, SYST:FW:BUILD? baseline
- SYST:CORE? core1 enabled and heartbeat growing
- LOOP:STAT? loop engine ready and service counter growing
- VDC:STAT? VDC sync skeleton ready and service counter growing
- DPLL:STAT? DPLL skeleton ready and service counter growing
- TRIG:MODE 1 -> ARM -> DISARM state progression
- Error queue, LOG STAT, TRACE LAST
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
TASK_RULES = """需要烧录固件前必须提前告知用户烧录对象、原因和预期影响；
若需要用户按 BOOTSEL、断电或复位，必须停下来等待用户操作。"""


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _read_line(ser: serial.Serial, timeout_s: float) -> str:
    """Read one meaningful SCPI response line, skipping logs and empty lines."""
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
        if len(raw) == 0:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"[') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if line.startswith('"OK"') or line.startswith('OK"') or line.startswith('"OK[') or line.startswith('OK['):
            return '"OK"'
        line = re.sub(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+.*$', '', line).strip()
        return line
    return "<timeout>"


def _cmd(ser: serial.Serial, command: str, timeout_s: float) -> str:
    """Send a command and read the ACK line."""
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return _read_line(ser, timeout_s)


def _query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    """Send a query and read the response (no ACK)."""
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return _read_line(ser, timeout_s)


def _parse_ints(response: str) -> list[int]:
    """Parse a CSV integer response like '1,0,95293475,0,68513'."""
    parts = [p.strip() for p in response.split(",")]
    out: list[int] = []
    for p in parts:
        try:
            out.append(int(p))
        except ValueError:
            pass
    return out


def _ack_ok(response: str) -> bool:
    return response in {'"OK"', "OK", '"OK', "OK\"", '"O', "O"}


# ---------------------------------------------------------------------------
# test functions – each returns (passed: bool, detail: str)
# ---------------------------------------------------------------------------

def test_identity(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "*IDN?", timeout)
    if resp == "<timeout>":
        return False, "*IDN? timed out"
    if "GTS" in resp or "RP2350_TRIG" in resp or "SYNC_TRIGGER" in resp:
        return True, resp
    return False, f"unexpected *IDN? response: {resp}"


def test_build_id(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:FW:BUILD?", timeout)
    if resp == "<timeout>":
        return False, "SYST:FW:BUILD? timed out"
    # Response is a quoted build id like "20260810072841"
    if len(resp) >= 14:
        return True, resp
    return False, f"unexpected build id: {resp}"


def test_core_heartbeat(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read SYST:CORE? 3 times, 1s apart.  core1 must be enabled AND loop count growing."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "SYST:CORE?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 5:
            return False, f"SYST:CORE? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    # check core1 enabled (field 0 == 1)
    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"SYST:CORE? read {i+1}: core1 NOT enabled (field0={fields[0]})"

    # check heartbeat growing (field 2 = core1 loop count)
    lc = [r[2] for r in reads]
    if lc[0] < lc[1] < lc[2]:
        return True, f"core1 enabled, loop counts: {lc[0]} -> {lc[1]} -> {lc[2]} (+{lc[2]-lc[0]})"
    if lc[0] == lc[1] or lc[1] == lc[2]:
        return False, f"core1 loop count STALLED: {lc}"
    return False, f"core1 loop count not monotonic: {lc}"


def test_trigger_seq(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """TRIG:MODE 1 -> STAT:TRIG? -> ARM -> STAT:TRIG? -> DIS -> STAT:TRIG?."""
    steps: list[str] = []

    # Make the test repeatable after an interrupted/manual ARM sequence.
    _cmd(ser, "TRIG:DIS", timeout)

    # Set mode
    ack = _cmd(ser, "TRIG:MODE 1", timeout)
    if not _ack_ok(ack):
        return False, f"TRIG:MODE 1 ack: {ack}"
    steps.append("MODE=1")

    # Pre-arm status
    s0 = _query(ser, "STAT:TRIG?", timeout)
    f0 = _parse_ints(s0)
    if len(f0) < 2 or f0[0] != 1:
        return False, f"Pre-ARM state expected state_id=1 (SEQ_CONFIGURED), got: {s0}"
    steps.append(f"state_before={f0[0]}")

    # Arm
    ack = _cmd(ser, "TRIG:ARM", timeout)
    if not _ack_ok(ack):
        return False, f"TRIG:ARM ack: {ack}"
    steps.append("ARMED")

    # Armed status
    s1 = _query(ser, "STAT:TRIG?", timeout)
    f1 = _parse_ints(s1)
    if len(f1) < 2 or f1[0] != 2:
        return False, f"ARMED state expected state_id=2 (SEQ_ARMED), got: {s1}"
    steps.append(f"state_armed={f1[0]}")

    # Disarm
    ack = _cmd(ser, "TRIG:DIS", timeout)
    if not _ack_ok(ack):
        return False, f"TRIG:DIS ack: {ack}"
    steps.append("DISARMED")

    # Post-disarm status
    s2 = _query(ser, "STAT:TRIG?", timeout)
    f2 = _parse_ints(s2)
    if len(f2) < 2 or f2[0] != 0:
        return False, f"DISARMED state expected state_id=0 (IDLE), got: {s2}"
    steps.append(f"state_idle={f2[0]}")

    return True, " -> ".join(steps)


def test_loop_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read LOOP:STAT? 3 times, 1s apart. service_count must grow and ready must be true."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "LOOP:STAT?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 4:
            return False, f"LOOP:STAT? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"LOOP:STAT? read {i+1}: loop engine NOT ready (field0={fields[0]})"

    service_counts = [r[1] for r in reads]
    if service_counts[0] < service_counts[1] < service_counts[2]:
        return True, (
            "loop engine ready, service counts: "
            f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}"
        )
    if service_counts[0] == service_counts[1] or service_counts[1] == service_counts[2]:
        return False, f"LOOP:STAT? service count STALLED: {service_counts}"
    return False, f"LOOP:STAT? service count not monotonic: {service_counts}"


def test_vdc_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read VDC:STAT? 3 times, 1s apart. service_count must grow and ready must be true."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "VDC:STAT?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 6:
            return False, f"VDC:STAT? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"VDC:STAT? read {i+1}: VDC sync NOT ready (field0={fields[0]})"

    service_counts = [r[2] for r in reads]
    sync_seq = [r[5] for r in reads]
    if service_counts[0] < service_counts[1] < service_counts[2] and sync_seq[0] < sync_seq[1] < sync_seq[2]:
        return True, (
            "VDC sync ready, service counts: "
            f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}, "
            f"sync_seq: {sync_seq[0]} -> {sync_seq[1]} -> {sync_seq[2]}, "
            f"lock_state={reads[-1][1]}"
        )
    if service_counts[0] == service_counts[1] or service_counts[1] == service_counts[2]:
        return False, f"VDC:STAT? service count STALLED: {service_counts}"
    if sync_seq[0] == sync_seq[1] or sync_seq[1] == sync_seq[2]:
        return False, f"VDC:STAT? sync_seq STALLED: {sync_seq}"
    return False, f"VDC:STAT? counters not monotonic: service={service_counts}, sync_seq={sync_seq}"


def test_dpll_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read DPLL:STAT? 3 times, 1s apart. service_count must grow and ready must be true."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "DPLL:STAT?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 6:
            return False, f"DPLL:STAT? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"DPLL:STAT? read {i+1}: DPLL NOT ready (field0={fields[0]})"

    service_counts = [r[2] for r in reads]
    update_seq = [r[5] for r in reads]
    if service_counts[0] < service_counts[1] < service_counts[2] and update_seq[0] < update_seq[1] < update_seq[2]:
        return True, (
            "DPLL ready, service counts: "
            f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}, "
            f"update_seq: {update_seq[0]} -> {update_seq[1]} -> {update_seq[2]}, "
            f"state={reads[-1][1]}"
        )
    if service_counts[0] == service_counts[1] or service_counts[1] == service_counts[2]:
        return False, f"DPLL:STAT? service count STALLED: {service_counts}"
    if update_seq[0] == update_seq[1] or update_seq[1] == update_seq[2]:
        return False, f"DPLL:STAT? update_seq STALLED: {update_seq}"
    return False, f"DPLL:STAT? counters not monotonic: service={service_counts}, update_seq={update_seq}"


def test_error_queue(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:ERR?", timeout)
    if "No error" in resp or "0" in resp:
        return True, resp
    return False, f"unexpected error: {resp}"


def test_log_stat(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:LOG:STAT?", timeout)
    if resp == "<timeout>":
        return False, "SYST:LOG:STAT? timed out"
    return True, resp


def test_trace_last(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:TRAC:LAST?", timeout)
    if resp == "<timeout>":
        return False, "SYST:TRAC:LAST? timed out"
    return True, resp


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

ALL_TESTS = [
    ("identity", test_identity),
    ("build_id", test_build_id),
    ("core_heartbeat", test_core_heartbeat),
    ("loop_status", test_loop_status),
    ("vdc_status", test_vdc_status),
    ("dpll_status", test_dpll_status),
    ("trigger_seq", test_trigger_seq),
    ("error_queue", test_error_queue),
    ("log_stat", test_log_stat),
    ("trace_last", test_trace_last),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, e.g. COM4")
    parser.add_argument("--timeout", type=float, default=5.0, help="per-command timeout")
    parser.add_argument("--settle", type=float, default=1.5,
                        help="seconds to wait after opening the port")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    parser.add_argument("--tests", nargs="*", choices=[t[0] for t in ALL_TESTS] + ["all"],
                        default=["all"], help="which tests to run (default: all)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    # resolve test list
    if "all" in args.tests:
        test_names = [t[0] for t in ALL_TESTS]
    else:
        test_names = args.tests

    # output dir
    out_dir: Path
    if args.out_dir:
        out_dir = args.out_dir.resolve()
    else:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_dir = ROOT / "build-rtos-multicore-smoke" / f"multicore_validation_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)

    # open port
    print(f"Opening {args.port} …")
    results: list[dict] = []
    passed_all = True

    with serial.Serial(args.port, 115200, timeout=0.3) as ser:
        time.sleep(args.settle)
        print(f"Port open.  Output dir: {out_dir}\n")

        try:
            for name in test_names:
                fn = dict(ALL_TESTS)[name]
                print(f"--- {name} ---")
                try:
                    ok, detail = fn(ser, args.timeout)
                except Exception as exc:
                    ok, detail = False, f"exception: {exc}"
                status = "PASS" if ok else "FAIL"
                print(f"  {status}: {detail}")
                results.append({"test": name, "passed": ok, "detail": detail})
                if not ok:
                    passed_all = False
        finally:
            try:
                ser.reset_input_buffer()
                ser.reset_output_buffer()
            except Exception:
                pass

    # summary
    passed = sum(1 for r in results if r["passed"])
    total = len(results)
    print(f"\n{'='*40}")
    print(f"Result: {passed}/{total} passed")

    summary = {
        "title": "RTOS + Multicore AMP Smoke",
        "timestamp": datetime.now().isoformat(),
        "passed": passed,
        "total": total,
        "overall": "PASS" if passed_all else "FAIL",
        "tests": results,
    }
    summary_path = out_dir / "summary.json"
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    print(f"Summary: {summary_path}")

    # copy SCPI output transcript to out_dir
    transcript_path = out_dir / "scpi_log.txt"
    with open(transcript_path, "w", encoding="utf-8") as f:
        for r in results:
            f.write(f"[{r['test']}] {'PASS' if r['passed'] else 'FAIL'}: {r['detail']}\n")
    print(f"Transcript: {transcript_path}")

    return 0 if passed_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
