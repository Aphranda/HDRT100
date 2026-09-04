#!/usr/bin/env python3
"""Send a raw firmware .bin or unified OTA package to DHRT100 over SCPI USB CDC."""

from __future__ import annotations

import argparse
import binascii
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.ota_timing import OtaTiming  # noqa: E402


DEFAULT_BLOCK_SIZE = 4096
LOCAL_MAX_BLOCK_SIZE = 4096
COMPATIBILITY_BLOCK_SIZE = 512
BLOCK_SIZE_GRANULARITY = 256
SUPPORTED_BLOCK_SIZES = (256, 512, 1024, 2048, 4096)
DEFAULT_BEGIN_TIMEOUT_S = 60.0
PACKAGE_MAGIC = 0x474B5054
PACKAGE_HEADER_SIZE = 512
PACKAGE_VERSION = 2
PACKAGE_IMAGE_TABLE_OFFSET = 192
PACKAGE_IMAGE_ENTRY_SIZE = 32
FLASH_TRANSACTION_STATE_COMPLETE = 9
FLASH_TRANSACTION_STATE_FAILED = 10
FLASH_TRANSACTION_STATE_ABORTED = 11
FLASH_TRANSACTION_GENERATION_INDEX = 11


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial port, for example COM7")
    parser.add_argument("image", nargs="?", type=Path, help="standard raw firmware .bin")
    parser.add_argument("--auto-target", action="store_true", help="query SYST:OTA:TARG? and choose --image-a/--image-b")
    parser.add_argument("--image-a", type=Path, help="Slot A linked raw firmware .bin")
    parser.add_argument("--image-b", type=Path, help="Slot B linked raw firmware .bin")
    parser.add_argument("--baud", type=int, default=115200, help="ignored by USB CDC on most hosts")
    parser.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--begin-timeout", type=float, default=DEFAULT_BEGIN_TIMEOUT_S)
    parser.add_argument("--corrupt-crc", action="store_true", help="send an intentionally wrong CRC32")
    parser.add_argument("--corrupt-vector", action="store_true", help="corrupt the reset handler vector in memory")
    parser.add_argument(
        "--package-negative",
        choices=(
            "image-crc",
            "image-vector",
            "header-magic",
            "header-version",
            "header-size",
            "slot",
            "run-offset",
        ),
        help="mutate a unified OTA package to exercise negative paths",
    )
    parser.add_argument("--abort-after-blocks", type=int, default=0, help="abort after sending this many data blocks")
    parser.add_argument(
        "--flash-transaction-probe-after-blocks",
        type=int,
        default=0,
        help="query the read-only FlashTransaction Vector after the selected data block",
    )
    parser.add_argument("--expect-final-state", default="READY_TO_REBOOT", help="expected final OTA state text")
    parser.add_argument("--expect-error", help="expected OTA error text from SYST:OTA:STAT?")
    parser.add_argument(
        "--boot-and-commit",
        action="store_true",
        help="after READY_TO_REBOOT, reboot the target image and issue OTA:COMM",
    )
    parser.add_argument("--dry-run", action="store_true", help="print transfer plan without opening the port")
    parser.add_argument("--no-verify-query", action="store_true", help="skip STAT?/PROG? query commands")
    parser.add_argument("--out-dir", type=Path,
                        help="write timing.json and serial_trace.jsonl")
    return parser.parse_args()


def closed_loop_expected_state(expected: str, boot_and_commit_enabled: bool) -> str:
    if boot_and_commit_enabled and expected == "READY_TO_REBOOT":
        return "COMMITTED"
    return expected


def scpi_block(data: bytes) -> bytes:
    length = str(len(data)).encode("ascii")
    return b"#" + str(len(length)).encode("ascii") + length + data


def write_line(serial_port, command: str) -> None:
    serial_port.write(command.encode("ascii") + b"\n")


def read_line(serial_port) -> str:
    line = serial_port.readline()
    return line.decode("ascii", errors="replace").strip()


def is_log_line(line: str) -> bool:
    return line.startswith("[") or line.startswith("progress=[")


def is_ack_line(line: str) -> bool:
    return line == '"OK"'


def normalize_scpi_line(line: str) -> str:
    while line.startswith('"OK"'):
        line = line[len('"OK"') :].strip()
    return line


def read_scpi_line(serial_port) -> str:
    while True:
        line = read_line(serial_port)
        if not line:
            return ""
        line = normalize_scpi_line(line)
        if line and not is_log_line(line) and not is_ack_line(line):
            return line


