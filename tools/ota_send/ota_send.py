#!/usr/bin/env python3
"""Send a raw firmware .bin or unified OTA package to RP2350_TRIG over SCPI USB CDC."""

from __future__ import annotations

import argparse
import binascii
import sys
import time
from pathlib import Path


DEFAULT_BLOCK_SIZE = 512
DEFAULT_BEGIN_TIMEOUT_S = 60.0
PACKAGE_MAGIC = 0x474B5054
PACKAGE_HEADER_SIZE = 512


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
    parser.add_argument("--abort-after-blocks", type=int, default=0, help="abort after sending this many data blocks")
    parser.add_argument("--expect-final-state", default="READY_TO_REBOOT", help="expected final OTA state text")
    parser.add_argument("--dry-run", action="store_true", help="print transfer plan without opening the port")
    parser.add_argument("--no-verify-query", action="store_true", help="skip STAT?/PROG? query commands")
    return parser.parse_args()


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

    while time.monotonic() < deadline:
        response = query(serial_port, "SYST:OTA:STAT?")
        if response:
            last_response = response
            print(f"status={response}")
            if "RECEIVING" in response:
                return
            if "FAILED" in response or "ABORTED" in response:
                raise RuntimeError(f"device rejected OTA begin: {response}")
        time.sleep(0.1)

    raise TimeoutError(f"device did not enter RECEIVING state, last response: {last_response}")


def validate_block_size(block_size: int) -> None:
    if block_size <= 0 or block_size > 512:
        raise ValueError("block size must be in range 1..512")

    if block_size % 256 != 0:
        raise ValueError("block size must be 256 or 512 for current device firmware")


def parse_ota_state(response: str) -> str:
    if response.startswith('"'):
        end = response.find('"', 1)
        if end > 1:
            return response[1:end]

    return response.split(",", 1)[0]


def corrupt_vector(image: bytes) -> bytes:
    if len(image) < 8:
        raise ValueError("image too small to corrupt vector")

    data = bytearray(image)
    data[4:8] = (0).to_bytes(4, byteorder="little")
    return bytes(data)


def is_unified_package(image: bytes) -> bool:
    if len(image) < PACKAGE_HEADER_SIZE:
        return False

    return int.from_bytes(image[0:4], byteorder="little") == PACKAGE_MAGIC


def query_final_status(serial_port, delay_s: float = 0.1) -> str:
    time.sleep(delay_s)
    return query(serial_port, "SYST:OTA:STAT?")


def send_image(args: argparse.Namespace, image: bytes, image_crc: int, package_mode: bool) -> str:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    with serial.Serial(args.port, args.baud, timeout=args.timeout, write_timeout=args.timeout) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        begin_command = "SYST:OTA:PBEGIN" if package_mode else "SYST:OTA:BEGIN"
        write_line(ser, f"{begin_command} {len(image)},{image_crc}")
        wait_for_receiving(ser, args.begin_timeout)

        for offset in range(0, len(image), args.block_size):
            chunk = image[offset : offset + args.block_size]
            ser.write(b"SYST:OTA:DATA ")
            ser.write(scpi_block(chunk))
            ser.write(b"\n")

            if package_mode and offset == 0:
                wait_for_receiving(ser, args.begin_timeout)

            sent_blocks = (offset // args.block_size) + 1
            if args.abort_after_blocks and sent_blocks >= args.abort_after_blocks:
                write_line(ser, "SYST:OTA:ABOR")
                final_status = query_final_status(ser)
                if not args.no_verify_query:
                    print(final_status)
                return final_status

            if not args.no_verify_query and ((offset // args.block_size) % 16 == 0):
                response = query(ser, "SYST:OTA:PROG?")
                if response:
                    print(f"progress={response}")

        write_line(ser, "SYST:OTA:END")
        final_status = query_final_status(ser)
        if not args.no_verify_query:
            print(final_status)
        return final_status


def main() -> int:
    args = parse_args()
    validate_block_size(args.block_size)

    image_path = select_image_path(args)
    image = image_path.read_bytes()
    if args.corrupt_vector:
        image = corrupt_vector(image)

    package_mode = is_unified_package(image)
    image_crc = crc32(image)
    send_crc = image_crc ^ 0xFFFFFFFF if args.corrupt_crc else image_crc
    block_count = (len(image) + args.block_size - 1) // args.block_size

    print(f"port={args.port}")
    print(f"image={image_path}")
    print(f"size={len(image)}")
    print(f"crc32=0x{image_crc:08X}")
    if args.corrupt_crc:
        print(f"send_crc32=0x{send_crc:08X}")
    if args.corrupt_vector:
        print("corrupt_vector=reset_handler_zero")
    if args.abort_after_blocks:
        print(f"abort_after_blocks={args.abort_after_blocks}")
    print(f"block_size={args.block_size}")
    print(f"block_count={block_count}")
    begin_command = "SYST:OTA:PBEGIN" if package_mode else "SYST:OTA:BEGIN"
    if package_mode:
        print("format=unified_package")
    else:
        print("format=raw_bin")
    print(f"begin={begin_command} {len(image)},{send_crc}")

    if args.dry_run:
        return 0

    final_status = send_image(args, image, send_crc, package_mode)
    final_state = parse_ota_state(final_status)
    if final_state != args.expect_final_state:
        print(f"unexpected_final_state={final_state}, expected={args.expect_final_state}", file=sys.stderr)
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
