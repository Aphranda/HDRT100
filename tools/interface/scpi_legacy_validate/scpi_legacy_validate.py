#!/usr/bin/env python3
"""Validate removed legacy SCPI headers return undefined-header errors."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_UNDEFINED = '-113,"Undefined header"'

COMMAND_GROUPS: dict[str, tuple[str, ...]] = {
    "status-doc-cleanup": (
        "VDC:STAT?",
        "DPLL:STAT?",
        "STATus:VDC?",
        "STATus:DPLL?",
        "STATus:LOOP?",
        "LOOP:STAT?",
        "LOOP:STATus?",
        "STATus:SYNC?",
        "STATus:TRIG?",
        "STATus:TRIGger?",
        "TRIGger:WIDTh 25",
        "TRIGger:WIDTh?",
        "TRIGger:IMMediate",
        "PULSe:WIDTh 40",
        "PULSe:WIDTh?",
        "PULSe:IMMediate",
        "MARKer:WIDTh 55",
        "MARKer:WIDTh?",
        "MARKer:IMMediate",
        "RJ45:TRIGger:WIDTh 65",
        "RJ45:TRIGger:WIDTh?",
        "RJ45:TRIGger:IMMediate",
        "RJ45:TRIGger:PINs?",
        "SAMPle:RATE 2000000",
        "SAMPle:RATE?",
        "SAMPle:STATe 1",
        "SAMPle:STATe?",
        "OUTPut:CLOCk:FREQuency 123456",
        "OUTPut:CLOCk:FREQuency?",
        "OUTPut:CLOCk:STATe 1",
        "OUTPut:CLOCk:STATe?",
        "TRIGger:SEQuence:LENGth 1",
        "TRIGger:SEQuence:LENGth?",
        "TRIGger:SEQuence:WIDTh 1",
        "TRIGger:SEQuence:WIDTh?",
        "TRIGger:SEQuence:INDex?",
        "TRIGger:SEQuence:DATA?",
        "TRIGger:PCNT:DECode 0",
        "TRIGger:PCNT:DECode?",
        "TRIGger:PCNT:DIRection 0",
        "TRIGger:PCNT:DIRection?",
        "TRIGger:PCNT:FILTer 0",
        "TRIGger:PCNT:FILTer?",
        "TRIGger:PCNT:GATE 1",
        "TRIGger:PCNT:GATE?",
        "TRIGger:PCNT:CMP 100",
        "TRIGger:PCNT:CMP?",
        "TRIGger:PCNT:PRESet 0",
        "TRIGger:PCNT:PRESet?",
        "TRIGger:PCNT:CLEar",
        "TRIGger:PCNT:TOTal?",
        "TRIGger:PCNT:FREQuency?",
        "TRIGger:ENC:TARGet 100",
        "TRIGger:ENC:TARGet?",
        "TRIGger:ENC:COUNt?",
        "TRIGger:ENC:APIN 16",
        "TRIGger:ENC:APIN?",
        "TRIGger:ENC:REVolution?",
        "CONFigure:ANGLe:BPOint",
        "CONFigure:ANGLe:BPOint:CLEAr",
        "READ:ANGLe:BPOint?",
        "CONFigure:CALibration:LINK:SET",
        "READ:T2:COUNt?",
        "READ:T2:DATA?",
        "SYSTem:PROT:STAT?",
        "SYSTem:MODE:TAB?",
        "SYSTem:RESource:TAB?",
        "SYSTem:FAULT:TAB?",
        "SYSTem:REFMEM:STATUS?",
        "SYSTem:LOOP:STAT?",
        "COMMunication:BISS:FBITs",
        "COMMunication:BISS:FBITs?",
        "COMMunication:BISS:POFFset",
        "COMMunication:BISS:POFFset?",
        "COMMunication:BISS:PBITs",
        "COMMunication:BISS:PBITs?",
        "COMMunication:BISS:PMODulo",
        "COMMunication:BISS:PMODulo?",
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="USB CDC serial port, for example COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.5, help="per-command response timeout")
    parser.add_argument("--settle", type=float, default=0.8, help="seconds to wait after opening the port")
    parser.add_argument("--group", default="status-doc-cleanup", choices=sorted(COMMAND_GROUPS))
    parser.add_argument("--commands-file", type=Path, help="UTF-8 text file with one legacy command per line")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    parser.add_argument("--list", action="store_true", help="print selected commands")
    parser.add_argument("--dry-run", action="store_true", help="print commands without opening the serial port")
    parser.add_argument("--fail-fast", action="store_true")
    return parser.parse_args()


def load_commands(args: argparse.Namespace) -> list[str]:
    if args.commands_file:
        lines = args.commands_file.read_text(encoding="utf-8").splitlines()
        return [line.strip() for line in lines if line.strip() and not line.lstrip().startswith("#")]
    return list(COMMAND_GROUPS[args.group])


def read_serial_line(ser: serial.Serial, deadline: float) -> str | None:
    raw = bytearray()
    while time.monotonic() < deadline:
        ch = ser.read(1)
        if not ch:
            continue
        raw.extend(ch)
        if ch == b"\n":
            break
    if not raw:
        return None
    return bytes(raw).decode("utf-8", errors="replace").strip()


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"[') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_response(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_serial_line(ser, deadline)
        if line is None or is_log_line(line):
            continue
        return line
    return "<timeout>"


def send_command(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_response(ser, timeout_s)


def write_outputs(out_dir: Path, records: list[dict[str, object]], summary: dict[str, object]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps({**summary, "records": records}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    with (out_dir / "transcript.txt").open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# legacy SCPI validation {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"> {record['command']}\n")
            handle.write(f"< {record['response']}\n")
            handle.write(f"! {record['error']}\n")
            handle.write(f"# {record['status']}\n")
    (out_dir / "summary.txt").write_text(
        "\n".join(
            (
                f"passed={summary['passed']}",
                f"total={summary['total']}",
                f"failed={summary['failed']}",
                f"group={summary['group']}",
                f"out_dir={out_dir}",
            )
        )
        + "\n",
        encoding="utf-8",
    )


def run(args: argparse.Namespace) -> int:
    commands = load_commands(args)
    if args.list or args.dry_run:
        for command in commands:
            print(command)
    if args.dry_run:
        print(f"generated={len(commands)}")
        return 0
    if not args.port:
        raise SystemExit("port is required unless --dry-run is used")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.out_dir or (ROOT / "build" / f"legacy_scpi_validation_{stamp}")
    records: list[dict[str, object]] = []
    failures = 0

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        send_command(ser, "*CLS", args.timeout)
        for command in commands:
            response = send_command(ser, command, args.timeout)
            error = send_command(ser, "SYSTem:ERRor?", args.timeout)
            ok = error == EXPECTED_UNDEFINED
            failures += 0 if ok else 1
            status = "PASS" if ok else "FAIL"
            records.append(
                {
                    "command": command,
                    "response": response,
                    "error": error,
                    "expected_error": EXPECTED_UNDEFINED,
                    "status": status,
                }
            )
            print(f"{status} {command} => {response} | ERR {error}")
            if args.fail_fast and not ok:
                break
            time.sleep(0.03)
        final_error = send_command(ser, "SYSTem:ERRor?", args.timeout)
        print(f"FINAL_ERR {final_error}")

    summary = {
        "passed": failures == 0,
        "total": len(records),
        "failed": failures,
        "group": args.group,
        "port": args.port,
        "final_error": final_error,
        "out_dir": str(out_dir),
    }
    write_outputs(out_dir, records, summary)
    print(f"summary: passed={summary['passed']} failed={failures} total={len(records)} out_dir={out_dir}")
    return 0 if failures == 0 else 1


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
