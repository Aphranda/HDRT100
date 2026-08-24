#!/usr/bin/env python3
"""Closed-loop Modbus RTU validator for the DHRT100 RS485 endpoint."""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import serial


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xA001) if crc & 1 else crc >> 1
    return crc


def frame(payload: bytes) -> bytes:
    crc = crc16(payload)
    return payload + bytes((crc & 0xFF, crc >> 8))


def valid_frame(data: bytes) -> bool:
    return len(data) >= 4 and crc16(data[:-2]) == int.from_bytes(data[-2:], "little")


def read_frame(port: serial.Serial, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    result = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(64)
        if chunk:
            result.extend(chunk)
            # Function 03 response length is byte-count + 5; function 06 is 8.
            if len(result) >= 3:
                expected = 8 if result[1] == 0x06 else 5 + result[2]
                if len(result) >= expected:
                    return bytes(result[:expected])
        else:
            time.sleep(0.001)
    return bytes(result)


def transact(port: serial.Serial, request: bytes, timeout: float) -> tuple[bytes, bool]:
    port.reset_input_buffer()
    port.write(request)
    port.flush()
    response = read_frame(port, timeout)
    return response, valid_frame(response)


def run_slave(args: argparse.Namespace) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    with serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1,
                       timeout=0.02, write_timeout=args.timeout) as port:
        requests = [
            ("read_diagnostics", frame(bytes((args.unit, 0x03, 0x00, 0x00, 0x00, 0x09)))),
            ("write_pattern", frame(bytes((args.unit, 0x06, 0x00, 0x10, 0x00, 0xA5)))),
            ("read_pattern", frame(bytes((args.unit, 0x03, 0x00, 0x10, 0x00, 0x01)))),
        ]
        for name, request in requests:
            response, crc_ok = transact(port, request, args.timeout)
            records.append({"case": name, "request": request.hex(),
                            "response": response.hex(), "crc_ok": crc_ok})
            if not response or not crc_ok:
                raise RuntimeError(f"{name}: missing or invalid response {response.hex()}")
        bad = bytearray(frame(bytes((args.unit, 0x03, 0x00, 0x00, 0x00, 0x01))))
        bad[-1] ^= 0x01
        response, crc_ok = transact(port, bytes(bad), args.timeout)
        records.append({"case": "bad_crc_rejected", "request": bytes(bad).hex(),
                        "response": response.hex(), "crc_ok": crc_ok})
        if response:
            raise RuntimeError(f"bad CRC was answered: {response.hex()}")
    return records


def run_peer(args: argparse.Namespace) -> list[dict[str, object]]:
    """Answer one master request, allowing a DHRT100 master to be exercised."""
    records: list[dict[str, object]] = []
    with serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1,
                       timeout=0.02, write_timeout=args.timeout) as port:
        request = read_frame(port, args.timeout)
        if len(request) != 8 or not valid_frame(request):
            raise RuntimeError(f"invalid master request: {request.hex()}")
        if request[1] == 0x03:
            quantity = int.from_bytes(request[4:6], "big")
            payload = bytes((request[0], 0x03, quantity * 2))
            for index in range(quantity):
                payload += (0x6000 + index).to_bytes(2, "big")
            response = frame(payload)
        elif request[1] == 0x06:
            response = frame(request[:6])
        else:
            raise RuntimeError(f"unsupported master function: {request[1]:02x}")
        port.write(response)
        port.flush()
        records.append({"case": "master_peer_response", "request": request.hex(),
                        "response": response.hex(), "crc_ok": valid_frame(response)})
    return records


def run_inject(args: argparse.Namespace) -> list[dict[str, object]]:
    """Inject a valid peer response repeatedly for a DHRT100 master test."""
    quantity = args.inject_read
    payload = bytes((args.unit, 0x03, quantity * 2))
    for index in range(quantity):
        payload += (0x6000 + index).to_bytes(2, "big")
    response = frame(payload)
    records: list[dict[str, object]] = []
    with serial.Serial(args.port, args.baud, bytesize=8, parity="N", stopbits=1,
                       timeout=0.02, write_timeout=args.timeout) as port:
        if args.inject_once:
            time.sleep(0.1)
            port.write(response)
            port.flush()
            time.sleep(max(0.0, args.timeout - 0.1))
        else:
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                port.write(response)
                port.flush()
                time.sleep(0.005)
        records.append({"case": "injected_peer_read", "response": response.hex(),
                        "crc_ok": valid_frame(response)})
    return records


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="RS485 adapter port, e.g. COM11")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--unit", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--peer", action="store_true", help="answer a DHRT100 master request")
    parser.add_argument("--inject-read", type=int,
                        help="inject a valid read response repeatedly for master HIL")
    parser.add_argument("--inject-once", action="store_true",
                        help="send one delayed valid peer response")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.inject_read is not None:
        if not 1 <= args.inject_read <= 32:
            raise SystemExit("--inject-read must be between 1 and 32")
        records = run_inject(args)
    else:
        records = run_peer(args) if args.peer else run_slave(args)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"port": args.port, "baud": args.baud,
                                    "records": records}, indent=2) + "\n",
                           encoding="utf-8")
    print(json.dumps({"records": records}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
