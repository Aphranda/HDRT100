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
ROOT = Path(__file__).resolve().parents[2]
USB_VID = 0x2E8A
USB_PID = 0x0009
CONFIRM_TOKEN = 0x53435254


def v2_scratch_contract() -> dict[str, int | str]:
    manifest = json.loads(
        (ROOT / "config/flash_map_gen/flash_map_v2_manifest.json").read_text(
            encoding="utf-8"
        )
    )
    partitions = manifest["partitions"]
    for partition_id, partition in enumerate(partitions):
        if partition["id"] == "SCRATCH":
            return {
                "map_version": manifest["map_version"],
                "deployment_state": manifest["deployment_state"],
                "partition_id": partition_id,
                "offset": partition["offset"],
                "size": partition["size"],
            }
    raise ValueError("v2 manifest has no SCRATCH partition")


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


def parse_flash_map(response: str) -> dict[str, int | str]:
    values = [part.strip().strip('"') for part in response.split(",")]
    if len(values) != 11:
        raise ValueError(f"flash map response has {len(values)} fields: {response!r}")
    return {
        "map_version": int(values[0], 0),
        "deployment_state": values[1],
        "partition_count": int(values[2], 0),
        "partition_id": int(values[3], 0),
        "offset": int(values[4], 0),
        "size": int(values[5], 0),
        "raw": response,
    }


def parse_jedec(response: str) -> dict[str, int | str]:
    values = [part.strip().strip('"') for part in response.split(",")]
    if len(values) != 9:
        raise ValueError(f"JEDEC response has {len(values)} fields: {response!r}")
    return {
        "valid": int(values[0], 0),
        "source": values[1],
        "raw_id": int(values[2], 0),
        "manufacturer_id": int(values[3], 0),
        "memory_type": int(values[4], 0),
        "capacity_code": int(values[5], 0),
        "capacity_bytes": int(values[6], 0),
        "configured_bytes": int(values[7], 0),
        "capacity_matches_geometry": int(values[8], 0),
        "raw": response,
    }


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
    scratch = v2_scratch_contract()
    out_dir = args.out_dir or Path("build") / (
        "dhrt100_flash_scratch_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    commands = {
        "identity": "*IDN?",
        "build": "SYST:FW:BUILD?",
        "slot": "SYST:OTA:SLOT?",
        # Diagnostics expose the generated target-map symbol; the destructive
        # transaction remains constrained to that selected deployment map by
        # FlashTransactionFB.
        "flash_jedec": "SYSTem:DIAGnostic:FLASh:JEDEC?",
        "flash_map": (
            "SYSTem:DIAGnostic:FLASh:MAP? "
            f"{scratch['partition_id']}"
        ),
        "flash_access": (
            "SYSTem:DIAGnostic:FLASh:ACCEss? "
            f"{scratch['partition_id']},1,1,1,1,0,4096"
        ),
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
        flash_map = parse_flash_map(responses["flash_map"])
        jedec = parse_jedec(responses["flash_jedec"])
        high_address_end = int(scratch["offset"]) + 4096
        if (
            flash_map["map_version"] != scratch["map_version"]
            or flash_map["deployment_state"] != scratch["deployment_state"]
            or flash_map["partition_id"] != scratch["partition_id"]
            or flash_map["offset"] != scratch["offset"]
            or flash_map["size"] != scratch["size"]
            or int(scratch["size"]) < 4096
            or jedec["valid"] != 1
            or jedec["source"] != "JEDEC_RDID_9F"
            or jedec["capacity_matches_geometry"] != 1
            or int(jedec["capacity_bytes"]) < high_address_end
            or responses["flash_access"].split(",", 1)[0].strip() != "1"
        ):
            raise RuntimeError(
                "v2 physical Scratch window rejected by map/JEDEC/access preflight"
            )
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
        "v2_scratch_contract": scratch,
        "flash_map": flash_map,
        "jedec": jedec,
        "success": success,
        "notes": [
            "Scratch uses the selected deployment map; v2 candidate validation covers the high-address target_not_deployed map.",
            "success requires hash_match and erased_ok after restore.",
        ],
    }
    report_path = out_dir / "flash_scratch_validation.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
