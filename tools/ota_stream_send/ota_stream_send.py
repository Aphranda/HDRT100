#!/usr/bin/env python3
"""Send a DHRT100 image or package through the HAOFV local stream ingress."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import struct
import sys
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.ota_timing import OtaTiming  # noqa: E402

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
DEFAULT_BLOCK_SIZE = 4096
LOCAL_MAX_BLOCK_SIZE = 4096
COMPATIBILITY_BLOCK_SIZE = 512
BLOCK_SIZE_GRANULARITY = 256
SUPPORTED_BLOCK_SIZES = (256, 512, 1024, 2048, 4096)
DEFAULT_DEPLOYMENT_MAP_VERSION = 1
PACKAGE_HEADER_SIZE = 512
PACKAGE_MAGIC = 0x474B5054
PACKAGE_VERSION = 2
PACKAGE_IMAGE_TABLE_OFFSET = 192
PACKAGE_IMAGE_ENTRY_SIZE = 32
PACKAGE_MAX_IMAGES = 2


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def scpi_block(data: bytes) -> bytes:
    length = str(len(data)).encode("ascii")
    return b"#" + str(len(length)).encode("ascii") + length + data


def stream_chunks(data: bytes, start: int, block_size: int,
                  package_mode: bool) -> list[tuple[int, bytes]]:
    if start < 0 or start > len(data) or block_size <= 0:
        raise ValueError("invalid stream chunk range")
    boundaries = {start, len(data)}
    if package_mode:
        if len(data) < PACKAGE_HEADER_SIZE:
            raise ValueError("package is smaller than its manifest header")
        boundaries.add(PACKAGE_HEADER_SIZE)
    ordered = sorted(boundary for boundary in boundaries if boundary >= start)
    chunks: list[tuple[int, bytes]] = []
    offset = start
    for boundary in ordered:
        while offset < boundary:
            end = min(offset + block_size, boundary)
            chunks.append((offset, data[offset:end]))
            offset = end
    return chunks


def selected_package_object(package: bytes, target_slot: int) -> bytes:
    if len(package) < PACKAGE_HEADER_SIZE or target_slot not in (1, 2):
        raise ValueError("invalid package or target slot")
    magic, version, header_size, package_size, _, image_count = \
        struct.unpack_from("<6I", package, 0)
    if magic != PACKAGE_MAGIC or version != PACKAGE_VERSION or \
            header_size != PACKAGE_HEADER_SIZE or package_size != len(package) or \
            image_count == 0 or image_count > PACKAGE_MAX_IMAGES:
        raise ValueError("invalid package header")
    selected: bytes | None = None
    for index in range(image_count):
        cursor = PACKAGE_IMAGE_TABLE_OFFSET + index * PACKAGE_IMAGE_ENTRY_SIZE
        slot, image_offset, image_size = struct.unpack_from("<3I", package,
                                                            cursor)
        image_end = image_offset + image_size
        if slot not in (1, 2) or image_size == 0 or \
                image_offset < PACKAGE_HEADER_SIZE or image_end > len(package):
            raise ValueError("invalid package image range")
        if slot == target_slot:
            if selected is not None:
                raise ValueError("duplicate target image")
            selected = package[image_offset:image_end]
    if selected is None:
        raise ValueError("target image missing from package")
    return package[:PACKAGE_HEADER_SIZE] + selected


def read_line(ser: serial.Serial, timeout_s: float,
              received_lines: list[str] | None = None) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if line and received_lines is not None:
            received_lines.append(line)
        if not line or line.startswith("[") or line.startswith("progress=["):
            continue
        return line
    return "<timeout>"


def command(ser: serial.Serial, payload: bytes, timeout_s: float, *,
            timing: OtaTiming | None = None, stage: str = "scpi_command",
            request: str | None = None,
            details: dict[str, Any] | None = None) -> str:
    started = time.monotonic()
    received_lines: list[str] = []
    response = "<transport-error>"
    try:
        ser.write(payload + b"\n")
        ser.flush()
        response = read_line(ser, timeout_s, received_lines)
        return response
    finally:
        if timing is not None:
            fields: dict[str, Any] = {
                "request": request or payload[:80].decode(
                    "ascii", errors="replace"),
                "payload_bytes": len(payload),
                "response": response,
                "received_lines": received_lines,
            }
            fields.update(details or {})
            timing.record(stage, time.monotonic() - started, **fields)


def query(ser: serial.Serial, text: str, timeout_s: float, *,
          timing: OtaTiming | None = None, stage: str = "scpi_query",
          details: dict[str, Any] | None = None) -> str:
    return command(ser, text.encode("ascii"), timeout_s, timing=timing,
                   stage=stage, request=text, details=details)


def parse_first_uint(response: str) -> int:
    return int(response.split(",", 1)[0].strip(), 0)


def parse_stream_status(response: str) -> tuple[int, int, int, int, int]:
    fields = [int(field.strip(), 0) for field in response.split(",")]
    if len(fields) != 5:
        raise ValueError(f"invalid stream status: {response!r}")
    return tuple(fields)  # type: ignore[return-value]


def parse_stream_capability(response: str) -> tuple[int, int, int]:
    fields = tuple(int(field.strip(), 0) for field in response.split(","))
    if len(fields) != 3 or fields[0] not in SUPPORTED_BLOCK_SIZES or \
            fields[0] > LOCAL_MAX_BLOCK_SIZE or fields[2] not in (1, 2):
        raise ValueError(f"invalid stream capability: {response!r}")
    return fields  # type: ignore[return-value]


def effective_block_size(requested: int, reported_max: int) -> int:
    if requested not in SUPPORTED_BLOCK_SIZES:
        raise ValueError(
            "block size must be one of "
            f"{', '.join(str(size) for size in SUPPORTED_BLOCK_SIZES)}")
    if reported_max not in SUPPORTED_BLOCK_SIZES:
        raise ValueError(
            f"target reports an unsupported stream block size: {reported_max}")
    return min(requested, reported_max)


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
                      timeout_s: float, *, timing: OtaTiming | None = None,
                      stage: str = "stream_state_wait") -> tuple[int, int, int, int, int]:
    deadline = time.monotonic() + timeout_s
    last = ""
    while time.monotonic() < deadline:
        last = query(ser, "SYST:OTA:STREAM:STAT?", timeout_s,
                     timing=timing, stage=f"{stage}_query")
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
                source: int, package_mode: bool = False) -> bytes:
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
        int(package_mode),
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


@contextmanager
def verified_connection(requested: str | None, baud: int,
                        timeout_s: float, *,
                        timing: OtaTiming | None = None) -> Iterator[tuple[str, str, serial.Serial]]:
    """Open one verified CDC lifetime for an explicitly selected port.

    Discovery still probes candidates separately, but the common explicit-port
    path now carries the verified handle directly into the transfer.
    """
    if requested:
        opened = time.monotonic()
        ser = serial.Serial(requested, baud, timeout=0.1,
                            write_timeout=timeout_s)
        if timing is not None:
            timing.record("serial_open", time.monotonic() - opened,
                          port=requested)
        try:
            settled = time.monotonic()
            time.sleep(0.15)
            if timing is not None:
                timing.record("serial_settle", time.monotonic() - settled,
                              port=requested)
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            identity = query(ser, "*IDN?", timeout_s, timing=timing,
                             stage="serial_probe")
            if "DHRT100" not in identity.upper():
                raise RuntimeError(
                    f"port {requested} is not a DHRT100: {identity!r}")
            yield requested, identity, ser
        finally:
            ser.close()
        return

    discovered = time.monotonic()
    port, identity = resolve_dhrt100_port(None, baud, timeout_s)
    if timing is not None:
        timing.record("port_discovery", time.monotonic() - discovered,
                      port=port, identity=identity)
    opened = time.monotonic()
    with serial.Serial(port, baud, timeout=0.1,
                       write_timeout=timeout_s) as ser:
        if timing is not None:
            timing.record("serial_open", time.monotonic() - opened,
                          port=port)
        settled = time.monotonic()
        time.sleep(0.15)
        if timing is not None:
            timing.record("serial_settle", time.monotonic() - settled,
                          port=port)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        yield port, identity, ser


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


def wait_reenumeration(port: str, baud: int, timeout_s: float, *,
                       timing: OtaTiming | None = None) -> serial.Serial:
    started = time.monotonic()
    deadline = time.monotonic() + max(timeout_s, 20.0)
    last_error: Exception | None = None
    attempts = 0
    while time.monotonic() < deadline:
        attempts += 1
        try:
            ser = serial.Serial(port, baud, timeout=0.1,
                                write_timeout=timeout_s)
            time.sleep(0.2)
            ser.reset_input_buffer()
            if "DHRT100" in query(
                    ser, "*IDN?", timeout_s, timing=timing,
                    stage="reenumeration_probe",
                    details={"attempt": attempts}).upper():
                if timing is not None:
                    timing.record("usb_reenumeration",
                                  time.monotonic() - started,
                                  port=port, attempts=attempts)
                return ser
            ser.close()
        except (OSError, serial.SerialException) as exc:
            last_error = exc
        time.sleep(0.25)
    if timing is not None:
        timing.record("usb_reenumeration", time.monotonic() - started,
                      port=port, attempts=attempts,
                      error=str(last_error or "timeout"))
    raise TimeoutError(f"DHRT100 did not re-enumerate: {last_error}")


def stream_failure_snapshot(ser: serial.Serial, timeout_s: float,
                            timing: OtaTiming | None) -> None:
    for text in (
            "SYST:OTA:STREAM:STAT?",
            "SYST:OTA:STAT?",
            "SYST:ERR?",
    ):
        try:
            response = query(ser, text, timeout_s, timing=timing,
                             stage="failure_diagnostic")
        except (OSError, serial.SerialException) as exc:
            response = f"<transport-error:{exc}>"
        print(f"diagnostic {text} => {response}", file=sys.stderr)


def send(args: argparse.Namespace, timing: OtaTiming | None = None,
         metadata: dict[str, Any] | None = None) -> int:
    with verified_connection(args.port, args.baud, args.timeout,
                             timing=timing) as (
            port, idn, ser):
        print(f"board=DHRT100 idn={idn}")
        print(f"connection={port}")
        ser.reset_input_buffer()
        target_slot = parse_first_uint(query(
            ser, "SYST:OTA:TARG?", args.timeout, timing=timing,
            stage="target_slot_query"))
        if target_slot not in (1, 2):
            raise ValueError(f"unsupported target slot {target_slot}")
        reported_block_size = COMPATIBILITY_BLOCK_SIZE
        stream_capability = STREAM_CAPABILITIES
        reported_map_version = DEFAULT_DEPLOYMENT_MAP_VERSION
        capability_reported = False
        try:
            reported_block_size, stream_capability, reported_map_version = \
                parse_stream_capability(
                query(ser, "SYST:OTA:STREAM:CAP?", args.timeout,
                      timing=timing, stage="stream_capability_query"))
            capability_reported = True
        except (ValueError, RuntimeError):
            # Old firmware does not expose the live deployment map. It is
            # unsafe to infer v2 from FLASH:MAP?, which reports a catalog
            # candidate; require an explicit map version for that transition.
            print("stream_capability_query=unavailable; using compatibility "
                  "defaults", file=sys.stderr)
        if args.map_version is not None and \
                args.map_version != reported_map_version:
            raise RuntimeError(
                f"requested map v{args.map_version} does not match target "
                f"map v{reported_map_version}")
        map_version = args.map_version or reported_map_version
        if map_version == 1:
            raise RuntimeError(
                "stream transport is unavailable on v1_compat firmware: "
                "durable checkpoints are only attached by the V2 build; "
                "use legacy transport or factory-migrate the board to V2")
        if (stream_capability & STREAM_CAPABILITIES) != STREAM_CAPABILITIES:
            raise RuntimeError(
                "target does not report durable inactive-slot stream support: "
                f"capability=0x{stream_capability:X}")
        block_size = effective_block_size(args.block_size, reported_block_size)
        if metadata is not None:
            metadata.update({
                "effective_block_size": block_size,
                "target_max_block_size": reported_block_size,
                "stream_capability": stream_capability,
                "capability_reported": capability_reported,
                "map_version": map_version,
                "reported_map_version": reported_map_version,
            })
        if block_size != args.block_size:
            print(f"block_size_fallback={args.block_size}->{block_size} "
                  f"target_max={reported_block_size}")
        image_path = args.image
        if not args.package and args.image_a is not None and args.image_b is not None:
            image_path = args.image_a if target_slot == 1 else args.image_b
        if image_path is None:
            raise ValueError("no image selected")
        source_image = image_path.read_bytes()
        if not source_image:
            raise ValueError("image is empty")
        image = selected_package_object(source_image, target_slot) \
            if args.package else source_image
        identity = hashlib.sha256(idn.encode("utf-8")).digest()[:16]
        partition_id = app_partition_id(map_version, target_slot)
        if args.resume:
            journal = parse_journal_status(query(
                ser, "SYST:OTA:JOUR?", args.timeout, timing=timing,
                stage="journal_query"))
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
                           SOURCE_USB_CDC, args.package)
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
            timing=timing,
            stage="open_ack",
            request="SYST:OTA:STREAM:OPEN <descriptor>",
            details={"descriptor_bytes": len(wire)},
        )
        if "OK" not in response:
            stream_failure_snapshot(ser, args.timeout, timing)
            raise RuntimeError(f"stream OPEN rejected: {response!r}")
        if args.package and args.resume:
            header = image[:PACKAGE_HEADER_SIZE]
            if len(header) != PACKAGE_HEADER_SIZE:
                raise ValueError("package is smaller than its manifest header")
            response = command(
                ser,
                (f"SYST:OTA:STREAM:DATA {SOURCE_USB_CDC},0,"
                 f"{crc32(header)},").encode("ascii") + scpi_block(header),
                args.timeout,
                timing=timing,
                stage="resume_header_ack",
                request="SYST:OTA:STREAM:DATA <resume-header>",
                details={"offset": 0, "data_bytes": len(header)},
            )
            if "OK" not in response:
                raise RuntimeError(
                    f"package resume header rejected: {response!r}")
        ready_started = time.monotonic()
        status = wait_stream_state(
            ser, STREAM_STATE_RECEIVING, args.begin_timeout, timing=timing,
            stage="open_ready")
        if timing is not None:
            timing.record("flash_prepare", time.monotonic() - ready_started,
                          durable_offset=status[2])
        resume_offset = status[2]
        if resume_offset > len(image) or (
                resume_offset != len(image) and
                resume_offset % block_size != 0):
            raise RuntimeError(
                f"invalid durable offset {resume_offset} for block size "
                f"{block_size}")
        print(f"resume_offset={resume_offset}")

        for offset, chunk in stream_chunks(image, resume_offset,
                                           block_size, args.package):
            response = command(
                ser,
                (f"SYST:OTA:STREAM:DATA {SOURCE_USB_CDC},{offset},"
                 f"{crc32(chunk)},").encode("ascii") + scpi_block(chunk),
                args.timeout,
                timing=timing,
                stage="data_ack",
                request="SYST:OTA:STREAM:DATA <block>",
                details={
                    "offset": offset,
                    "data_bytes": len(chunk),
                    "chunk_crc32": crc32(chunk),
                },
            )
            if "OK" not in response:
                raise RuntimeError(f"stream DATA rejected at {offset}: {response!r}")
            if offset == 0 or offset + len(chunk) == len(image) or \
                    (offset // block_size) % args.progress_every == 0:
                status = parse_stream_status(query(
                    ser, "SYST:OTA:STREAM:STAT?", args.timeout,
                    timing=timing, stage="data_status_query",
                    details={"offset": offset + len(chunk)}))
                print(f"durable_offset={status[2]} token={status[3]}")

        response = query(
            ser, f"SYST:OTA:STREAM:CLOSE {SOURCE_USB_CDC}", args.timeout,
            timing=timing, stage="close_ack")
        if "OK" not in response:
            raise RuntimeError(f"stream CLOSE rejected: {response!r}")
        close_started = time.monotonic()
        status = wait_stream_state(
            ser, STREAM_STATE_READY_TO_REBOOT, args.begin_timeout,
            timing=timing, stage="close_ready")
        if timing is not None:
            timing.record("flash_finalize", time.monotonic() - close_started,
                          durable_offset=status[2])
        print(f"ready durable_offset={status[2]} token={status[3]}")
        if not args.boot_and_commit:
            return 0
        try:
            query(ser, "SYST:OTA:STREAM:BOOT", args.timeout, timing=timing,
                  stage="boot")
        except (OSError, serial.SerialException):
            pass

    with wait_reenumeration(port, args.baud, args.timeout,
                            timing=timing) as ser:
        if args.expected_build:
            running_build = query(
                ser, "SYSTem:FW:BUILD?", args.timeout, timing=timing,
                stage="build_verify")
            if running_build.strip('"') != args.expected_build:
                raise RuntimeError(
                    f"running build {running_build!r} != "
                    f"{args.expected_build!r}")
        slot_before = query(ser, "SYST:OTA:SLOT?", args.timeout,
                            timing=timing, stage="pre_commit_slot")
        response = query(ser, "SYST:OTA:COMM", args.timeout, timing=timing,
                         stage="commit")
        if "OK" not in response:
            raise RuntimeError(f"commit rejected: {response!r}")
        slot_after = query(ser, "SYST:OTA:SLOT?", args.timeout,
                           timing=timing, stage="post_commit_slot")
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
    parser.add_argument(
        "--map-version", type=int, choices=(1, 2),
        default=None,
        help="deployed Flash map version; default queries the target",
    )
    parser.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--begin-timeout", type=float, default=60.0)
    parser.add_argument("--progress-every", type=int, default=16)
    parser.add_argument("--expected-build",
                        help="expected SYSTem:FW:BUILD? after reboot")
    parser.add_argument("--session-id", type=int,
                        help="stable nonzero session identity for manual resume")
    parser.add_argument("--generation", type=int,
                        help="stable nonzero generation for manual resume")
    parser.add_argument("--resume", action="store_true",
                        help="reuse session/generation from SYST:OTA:JOUR?")
    parser.add_argument("--package", action="store_true",
                        help="input is a complete signed OTA package")
    parser.add_argument("--boot-and-commit", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out-dir", type=Path,
                        help="write timing.json and serial_trace.jsonl")
    args = parser.parse_args()
    if args.image is None and (args.image_a is None or args.image_b is None):
        parser.error("provide image or both --image-a and --image-b")
    if args.package and args.image is None:
        parser.error("--package requires the positional package path")
    if args.package and (args.image_a is not None or args.image_b is not None):
        parser.error("--package cannot be combined with slot artifacts")
    if args.progress_every <= 0:
        parser.error("--progress-every must be positive")
    if args.block_size not in SUPPORTED_BLOCK_SIZES:
        parser.error(
            "--block-size must be one of "
            f"{', '.join(str(size) for size in SUPPORTED_BLOCK_SIZES)}")
    if args.resume and (args.session_id is not None or args.generation is not None):
        parser.error("--resume cannot be combined with explicit session identity")
    if args.session_id is not None and not 0 < args.session_id <= 0xFFFFFFFF:
        parser.error("--session-id must fit a nonzero uint32")
    if args.generation is not None and not 0 < args.generation <= 0xFFFFFFFF:
        parser.error("--generation must fit a nonzero uint32")
    return args


def main() -> int:
    args = parse_args()
    timing = OtaTiming("stream")
    metadata: dict[str, Any] = {
        "port": args.port or "",
        "image": str(args.image or ""),
        "map_version": args.map_version,
        "requested_block_size": args.block_size,
        "effective_block_size": None,
        "target_max_block_size": None,
        "stream_capability": None,
    }
    result = 2
    error = ""
    try:
        result = send(args, timing, metadata)
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        error = str(exc)
        print(f"FAIL {exc}", file=sys.stderr)
    finally:
        if args.out_dir is not None:
            timing.write(
                args.out_dir,
                passed=result == 0,
                error=error,
                metadata=metadata,
            )
    return result


if __name__ == "__main__":
    raise SystemExit(main())
