#!/usr/bin/env python3
"""Read diagnostic UART log output for a bounded time window.

This tool is intentionally read-only: it opens the UART, drains text lines, and
never writes to the device.  Use SCPI tools over USB CDC/USBTMC to enable or
disable the producer that emits these logs.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


@dataclass
class LogRecord:
    timestamp: str
    line: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="UART serial port, for example COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=10.0,
                        help="seconds to monitor before exiting")
    parser.add_argument("--settle", type=float, default=0.2,
                        help="seconds to wait after opening the port")
    parser.add_argument("--contains", action="append", default=[],
                        help="substring that must appear at least once; may repeat")
    parser.add_argument("--out", type=Path, help="write UTF-8 transcript")
    parser.add_argument("--json", action="store_true",
                        help="print JSON records instead of raw lines")
    return parser.parse_args()


def read_available_line(ser: serial.Serial, buffer: bytearray) -> str | None:
    data = ser.read(max(1, ser.in_waiting))
    if data:
        buffer.extend(data)

    for separator in (b"\n", b"\r"):
        index = buffer.find(separator)
        if index >= 0:
            raw = bytes(buffer[:index])
            del buffer[:index + 1]
            while buffer[:1] in (b"\r", b"\n"):
                del buffer[:1]
            return raw.decode("utf-8", errors="replace").strip()
    return None


def write_transcript(path: Path, records: list[LogRecord]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"# uart_log_monitor transcript {datetime.now().isoformat(timespec='seconds')}\n")
        for record in records:
            handle.write(f"[{record.timestamp}] {record.line}\n")


def main() -> int:
    args = parse_args()
    records: list[LogRecord] = []
    required = list(args.contains)

    with serial.Serial(args.port,
                       args.baud,
                       timeout=0.05,
                       write_timeout=0) as ser:
        time.sleep(args.settle)
        ser.reset_input_buffer()
        deadline = time.monotonic() + args.duration
        buffer = bytearray()
        while time.monotonic() < deadline:
            line = read_available_line(ser, buffer)
            if line is None:
                continue
            timestamp = datetime.now().isoformat(timespec="milliseconds")
            record = LogRecord(timestamp=timestamp, line=line)
            records.append(record)
            if args.json:
                print(json.dumps(asdict(record), ensure_ascii=False), flush=True)
            else:
                print(line, flush=True)

    if buffer:
        line = bytes(buffer).decode("utf-8", errors="replace").strip()
        if line:
            record = LogRecord(datetime.now().isoformat(timespec="milliseconds"), line)
            records.append(record)
            if args.json:
                print(json.dumps(asdict(record), ensure_ascii=False), flush=True)
            else:
                print(line, flush=True)

    if args.out is not None:
        write_transcript(args.out, records)

    missing = [needle for needle in required
               if not any(needle in record.line for record in records)]
    if missing:
        print(f"missing required substring(s): {', '.join(missing)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
