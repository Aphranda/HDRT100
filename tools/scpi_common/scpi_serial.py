#!/usr/bin/env python3
"""Shared USB CDC serial lifecycle helpers for SCPI tools."""

from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


@contextmanager
def open_serial_port(port: str,
                     baud: int,
                     timeout_s: float,
                     settle_s: float,
                     *,
                     read_timeout_s: float = 0.1) -> Iterator[serial.Serial]:
    """Open a CDC port, settle it, clear stale bytes, and always close it."""
    ser = serial.Serial(port, baud, timeout=read_timeout_s, write_timeout=timeout_s)
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


def read_serial_line_idle(ser: serial.Serial,
                          deadline: float,
                          *,
                          idle_gap_s: float = 0.05,
                          encoding: str = "utf-8") -> str | None:
    """Read one CR/LF terminated or idle-delimited line until deadline."""
    raw = bytearray()
    last_rx = 0.0
    while time.monotonic() < deadline:
        ch = ser.read(1)
        if not ch:
            if raw and last_rx > 0.0 and (time.monotonic() - last_rx) >= idle_gap_s:
                break
            continue
        raw.extend(ch)
        last_rx = time.monotonic()
        if ch in (b"\n", b"\r"):
            break
    if not raw:
        return None
    return bytes(raw).decode(encoding, errors="replace").strip()
