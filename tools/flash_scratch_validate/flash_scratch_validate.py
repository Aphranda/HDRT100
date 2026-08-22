#!/usr/bin/env python3
"""Run the validation-only Scratch transaction on a DHRT100 board.

The tool deliberately discovers the RP2350 USB CDC device instead of making a
serial-port name part of the board identity.  It records the complete SCPI
diagnostic context and refuses to report success unless the pattern hash and
post-restore erased check both pass.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


BOARD = "DHRT100"
USB_VID = 0x2E8A
USB_PID = 0x0009
CONFIRM_TOKEN = 0x53435254


def discover_port() -> str:
    candidates = [
        port.device
        for port in list_ports.comports()
        if port.vid == USB_VID and port.pid == USB_PID
    ]
    if len(candidates) != 1:
        raise RuntimeError(
            f"expected exactly one {BOARD} USB CDC device, found {len(candidates)}"
        )
    return candidates[0]


def is_log_line(line: str) -> bool:
    maybe_log = line[1:] if line.startswith('"') else line
    return not line or maybe_log.startswith("[") or maybe_log.startswith("log:")


def read_response(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = bytearray()
        while time.monotonic() < deadline:
            chunk = ser.read(1)
            if not chunk:
                continue
            raw.extend(chunk)
            if chunk == b"\n":
                break
        if not raw:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        if is_log_line(line) or line in {"OK", '"OK"'}:
            continue
        return line
    raise TimeoutError("SCPI response timeout")


def issue(ser: serial.Serial, command: str, timeout_s: float) -> str:
    last_error: TimeoutError | None = None
    for _attempt in range(3):
        ser.reset_input_buffer()
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        try:
            return read_response(ser, timeout_s)
        except TimeoutError as exc:
            last_error = exc
            time.sleep(0.2)
    assert last_error is not None
    raise last_error


def parse_validation(response: str) -> dict[str, int | str]:
    values = [part.strip() for part in response.split(",")]
    if len(values) != 7:
        raise ValueError(f"validation response has {len(values)} fields: {response!r}")
    try:
        parsed = [int(value, 0) for value in values]
    except ValueError as exc:
        raise ValueError(f"validation response is not numeric: {response!r}") from exc
    names = (
        "erase_ok",
        "program_ok",
        "expected_hash",
        "readback_hash",
        "hash_match",
        "restore_ok",
        "erased_ok",
    )
    return dict(zip(names, parsed, strict=True)) | {"raw": response}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", default=BOARD, choices=[BOARD])
    parser.add_argument("--port", help="override automatic DHRT100 USB discovery")
    parser.add_argument("--pattern", type=int, choices=[0, 1], default=0)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--out-dir", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    port = args.port or discover_port()
    out_dir = args.out_dir or Path("build") / (
        "dhrt100_flash_scratch_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    commands = {
        "identity": "*IDN?",
        "build": "SYST:FW:BUILD?",
        "slot": "SYST:OTA:SLOT?",
        # Diagnostics expose the generated target-map symbol; the destructive
        # transaction itself remains constrained to the deployed compatibility
        # Scratch intent in FlashTransactionFB.
        "flash_map": "SYSTem:DIAGnostic:FLASh:MAP? 12",
        "flash_access": "SYSTem:DIAGnostic:FLASh:ACCEss? 12,1,1,1,1,0,4096",
        "sensors_before": "SYSTem:DIAGnostic:SENSors?",
    }
    responses: dict[str, str] = {}
    with serial.Serial(port, 115200, timeout=0.2, write_timeout=args.timeout) as ser:
        time.sleep(args.settle)
        ser.reset_input_buffer()
        for name, command in commands.items():
            try:
                responses[name] = issue(ser, command, args.timeout)
            except TimeoutError as exc:
                raise TimeoutError(f"{name} ({command}) response timeout") from exc
        validation_command = (
            f"SYSTem:DIAGnostic:FLASh:VALidate {CONFIRM_TOKEN},{args.pattern}"
        )
        try:
            responses["validation"] = issue(ser, validation_command, args.timeout)
            responses["sensors_after"] = issue(ser, commands["sensors_before"], args.timeout)
            responses["error"] = issue(ser, "SYSTem:ERRor?", args.timeout)
            responses["slot_after"] = issue(ser, "SYST:OTA:SLOT?", args.timeout)
        except TimeoutError as exc:
            raise TimeoutError("validation or post-check response timeout") from exc

    validation = parse_validation(responses["validation"])
    success = all(
        validation[key] == 1
        for key in ("erase_ok", "program_ok", "hash_match", "restore_ok", "erased_ok")
    )
    report = {
        "board": args.board,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "pattern_id": args.pattern,
        "confirm_token": hex(CONFIRM_TOKEN),
        "commands": commands | {"validation": validation_command},
        "responses": responses,
        "validation": validation,
        "success": success,
        "notes": [
            "Scratch uses the deployed compatibility map; no v2 high-address write is attempted.",
            "success requires hash_match and erased_ok after restore.",
        ],
    }
    report_path = out_dir / "flash_scratch_validation.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
