#!/usr/bin/env python3
"""Validate two minimal-system boards for RefMem/VDC network bring-up.

This tool is intentionally conservative. It does not configure hardware and it
does not assume RJ45 CLAIM_* coordination is implemented yet. It opens two SCPI
serial sessions with explicit lifecycle management, reads baseline snapshots,
and checks that both boards are ready for the later two-board network tests.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from contextlib import contextmanager
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterator

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]


@dataclass
class BoardSnapshot:
    name: str
    port: str
    idn: str
    build_id: str
    core: list[int]
    vdc: list[int]
    dpll: list[int]
    config: list[int]
    claim0: list[int]
    claim_evidence0: list[int]
    passed: bool
    failures: list[str]


def read_serial_line(ser: serial.Serial, timeout_s: float) -> str:
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
    return read_serial_line(ser, timeout_s)


def parse_ints(response: str) -> list[int]:
    fields: list[int] = []
    for part in response.split(","):
        try:
            fields.append(int(part.strip()))
        except ValueError:
            pass
    return fields


@contextmanager
def open_board(port: str, baud: int, timeout_s: float, settle_s: float) -> Iterator[serial.Serial]:
    ser = serial.Serial(port, baud, timeout=0.1, write_timeout=timeout_s)
    try:
        time.sleep(settle_s)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        yield ser
    finally:
        try:
            ser.flush()
        finally:
            ser.close()


def expect(condition: bool, failures: list[str], message: str) -> None:
    if not condition:
        failures.append(message)


def read_board_snapshot(name: str,
                        port: str,
                        ser: serial.Serial,
                        timeout_s: float) -> BoardSnapshot:
    failures: list[str] = []
    idn = query(ser, "*IDN?", timeout_s)
    build_id = query(ser, "SYST:FW:BUILD?", timeout_s).strip('"')
    core = parse_ints(query(ser, "SYST:CORE?", timeout_s))
    vdc = parse_ints(query(ser, "SYST:SYNC:VDC:STAT?", timeout_s))
    dpll = parse_ints(query(ser, "SYST:SYNC:VDC:DPLL:STAT?", timeout_s))
    config = parse_ints(query(ser, "SYST:CONFigure:STAT?", timeout_s))
    claim0 = parse_ints(query(ser, "SYST:REFMEM:CLAIM? 0", timeout_s))
    claim_evidence0 = parse_ints(query(ser, "SYST:REFMEM:CLAIM:EVIDence? 0", timeout_s))

    expect(idn != "<timeout>" and ("GTS" in idn or "DHRT100" in idn or "RP2350_TRIG" in idn),
           failures,
           f"{name}: unexpected *IDN? response {idn!r}")
    expect(len(build_id) >= 8, failures, f"{name}: build id is empty or malformed: {build_id!r}")
    expect(len(core) >= 5 and core[0] == 1, failures, f"{name}: core1 not ready: {core}")
    expect(len(vdc) >= 6 and vdc[0] == 1, failures, f"{name}: VDC baseline not ready: {vdc}")
    expect(len(dpll) >= 6 and dpll[0] == 1, failures, f"{name}: DPLL task not ready: {dpll}")
    expect(len(config) >= 24 and config[0] == 1 and config[1] == 1,
           failures,
           f"{name}: config gate not ready: {config}")
    expect(len(claim0) >= 30 and claim0[0] == 1 and claim0[2] == 8 and claim0[9] == 1,
           failures,
           f"{name}: SlotClaim gate not ready: {claim0}")
    expect(len(claim_evidence0) >= 16 and claim_evidence0[2] == 0,
           failures,
           f"{name}: default SlotClaim evidence is not empty: {claim_evidence0}")

    return BoardSnapshot(
        name=name,
        port=port,
        idn=idn,
        build_id=build_id,
        core=core,
        vdc=vdc,
        dpll=dpll,
        config=config,
        claim0=claim0,
        claim_evidence0=claim_evidence0,
        passed=len(failures) == 0,
        failures=failures,
    )


def compare_boards(a: BoardSnapshot,
                   b: BoardSnapshot,
                   *,
                   allow_build_mismatch: bool,
                   allow_map_mismatch: bool) -> list[str]:
    failures: list[str] = []
    if not allow_build_mismatch:
        expect(a.build_id == b.build_id,
               failures,
               f"build mismatch: {a.name}={a.build_id}, {b.name}={b.build_id}")
    if not allow_map_mismatch and len(a.claim0) >= 9 and len(b.claim0) >= 9:
        expect(a.claim0[8] == b.claim0[8],
               failures,
               f"SlotClaimMap CRC mismatch: {a.name}={a.claim0[8]}, {b.name}={b.claim0[8]}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-a", required=True, help="first board serial port, for example COM4")
    parser.add_argument("--port-b", required=True, help="second board serial port, for example COM7")
    parser.add_argument("--name-a", default="B0", help="label for the first board")
    parser.add_argument("--name-b", default="B1", help="label for the second board")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=1.5)
    parser.add_argument("--allow-build-mismatch", action="store_true")
    parser.add_argument("--allow-map-mismatch", action="store_true")
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    if args.port_a == args.port_b:
        raise SystemExit("--port-a and --port-b must be different serial ports")

    started = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or ROOT / "build-rtos-multicore-smoke" / f"refmem_network_{started}"
    out_dir.mkdir(parents=True, exist_ok=True)

    with open_board(args.port_a, args.baud, args.timeout, args.settle) as ser_a:
        snap_a = read_board_snapshot(args.name_a, args.port_a, ser_a, args.timeout)
    with open_board(args.port_b, args.baud, args.timeout, args.settle) as ser_b:
        snap_b = read_board_snapshot(args.name_b, args.port_b, ser_b, args.timeout)

    compare_failures = compare_boards(snap_a,
                                      snap_b,
                                      allow_build_mismatch=args.allow_build_mismatch,
                                      allow_map_mismatch=args.allow_map_mismatch)
    summary = {
        "started": started,
        "ports": [args.port_a, args.port_b],
        "boards": [asdict(snap_a), asdict(snap_b)],
        "compare_failures": compare_failures,
        "passed": snap_a.passed and snap_b.passed and not compare_failures,
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    lines = [
        f"RefMem network baseline validation: {'PASS' if summary['passed'] else 'FAIL'}",
        f"{snap_a.name} {snap_a.port}: {'PASS' if snap_a.passed else 'FAIL'} build={snap_a.build_id}",
        f"{snap_b.name} {snap_b.port}: {'PASS' if snap_b.passed else 'FAIL'} build={snap_b.build_id}",
    ]
    if len(snap_a.claim0) >= 9 and len(snap_b.claim0) >= 9:
        lines.append(f"SlotClaimMap CRC: {snap_a.name}={snap_a.claim0[8]} {snap_b.name}={snap_b.claim0[8]}")
    for failure in snap_a.failures + snap_b.failures + compare_failures:
        lines.append(f"FAIL: {failure}")
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    for line in lines:
        print(line)
    print(f"summary={out_dir}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
