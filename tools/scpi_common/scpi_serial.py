#!/usr/bin/env python3
"""Shared USB CDC serial lifecycle helpers for SCPI tools."""

from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator
import re
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


def is_scpi_log_line(line: str) -> bool:
    """Return true for diagnostic text sharing the USB CDC stream."""
    text = line.strip()
    maybe_log = text[1:] if text.startswith('"[') else text
    return (
        not text
        or maybe_log.startswith("[")
        or maybe_log.startswith("log:")
        or " initialized" in maybe_log
        or " service initialized" in maybe_log
    )


def trim_embedded_scpi_log(line: str) -> str:
    """Remove a trailing diagnostic log fragment appended after a response."""
    match = re.search(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+', line)
    return line[:match.start()].strip() if match else line.strip()


def strip_scpi_ack_prefix(line: str) -> str:
    """Drop standalone or leading OK acknowledgements before query data."""
    text = line.strip()
    if text in {'"OK"', "OK", 'OK"'}:
        return ""
    if text.startswith('"OK[') or text.startswith("OK["):
        return ""
    if text.startswith('"OK"['):
        return text[4:].strip()
    if text.startswith('OK"['):
        return text[3:].strip()
    if text.startswith('"OK"') and len(text) > 4:
        return text[4:].strip()
    return text


def is_scpi_query(command: str) -> bool:
    header = command.strip().split(maxsplit=1)[0]
    return "?" in header


def _csv_uints_match(line: str, count: int) -> bool:
    return re.fullmatch(r"\s*\d+\s*" + (r",\s*\d+\s*" * (count - 1)), line) is not None


def scpi_response_matches_command(command: str, line: str) -> bool:
    """Screen obvious boot logs and stale responses for command-sensitive tools."""
    header = command.strip().split(maxsplit=1)[0].upper()
    text = strip_scpi_ack_prefix(trim_embedded_scpi_log(line))
    if not text or is_scpi_log_line(text):
        return False

    if header in {"SYST:FW:BUILD?", "SYSTEM:FW:BUILD?", "SYST:FW:BUILD?", "SYSTEM:FW:BUILD?"}:
        return re.fullmatch(r'"[^"]+"', text) is not None
    if header == "*IDN?":
        return text.count(",") >= 3
    if header in {"SYST:BOARD:NO?", "SYSTEM:BOARD:NO?"}:
        return re.fullmatch(r"[0-8]", text) is not None
    if header in {"SYST:OTA:SLOT?", "SYSTEM:OTA:SLOT?"}:
        return _csv_uints_match(text, 5)
    if header in {"SYST:OTA:TXN?", "SYSTEM:OTA:TXN?"}:
        return _csv_uints_match(text, 8)
    if header in {"SYST:OTA:JOUR?", "SYSTEM:OTA:JOURNAL?"}:
        return _csv_uints_match(text, 11)
    if header in {"SYST:OTA:STAT?", "SYSTEM:OTA:STAT?"}:
        return re.fullmatch(r'"[^"]+",\s*\d+,\s*"[^"]+",\s*\d+', text) is not None
    if header in {"SYST:OTA:RES?", "SYSTEM:OTA:RES?"}:
        return re.fullmatch(r'\d+,\s*"[^"]+",\s*"[^"]+",\s*\d+,\s*\d+,\s*\d+', text) is not None
    if header in {"SYST:ERR?", "SYSTEM:ERR?", "SYST:ERROR?", "SYSTEM:ERROR?"}:
        return re.fullmatch(r'-?\d+(?:,.*)?', text) is not None
    if is_scpi_query(command):
        return text not in {'"OK"', "OK", "1"}
    return (
        text in {'"OK"', "OK", "1"}
        or re.fullmatch(r"\d+", text) is not None
        or re.fullmatch(r'-\d+(?:,.*)?', text) is not None
    )


def read_scpi_response(ser: serial.Serial,
                       command: str,
                       timeout_s: float,
                       *,
                       require_match: bool = False) -> str:
    """Read a command response while filtering startup logs on the CDC stream."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or is_scpi_log_line(line):
            continue
        line = strip_scpi_ack_prefix(trim_embedded_scpi_log(line))
        if not line:
            continue
        if require_match and not scpi_response_matches_command(command, line):
            continue
        scalar_one_queries = {"SYST:BOARD:NO?", "SYSTEM:BOARD:NO?"}
        header = command.strip().split(maxsplit=1)[0].upper()
        if (is_scpi_query(command) and line in {'"OK"', "OK", "1"} and
                header not in scalar_one_queries):
            continue
        return line
    return "<timeout>"