def query(serial_port, command: str) -> str:
    write_line(serial_port, command)
    return read_scpi_line(serial_port)


def parse_first_uint(response: str) -> int:
    token = response.split(",", 1)[0].strip()
    if not token:
        raise ValueError(f"empty numeric response: {response!r}")
    return int(token, 0)


def parse_transfer_capability(response: str) -> tuple[int, int, int]:
    fields = tuple(int(field.strip(), 0) for field in response.split(","))
    if len(fields) != 3 or fields[0] not in SUPPORTED_BLOCK_SIZES or \
            fields[0] > LOCAL_MAX_BLOCK_SIZE or fields[2] not in (1, 2):
        raise ValueError(f"invalid OTA transfer capability: {response!r}")
    return fields  # type: ignore[return-value]


def effective_block_size(requested: int, reported_max: int) -> int:
    validate_block_size(requested)
    if reported_max not in SUPPORTED_BLOCK_SIZES:
        raise ValueError(f"unsupported OTA block limit: {reported_max}")
    negotiated = min(requested, reported_max)
    return negotiated - (negotiated % BLOCK_SIZE_GRANULARITY)


def transfer_chunks(data: bytes, block_size: int,
                    package_mode: bool) -> list[tuple[int, bytes]]:
    validate_block_size(block_size)
    if package_mode and len(data) < PACKAGE_HEADER_SIZE:
        raise ValueError("package is smaller than its manifest header")
    chunks: list[tuple[int, bytes]] = []
    offset = 0
    if package_mode:
        chunks.append((0, data[:PACKAGE_HEADER_SIZE]))
        offset = PACKAGE_HEADER_SIZE
    while offset < len(data):
        end = min(offset + block_size, len(data))
        chunks.append((offset, data[offset:end]))
        offset = end
    return chunks


def select_image_path(args: argparse.Namespace) -> Path:
    if not args.auto_target:
        if args.image is None:
            raise SystemExit("image is required unless --auto-target is used")
        return args.image

    if args.image_a is None or args.image_b is None:
        raise SystemExit("--auto-target requires --image-a and --image-b")

    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    with serial.Serial(args.port, args.baud, timeout=args.timeout, write_timeout=args.timeout) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        target_response = query(ser, "SYST:OTA:TARG?")

    target_slot = parse_first_uint(target_response)
    if target_slot == 1:
        print(f"target_slot=1 image={args.image_a}")
        return args.image_a
    if target_slot == 2:
        print(f"target_slot=2 image={args.image_b}")
        return args.image_b

    raise SystemExit(f"unsupported target slot response: {target_response!r}")


def wait_for_receiving(serial_port, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    last_response = ""
    terminal_seen = 0

    while time.monotonic() < deadline:
        response = query(serial_port, "SYST:OTA:STAT?")
        if response:
            last_response = response
            print(f"status={response}")
            if "RECEIVING" in response:
                return
            if "FAILED" in response or "ABORTED" in response:
                # BEGIN is asynchronous.  A single terminal snapshot can be
                # the previous session before OTA AO consumes the new BEGIN;
                # require consecutive terminal observations before failing.
                terminal_seen += 1
                if terminal_seen >= 3:
                    raise RuntimeError(
                        f"device entered terminal OTA state while waiting: {response!r}")
            else:
                terminal_seen = 0
        time.sleep(0.1)

    raise TimeoutError(f"device did not enter RECEIVING state, last response: {last_response}")


def validate_block_size(block_size: int) -> None:
    if block_size not in SUPPORTED_BLOCK_SIZES:
        raise ValueError(
            "block size must be one of "
            f"{', '.join(str(size) for size in SUPPORTED_BLOCK_SIZES)}")


def parse_ota_state(response: str) -> str:
    if response.startswith('"'):
        end = response.find('"', 1)
        if end > 1:
            return response[1:end]

    return response.split(",", 1)[0]


def parse_csv_text_field(response: str, index: int) -> str:
    fields: list[str] = []
    current: list[str] = []
    in_quote = False
    for char in response:
        if char == '"':
            in_quote = not in_quote
            continue
        if char == "," and not in_quote:
            fields.append("".join(current).strip())
            current = []
            continue
        current.append(char)
    fields.append("".join(current).strip())
    if index >= len(fields):
        return ""
    return fields[index]


def parse_ota_error(response: str) -> str:
    return parse_csv_text_field(response, 2)


def read_u32(data: bytes | bytearray, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], byteorder="little")


