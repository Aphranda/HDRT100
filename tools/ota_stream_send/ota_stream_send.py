#!/usr/bin/env python3
"""Send a slot-specific DHRT100 image through the HAOFV local stream ingress."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import struct
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


SOURCE_USB_CDC = 0
STREAM_CAPABILITIES = (1 << 0) | (1 << 1)
STREAM_OPEN_WIRE_FORMAT = "<9IB3x16s32s"
STREAM_OPEN_WIRE_SIZE = 88
STREAM_STATE_OPEN = 1
STREAM_STATE_RECEIVING = 2
STREAM_STATE_READY_TO_REBOOT = 3
STREAM_STATE_FAILED = 5
DEFAULT_BLOCK_SIZE = 512
ROOT = Path(__file__).resolve().parents[2]


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def scpi_block(data: bytes) -> bytes:
    length = str(len(data)).encode("ascii")
    return b"#" + str(len(length)).encode("ascii") + length + data


def read_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line or line.startswith("[") or line.startswith("progress=["):
            continue
        return line
    return "<timeout>"


def command(ser: serial.Serial, payload: bytes, timeout_s: float) -> str:
    ser.write(payload + b"\n")
    ser.flush()
    return read_line(ser, timeout_s)


def query(ser: serial.Serial, text: str, timeout_s: float) -> str:
    return command(ser, text.encode("ascii"), timeout_s)


def parse_first_uint(response: str) -> int:
    return int(response.split(",", 1)[0].strip(), 0)


def parse_stream_status(response: str) -> tuple[int, int, int, int, int]:
    fields = [int(field.strip(), 0) for field in response.split(",")]
    if len(fields) != 5:
        raise ValueError(f"invalid stream status: {response!r}")
    return tuple(fields)  # type: ignore[return-value]


def parse_journal_status(response: str) -> tuple[int, ...]:
    fields = tuple(int(field.strip(), 0) for field in response.split(","))
    if len(fields) != 13:
        raise ValueError(f"invalid journal status: {response!r}")
    return fields


def app_partition_id(map_version: int, target_slot: int) -> int:
    source = ROOT / "config" / f"flash_map_v{map_version}.json"
    if map_version == 1:
        source = ROOT / "config" / "flash_map_v1_compat.json"
    data = json.loads(source.read_text(encoding="utf-8"))
    wanted = "APP_A" if target_slot == 1 else "APP_B"
    for numeric_id, partition in enumerate(data["partitions"]):
        if partition["id"] == wanted:
            return numeric_id
    raise ValueError(f"{wanted} missing from {source}")


def wait_stream_state(ser: serial.Serial, wanted: int,
                      timeout_s: float) -> tuple[int, int, int, int, int]:
    deadline = time.monotonic() + timeout_s
    last = ""
    while time.monotonic() < deadline:
        last = query(ser, "SYST:OTA:STREAM:STAT?", timeout_s)
        status = parse_stream_status(last)
        if status[1] == wanted:
            return status
        if status[1] == STREAM_STATE_FAILED:
            raise RuntimeError(f"stream failed while waiting for {wanted}: {last!r}")
        time.sleep(0.05)
    raise TimeoutError(f"stream state {wanted} timeout, last={last!r}")


def encode_open(identity: bytes, image: bytes, target_slot: int,
                session_id: int, generation: int, map_version: int,
                partition_id: int,
                source: int) -> bytes:
    if len(identity) != 16:
        raise ValueError("identity must be 16 bytes")
    if target_slot not in (1, 2):
        raise ValueError("target slot must be 1 or 2")
    if source != SOURCE_USB_CDC:
        raise ValueError("serial stream tool requires USB CDC source")
    if map_version <= 0:
        raise ValueError("map version must be positive")
    wire = struct.pack(
        STREAM_OPEN_WIRE_FORMAT,
        session_id,
        generation,
        STREAM_CAPABILITIES,
        map_version,
        partition_id,
        target_slot,
        1,
        len(image),
        crc32(image),
        0,
        identity,
        hashlib.sha256(image).digest(),
    )
    if len(wire) != STREAM_OPEN_WIRE_SIZE:
        raise AssertionError("stream OPEN wire size mismatch")
    return wire


def identify_port(port: str, baud: int, timeout_s: float) -> str | None:
    try:
        with serial.Serial(port, baud, timeout=0.1,
                           write_timeout=timeout_s) as ser:
            time.sleep(0.15)
            ser.reset_input_buffer()
            response = query(ser, "*IDN?", timeout_s)
            return response if "DHRT100" in response.upper() else None
    except (OSError, serial.SerialException):
        return None


def resolve_dhrt100_port(requested: str | None, baud: int,
                         timeout_s: float) -> tuple[str, str]:
    candidates = [requested] if requested else [item.device for item in list_ports.comports()]
    matches: list[tuple[str, str]] = []
    for port in candidates:
        if not port:
            continue
        identity = identify_port(port, baud, timeout_s)
        if identity is not None:
            matches.append((port, identity))
    if len(matches) != 1:
        raise RuntimeError(f"expected one DHRT100, found {len(matches)}")
    return matches[0]


def wait_reenumeration(port: str, baud: int, timeout_s: float) -> serial.Serial:
    deadline = time.monotonic() + max(timeout_s, 20.0)
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            ser = serial.Serial(port, baud, timeout=0.1,
                                write_timeout=timeout_s)
            time.sleep(0.2)
            ser.reset_input_buffer()
            if "DHRT100" in query(ser, "*IDN?", timeout_s).upper():
                return ser
            ser.close()
        except (OSError, serial.SerialException) as exc:
            last_error = exc
        time.sleep(0.25)
    raise TimeoutError(f"DHRT100 did not re-enumerate: {last_error}")


def send(args: argparse.Namespace) -> int:
    port, idn = resolve_dhrt100_port(args.port, args.baud, args.timeout)
    print(f"board=DHRT100 idn={idn}")
    print(f"connection={port}")

    with serial.Serial(port, args.baud, timeout=0.1,
                       write_timeout=args.timeout) as ser:
        ser.reset_input_buffer()
        target_slot = parse_first_uint(query(ser, "SYST:OTA:TARG?", args.timeout))
        if target_slot not in (1, 2):
            raise ValueError(f"unsupported target slot {target_slot}")
        image_path = args.image
        if args.image_a is not None and args.image_b is not None:
            image_path = args.image_a if target_slot == 1 else args.image_b
        if image_path is None:
            raise ValueError("no image selected")
        image = image_path.read_bytes()
        if not image:
            raise ValueError("image is empty")
        identity = hashlib.sha256(idn.encode("utf-8")).digest()[:16]
        map_version = parse_first_uint(query(
            ser, "SYST:DIAG:FLASH:MAP? 0", args.timeout))
        partition_id = app_partition_id(map_version, target_slot)
        if args.resume:
            journal = parse_journal_status(query(
                ser, "SYST:OTA:JOUR?", args.timeout))
            if journal[0] != 1 or journal[8] != len(image) or \
                    journal[9] != crc32(image) or journal[12] != 0:
                raise RuntimeError(
                    "journal does not match the selected image")
            session_id = journal[3]
            generation = journal[4]
        else:
            session_id = args.session_id or (int(time.time()) & 0xFFFFFFFF or 1)
            generation = args.generation or session_id
        wire = encode_open(identity, image, target_slot, session_id,
                           generation, map_version, partition_id,
                           SOURCE_USB_CDC)
        if args.resume and (journal[6] != 1 or journal[5] != crc32(wire)):
            raise RuntimeError(
                "journal identity token does not match the selected image/map")
        print(f"target_slot={target_slot} image={image_path} size={len(image)} "
              f"crc32=0x{crc32(image):08X} map_version={map_version} "
              f"partition_id={partition_id} session_id={session_id} "
              f"generation={generation}")
        if args.dry_run:
            return 0

        response = command(
            ser,
            f"SYST:OTA:STREAM:OPEN {SOURCE_USB_CDC},".encode("ascii") +
            scpi_block(wire),
            args.timeout,
        )
        if "OK" not in response:
            raise RuntimeError(f"stream OPEN rejected: {response!r}")
        status = wait_stream_state(ser, STREAM_STATE_RECEIVING,
                                   args.begin_timeout)
        resume_offset = status[2]
        if resume_offset > len(image) or (
                resume_offset != len(image) and
                resume_offset % args.block_size != 0):
            raise RuntimeError(
                f"invalid durable offset {resume_offset} for block size "
                f"{args.block_size}")
        print(f"resume_offset={resume_offset}")

        for offset in range(resume_offset, len(image), args.block_size):
            chunk = image[offset:offset + args.block_size]
            response = command(
                ser,
                (f"SYST:OTA:STREAM:DATA {SOURCE_USB_CDC},{offset},"
                 f"{crc32(chunk)},").encode("ascii") + scpi_block(chunk),
                args.timeout,
            )
            if "OK" not in response:
                raise RuntimeError(f"stream DATA rejected at {offset}: {response!r}")
            if offset == 0 or offset + len(chunk) == len(image) or \
                    (offset // args.block_size) % args.progress_every == 0:
                status = parse_stream_status(query(
                    ser, "SYST:OTA:STREAM:STAT?", args.timeout))
                print(f"durable_offset={status[2]} token={status[3]}")

        response = query(ser, f"SYST:OTA:STREAM:CLOSE {SOURCE_USB_CDC}", args.timeout)
        if "OK" not in response:
            raise RuntimeError(f"stream CLOSE rejected: {response!r}")
        status = wait_stream_state(ser, STREAM_STATE_READY_TO_REBOOT,
                                   args.begin_timeout)
        print(f"ready durable_offset={status[2]} token={status[3]}")
        if not args.boot_and_commit:
            return 0
        try:
            query(ser, "SYST:OTA:STREAM:BOOT", args.timeout)
        except (OSError, serial.SerialException):
            pass

    with wait_reenumeration(port, args.baud, args.timeout) as ser:
        slot_before = query(ser, "SYST:OTA:SLOT?", args.timeout)
        response = query(ser, "SYST:OTA:COMM", args.timeout)
        if "OK" not in response:
            raise RuntimeError(f"commit rejected: {response!r}")
        slot_after = query(ser, "SYST:OTA:SLOT?", args.timeout)
        print(f"post_boot_slot={slot_before}")
        print(f"committed_slot={slot_after}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", nargs="?", type=Path,
                        help="slot-specific raw image; omit when both slot artifacts are supplied")
    parser.add_argument("--port", help="optional serial connection; identity is still verified with *IDN?")
    parser.add_argument("--image-a", type=Path, help="expected Slot A artifact")
    parser.add_argument("--image-b", type=Path, help="expected Slot B artifact")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--block-size", type=int, choices=(256, 512),
                        default=DEFAULT_BLOCK_SIZE)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--begin-timeout", type=float, default=60.0)
    parser.add_argument("--progress-every", type=int, default=16)
    parser.add_argument("--session-id", type=int,
                        help="stable nonzero session identity for manual resume")
    parser.add_argument("--generation", type=int,
                        help="stable nonzero generation for manual resume")
    parser.add_argument("--resume", action="store_true",
                        help="reuse session/generation from SYST:OTA:JOUR?")
    parser.add_argument("--boot-and-commit", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.image is None and (args.image_a is None or args.image_b is None):
        parser.error("provide image or both --image-a and --image-b")
    if args.progress_every <= 0:
        parser.error("--progress-every must be positive")
    if args.resume and (args.session_id is not None or args.generation is not None):
        parser.error("--resume cannot be combined with explicit session identity")
    if args.session_id is not None and not 0 < args.session_id <= 0xFFFFFFFF:
        parser.error("--session-id must fit a nonzero uint32")
    if args.generation is not None and not 0 < args.generation <= 0xFFFFFFFF:
        parser.error("--generation must fit a nonzero uint32")
    return args


def main() -> int:
    try:
        return send(parse_args())
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