def write_u32(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 4] = (value & 0xFFFFFFFF).to_bytes(4, byteorder="little")


def corrupt_vector(image: bytes) -> bytes:
    if len(image) < 8:
        raise ValueError("image too small to corrupt vector")

    data = bytearray(image)
    data[4:8] = (0).to_bytes(4, byteorder="little")
    return bytes(data)


def package_image_entry_offset(index: int) -> int:
    return PACKAGE_IMAGE_TABLE_OFFSET + index * PACKAGE_IMAGE_ENTRY_SIZE


def selected_package_image_index(package: bytes, target_slot: int | None = None) -> int:
    image_count = read_u32(package, 20)
    if image_count == 0:
        raise ValueError("package has no image entries")
    if target_slot is None:
        return 0
    for index in range(image_count):
        image_entry = package_image_entry_offset(index)
        if read_u32(package, image_entry) == target_slot:
            return index
    raise ValueError(f"package has no image entry for target slot {target_slot}")


def mutate_package(package: bytes, mutation: str, target_slot: int | None = None) -> bytes:
    if not is_unified_package(package):
        raise ValueError("--package-negative requires a unified OTA package")

    data = bytearray(package)
    image_index = selected_package_image_index(package, target_slot)
    image_entry = package_image_entry_offset(image_index)
    image_offset = read_u32(data, image_entry + 4)

    if mutation == "image-crc":
        write_u32(data, image_entry + 12, read_u32(data, image_entry + 12) ^ 0xFFFFFFFF)
    elif mutation == "image-vector":
        data[image_offset + 4 : image_offset + 8] = (0).to_bytes(4, byteorder="little")
        image_size = read_u32(data, image_entry + 8)
        write_u32(data, image_entry + 12, crc32(data[image_offset : image_offset + image_size]))
    elif mutation == "header-magic":
        write_u32(data, 0, PACKAGE_MAGIC ^ 0xFFFFFFFF)
    elif mutation == "header-version":
        write_u32(data, 4, PACKAGE_VERSION + 1)
    elif mutation == "header-size":
        write_u32(data, 12, len(data) + PACKAGE_HEADER_SIZE)
    elif mutation == "slot":
        write_u32(data, image_entry, 99)
    elif mutation == "run-offset":
        write_u32(data, image_entry + 16, read_u32(data, image_entry + 16) ^ 0x00180000)
    else:
        raise ValueError(f"unsupported package mutation: {mutation}")

    return bytes(data)


def query_target_slot(port: str, baud: int, timeout_s: float) -> int:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    with serial.Serial(port, baud, timeout=timeout_s, write_timeout=timeout_s) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        response = query(ser, "SYST:OTA:TARG?")
    target_slot = parse_first_uint(response)
    if target_slot not in (1, 2):
        raise ValueError(f"unsupported target slot response: {response!r}")
    return target_slot


def is_unified_package(image: bytes) -> bool:
    if len(image) < PACKAGE_HEADER_SIZE:
        return False

    return int.from_bytes(image[0:4], byteorder="little") == PACKAGE_MAGIC


def query_final_status(serial_port, delay_s: float = 0.1) -> str:
    time.sleep(delay_s)
    return query(serial_port, "SYST:OTA:STAT?")


def wait_for_ready_to_reboot(serial_port, timeout_s: float) -> str:
    """Wait for the schedulable END transaction to reach its durable terminal state."""
    deadline = time.monotonic() + timeout_s
    last_status = ""
    while time.monotonic() < deadline:
        status = query(serial_port, "SYST:OTA:STAT?")
        if status:
            last_status = status
            state = parse_ota_state(status)
            if state == "READY_TO_REBOOT" or state in {"FAILED", "ABORTED"}:
                return status
        time.sleep(0.05)
    raise TimeoutError(
        f"device did not reach READY_TO_REBOOT after END; last status: {last_status!r}"
    )


def wait_for_received_offset(serial_port, expected_offset: int,
                             timeout_s: float) -> str:
    """Wait until the asynchronous OTA AO consumes one DATA block."""
    deadline = time.monotonic() + timeout_s
    last_status = ""
    while time.monotonic() < deadline:
        progress = query(serial_port, "SYST:OTA:PROG?")
        if progress:
            try:
                received = int(progress.split(",", 1)[0].strip(), 0)
            except ValueError:
                received = -1
            if received >= expected_offset:
                return progress
        status = query(serial_port, "SYST:OTA:STAT?")
        if status:
            last_status = status
            if "FAILED" in status or "ABORTED" in status:
                raise RuntimeError(
                    f"device entered terminal OTA state while waiting for offset "
                    f"{expected_offset}: {status!r}")
        time.sleep(0.01)
    raise TimeoutError(
        f"device did not consume DATA through offset {expected_offset}; "
        f"last status: {last_status!r}")


def boot_and_commit(port: str, baud: int, timeout_s: float,
                    settle_s: float = 4.0) -> str:
    """Apply a staged image and confirm it after the USB CDC reset.

    BOOT intentionally tears down the serial connection.  Re-opening the
    port and observing IDLE before COMM makes transport completion distinct
    from boot/confirmation completion.
    """
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    try:
        with serial.Serial(port, baud, timeout=timeout_s,
                           write_timeout=timeout_s) as ser:
            ser.reset_input_buffer()
            write_line(ser, "SYST:OTA:BOOT")
            print(f"boot_response={read_scpi_line(ser)}")
    except (OSError, serial.SerialException) as exc:
        # A reset commonly closes CDC before the textual acknowledgement is
        # delivered; the re-enumeration below is the authoritative step.
        print(f"boot_reset={exc}")

    deadline = time.monotonic() + max(timeout_s, settle_s) + 15.0
    time.sleep(settle_s)
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with serial.Serial(port, baud, timeout=timeout_s,
                               write_timeout=timeout_s) as ser:
                ser.reset_input_buffer()
                status = query(ser, "SYST:OTA:STAT?")
                print(f"post_boot_status={status}")
                if parse_ota_state(status) != "IDLE":
                    last_error = RuntimeError(
                        f"target did not return IDLE after BOOT: {status!r}"
                    )
                    time.sleep(0.25)
                    continue
                write_line(ser, "SYST:OTA:COMM")
                print(f"commit_response={read_scpi_line(ser)}")
                final_status = query_final_status(ser)
                print(f"committed_status={final_status}")
                return final_status
        except (OSError, serial.SerialException) as exc:
            last_error = exc
        time.sleep(0.25)

    raise TimeoutError(f"DHRT100 did not re-enumerate for BOOT/COMM: {last_error}")


def parse_flash_transaction_state(response: str) -> tuple[int, int]:
    fields = [int(field.strip(), 0) for field in response.split(",")]
    if len(fields) <= FLASH_TRANSACTION_GENERATION_INDEX:
        raise ValueError(f"incomplete FlashTransaction Vector: {response!r}")
    return fields[0], fields[FLASH_TRANSACTION_GENERATION_INDEX]


def wait_for_flash_transaction_probe(serial_port, baseline: str,
                                     timeout_s: float) -> str:
    _, baseline_generation = parse_flash_transaction_state(baseline)
    deadline = time.monotonic() + timeout_s
    last_response = ""
    while time.monotonic() < deadline:
        response = query(serial_port, "SYST:DIAG:FLASH:TRAN?")
        if response:
            last_response = response
            state, generation = parse_flash_transaction_state(response)
            if (generation != baseline_generation and
                    state in {
                        FLASH_TRANSACTION_STATE_COMPLETE,
                        FLASH_TRANSACTION_STATE_FAILED,
                        FLASH_TRANSACTION_STATE_ABORTED,
                    }):
                return response
        time.sleep(0.01)
    raise TimeoutError(
        "FlashTransaction probe did not reach a new terminal generation, "
        f"last response: {last_response}"
    )


def send_image(args: argparse.Namespace, image: bytes, image_crc: int,
               package_mode: bool, timing: OtaTiming | None = None,
               metadata: dict[str, Any] | None = None) -> str:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    open_started = time.monotonic()
    with serial.Serial(args.port, args.baud, timeout=args.timeout, write_timeout=args.timeout) as ser:
        if timing is not None:
            timing.record("serial_open", time.monotonic() - open_started,
                          port=args.port)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        capability_started = time.monotonic()
        capability_response = ""
        reported_block_size = COMPATIBILITY_BLOCK_SIZE
        capability_mask = 0
        reported_map_version = 0
        capability_reported = False
        try:
            capability_response = query(ser, "SYST:OTA:STREAM:CAP?")
            reported_block_size, capability_mask, reported_map_version = \
                parse_transfer_capability(capability_response)
            capability_reported = True
        except ValueError:
            print("transfer_capability_query=unavailable; using 512",
                  file=sys.stderr)
        block_size = effective_block_size(args.block_size,
                                          reported_block_size)
        chunks = transfer_chunks(image, block_size, package_mode)
        if (args.flash_transaction_probe_after_blocks < 0 or
                args.flash_transaction_probe_after_blocks > len(chunks)):
            raise ValueError(
                "flash transaction probe block must be in range "
                f"0..{len(chunks)}")
        if timing is not None:
            timing.record(
                "transfer_capability_query",
                time.monotonic() - capability_started,
                request="SYST:OTA:STREAM:CAP?",
                response=capability_response,
                target_max_block_size=reported_block_size,
                capability_mask=capability_mask,
                reported_map_version=reported_map_version,
                capability_reported=capability_reported,
            )
        if metadata is not None:
            metadata.update({
                "effective_block_size": block_size,
                "target_max_block_size": reported_block_size,
                "capability_mask": capability_mask,
                "reported_map_version": reported_map_version,
                "capability_reported": capability_reported,
                "block_count": len(chunks),
            })
        if block_size != args.block_size:
            print(f"block_size_fallback={args.block_size}->{block_size} "
                  f"target_max={reported_block_size}")
        print(f"effective_block_size={block_size}")
        print(f"effective_block_count={len(chunks)}")

        begin_command = "SYST:OTA:PBEGIN" if package_mode else "SYST:OTA:BEGIN"
        begin_started = time.monotonic()
        write_line(ser, f"{begin_command} {len(image)},{image_crc}")
        wait_for_receiving(ser, args.begin_timeout)
        if timing is not None:
            timing.record("open_ready", time.monotonic() - begin_started,
                          request=f"{begin_command} {len(image)},{image_crc}",
                          image_bytes=len(image), package_mode=package_mode)

        for sent_blocks, (offset, chunk) in enumerate(chunks, start=1):
            block_started = time.monotonic()
            # DATA is posted to the OTA AO asynchronously.  In package mode
            # the first header block starts a bounded erase service; wait for
            # the AO to return to RECEIVING before queueing the next block so
            # a full USB burst cannot starve that service or turn into
            # INVALID_STATE events.
            if package_mode and offset != 0:
                # The first package block also starts erasing the inactive
                # image.  Sector erase is deliberately bounded and may take
                # substantially longer than one USB round trip.
                time.sleep(5.0 if offset == PACKAGE_HEADER_SIZE else 0.02)
                wait_for_receiving(ser, args.begin_timeout)
            probe_baseline = ""
            if sent_blocks == args.flash_transaction_probe_after_blocks:
                probe_baseline = query(ser, "SYST:DIAG:FLASH:TRAN?")
                parse_flash_transaction_state(probe_baseline)

            ser.write(b"SYST:OTA:DATA ")
            ser.write(scpi_block(chunk))
            ser.write(b"\n")

            if package_mode and offset == 0:
                first_block_status = query_final_status(ser)
                if first_block_status:
                    if not args.no_verify_query:
                        print(first_block_status)
                    if "FAILED" in first_block_status or "ABORTED" in first_block_status:
                        return first_block_status
                    if "RECEIVING" not in first_block_status:
                        wait_for_receiving(ser, args.begin_timeout)

            # DATA is asynchronous; apply sender-side back-pressure so the
            # following block (or END) cannot overtake the AO.
            wait_for_received_offset(ser, offset + len(chunk), args.begin_timeout)

            if sent_blocks == args.flash_transaction_probe_after_blocks:
                response = wait_for_flash_transaction_probe(
                    ser, probe_baseline, args.timeout
                )
                print(f"flash_transaction_probe={response}")

            if args.abort_after_blocks and sent_blocks >= args.abort_after_blocks:
                write_line(ser, "SYST:OTA:ABOR")
                final_status = query_final_status(ser)
                if not args.no_verify_query:
                    print(final_status)
                return final_status

            if not args.no_verify_query and ((sent_blocks - 1) % 16 == 0):
                response = query(ser, "SYST:OTA:PROG?")
                if response:
                    print(f"progress={response}")
            if timing is not None:
                timing.record(
                    "data_ack",
                    time.monotonic() - block_started,
                    offset=offset,
                    data_bytes=len(chunk),
                    block_index=sent_blocks,
                )

        close_started = time.monotonic()
        write_line(ser, "SYST:OTA:END")
        try:
            final_status = wait_for_ready_to_reboot(
                ser, max(args.timeout, args.begin_timeout)
            )
        except Exception as exc:
            # END may legitimately reset/re-enumerate the CDC device.  Close
            # the stale handle and obtain the authoritative post-END state
            # through a fresh lifetime instead of leaking the port object or
            # reporting an opaque Win32 ClearCommError.
            print(f"end_transport_reset={exc}")
            final_status = ""
            deadline = time.monotonic() + max(args.timeout, args.begin_timeout)
            while time.monotonic() < deadline:
                try:
                    with serial.Serial(args.port, args.baud,
                                       timeout=args.timeout,
                                       write_timeout=args.timeout) as reopened:
                        reopened.reset_input_buffer()
                        final_status = wait_for_ready_to_reboot(
                            reopened, max(args.timeout, args.begin_timeout)
                        )
                        break
                except (OSError, serial.SerialException):
                    time.sleep(0.25)
        if not args.no_verify_query:
            print(final_status)
        if timing is not None:
            timing.record("close_ready", time.monotonic() - close_started,
                          response=final_status)
        return final_status


def main() -> int:
    args = parse_args()
    timing = OtaTiming("legacy")
    result = 2
    error = ""
    metadata: dict[str, Any] = {
        "port": args.port,
        "image": str(getattr(args, "image", "") or ""),
        "requested_block_size": args.block_size,
        "effective_block_size": None,
        "target_max_block_size": None,
        "capability_mask": None,
        "capability_reported": False,
    }
    try:
        validate_block_size(args.block_size)

        image_path = select_image_path(args)
        image = image_path.read_bytes()
        package_mode = is_unified_package(image)
        if args.package_negative:
            target_slot = None
            if package_mode and not args.dry_run:
                target_slot = query_target_slot(args.port, args.baud, args.timeout)
                print(f"package_target_slot={target_slot}")
            image = mutate_package(image, args.package_negative, target_slot)
            package_mode = True
        if args.corrupt_vector:
            image = corrupt_vector(image)

        image_crc = crc32(image)
        send_crc = image_crc ^ 0xFFFFFFFF if args.corrupt_crc else image_crc
        block_count = len(transfer_chunks(image, args.block_size,
                                          package_mode))
        if args.flash_transaction_probe_after_blocks < 0:
            raise ValueError(
                "flash transaction probe block must be non-negative")

        print(f"port={args.port}")
        print(f"image={image_path}")
        print(f"size={len(image)}")
        print(f"crc32=0x{image_crc:08X}")
        if args.corrupt_crc:
            print(f"send_crc32=0x{send_crc:08X}")
        if args.corrupt_vector:
            print("corrupt_vector=reset_handler_zero")
        if args.package_negative:
            print(f"package_negative={args.package_negative}")
        if args.abort_after_blocks:
            print(f"abort_after_blocks={args.abort_after_blocks}")
        if args.flash_transaction_probe_after_blocks:
            print(
                "flash_transaction_probe_after_blocks="
                f"{args.flash_transaction_probe_after_blocks}"
            )
        print(f"block_size={args.block_size}")
        print(f"block_count={block_count}")
        begin_command = "SYST:OTA:PBEGIN" if package_mode else "SYST:OTA:BEGIN"
        if package_mode:
            print("format=unified_package")
        else:
            print("format=raw_bin")
        print(f"begin={begin_command} {len(image)},{send_crc}")

        if args.dry_run:
            result = 0
            return result

        final_status = send_image(args, image, send_crc, package_mode,
                                  timing, metadata)
        expected_final_state = closed_loop_expected_state(
            args.expect_final_state, args.boot_and_commit
        )
        if args.boot_and_commit:
            if parse_ota_state(final_status) != "READY_TO_REBOOT":
                print(
                    f"boot_and_commit_requires_ready={parse_ota_state(final_status)}",
                    file=sys.stderr,
                )
                result = 4
                return result
            boot_started = time.monotonic()
            final_status = boot_and_commit(args.port, args.baud, args.timeout)
            timing.record("boot_commit", time.monotonic() - boot_started,
                          response=final_status)
        final_state = parse_ota_state(final_status)
        if final_state != expected_final_state:
            print(f"unexpected_final_state={final_state}, expected={expected_final_state}", file=sys.stderr)
            return result
        if args.expect_error:
            final_error = parse_ota_error(final_status)
            if final_error != args.expect_error:
                print(f"unexpected_error={final_error}, expected={args.expect_error}", file=sys.stderr)
                result = 3
                return result

        result = 0
        return result
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        error = str(exc)
        print(f"FAIL {exc}", file=sys.stderr)
        return result
    finally:
        if args.out_dir is not None:
            timing.write(
                args.out_dir, passed=result == 0, error=error,
                metadata=metadata)


if __name__ == "__main__":
    raise SystemExit(main())
