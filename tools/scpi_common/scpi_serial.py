#!/usr/bin/env python3
"""Shared USB CDC serial lifecycle helpers for SCPI tools."""

from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator
import os
import re
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


COMPOSITE_ACK_HEADERS = {
    "CAL:MARK:CAPT:SAVE", "CALIBRATION:MARKER:CAPTURE:SAVE",
    "CAL:DATA:CAPT:SAVE", "CALIBRATION:DATA:CAPTURE:SAVE",
    "CAL:SCK:CAPT:SAVE", "CALIBRATION:SCK:CAPTURE:SAVE",
    "CAL:RING:CAPT:SAVE", "CALIBRATION:RING:CAPTURE:SAVE",
    "SYST:STOR:FILE:READ?", "SYSTEM:STORAGE:FILE:READ?",
    "SYST:STOR:FILE:INFO?", "SYSTEM:STORAGE:FILE:INFO?",
    "MMEM:CAT:PAGE?", "MMEMORY:CATALOG:PAGE?",
    "MMEM:CAT?", "MMEMORY:CATALOG?",
    "MMEM:INFO?", "MMEMORY:INFO?",
    "MMEM:READ?", "MMEMORY:READ?",
    "SYST:REFMEM:VDC:VECTOR?", "SYSTEM:REFMEM:VDC:VECTOR?",
    "SYST:REFMEM:DPLL:VECTOR?", "SYSTEM:REFMEM:DPLL:VECTOR?",
    "SYST:SYNC:VDC:DPLL:TRACE:ARM",
    "SYSTEM:SYNC:VDC:DPLL:TRACE:ARM",
    "SYST:SYNC:VDC:DPLL:TRACE:STOP",
    "SYSTEM:SYNC:VDC:DPLL:TRACE:STOP",
    "SYST:SYNC:VDC:DPLL:TRACE:SAVE",
    "SYSTEM:SYNC:VDC:DPLL:TRACE:SAVE",
    "SYST:SYNC:VDC:OBSERVER:WAVEFORM:ARM",
    "SYSTEM:SYNC:VDC:OBSERVER:WAVEFORM:ARM",
    "SYST:SYNC:VDC:OBSERVER:WAVEFORM:STOP",
    "SYSTEM:SYNC:VDC:OBSERVER:WAVEFORM:STOP",
    "SYST:SYNC:VDC:OBSERVER:WAVEFORM:SAVE",
    "SYSTEM:SYNC:VDC:OBSERVER:WAVEFORM:SAVE",
}
MARKER_CAPTURE_SAVE_HEADERS = {
    "CAL:MARK:CAPT:SAVE", "CALIBRATION:MARKER:CAPTURE:SAVE",
    "CAL:DATA:CAPT:SAVE", "CALIBRATION:DATA:CAPTURE:SAVE",
    "CAL:SCK:CAPT:SAVE", "CALIBRATION:SCK:CAPTURE:SAVE",
    "CAL:RING:CAPT:SAVE", "CALIBRATION:RING:CAPTURE:SAVE",
}
STORAGE_COMPOSITE_QUERY_HEADERS = COMPOSITE_ACK_HEADERS - MARKER_CAPTURE_SAVE_HEADERS
LOAD_MASK_SET_HEADERS = {
    "SYST:TDMA:LOAD:MASK", "SYSTEM:TDMA:LOAD:MASK",
}
SCALAR_ONE_QUERY_HEADERS = {
    "SYST:BOARD:NO?", "SYSTEM:BOARD:NO?",
    "SYST:OTA:TARG?", "SYSTEM:OTA:TARG?",
    "SYST:TDMA:RING:ARM:STATUS?", "SYSTEM:TDMA:RING:ARM:STATUS?",
    "SYST:TDMA:RING:DIAGNOSTIC?", "SYSTEM:TDMA:RING:DIAGNOSTIC?",
    "SYST:TDMA:FLIGHT:MODE?", "SYSTEM:TDMA:FLIGHT:MODE?",
    "SYST:TDMA:FLIGHT:CLOCK:EVIDENCE?",
    "SYSTEM:TDMA:FLIGHT:CLOCK:EVIDENCE?",
}
SCALAR_U32_QUERY_HEADERS = {
    "SYST:TDMA:LOAD:MASK?", "SYSTEM:TDMA:LOAD:MASK?",
}
SCK_ARM_HEADERS = {
    "CAL:SCK:ARM", "CALIBRATION:SCK:ARM",
}

SERIAL_LIFECYCLE_COMMAND = "command"
SERIAL_LIFECYCLE_PHASE = "phase"
STORAGE_FILE_READ_MAX_BYTES = 4096


def serial_lifecycle_mode() -> str:
    """Resolve the shared CDC ownership policy for validation subprocesses."""
    value = os.environ.get("HAOFV_SERIAL_LIFECYCLE", "").strip().lower()
    if not value:
        legacy = os.environ.get("HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS")
        value = SERIAL_LIFECYCLE_PHASE if legacy == "1" else \
            SERIAL_LIFECYCLE_COMMAND
    if value not in {SERIAL_LIFECYCLE_COMMAND, SERIAL_LIFECYCLE_PHASE}:
        raise ValueError(f"invalid HAOFV_SERIAL_LIFECYCLE: {value!r}")
    return value


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


class SerialSession:
    """Explicitly owned CDC session with one close path.

    Validation tools may keep a session for a phase, but every owner uses the
    same open/settle/drain/flush/close lifecycle.  This prevents a temporary
    query from opening a port that is still held by another phase helper.
    """

    def __init__(self, port: str, baud: int, timeout_s: float,
                 settle_s: float, *, read_timeout_s: float = 0.1) -> None:
        self.port = port
        self.baud = baud
        self.timeout_s = timeout_s
        self.settle_s = settle_s
        self.read_timeout_s = read_timeout_s
        self.ser: serial.Serial | None = None

    def open(self) -> "SerialSession":
        if self.ser is not None:
            return self
        self.ser = serial.Serial(
            self.port, self.baud, timeout=self.read_timeout_s,
            write_timeout=self.timeout_s)
        try:
            time.sleep(self.settle_s)
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
        except Exception:
            self.close()
            raise
        return self

    def close(self) -> None:
        if self.ser is None:
            return
        ser = self.ser
        # Clear ownership before touching the handle. A target-side software
        # reset intentionally drops USB/CDC immediately, so flush/close may
        # raise ERROR_INVALID_FUNCTION on Windows. Cleanup remains best effort.
        self.ser = None
        try:
            ser.flush()
        except Exception:
            pass
        try:
            ser.close()
        except Exception:
            pass

    def __enter__(self) -> "SerialSession":
        return self.open()

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.close()

    def execute(self, command: str, *, require_match: bool = True) -> str:
        self.open()
        assert self.ser is not None
        self.ser.reset_input_buffer()
        self.ser.write((command + "\n").encode("ascii"))
        self.ser.flush()
        return read_scpi_response(
            self.ser, command, self.timeout_s, require_match=require_match)


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
    text = trim_embedded_scpi_log(line)
    if (header not in COMPOSITE_ACK_HEADERS and
            header not in LOAD_MASK_SET_HEADERS):
        text = strip_scpi_ack_prefix(text)
    if not text or is_scpi_log_line(text):
        return False

    if header in {"SYST:FW:BUILD?", "SYSTEM:FW:BUILD?", "SYST:FW:BUILD?", "SYSTEM:FW:BUILD?"}:
        return re.fullmatch(r'"[^"]+"', text) is not None
    if header == "*IDN?":
        return text.count(",") >= 3
    if header in SCALAR_ONE_QUERY_HEADERS:
        return re.fullmatch(r"[0-8]", text) is not None
    if header in SCALAR_U32_QUERY_HEADERS:
        return re.fullmatch(r"\d+", text) is not None
    if header in LOAD_MASK_SET_HEADERS:
        return (
            re.fullmatch(r'"?OK"?(?:,\s*\d+)?', text) is not None
            or re.fullmatch(r"\d+", text) is not None
        )
    if header in SCK_ARM_HEADERS:
        return _csv_uints_match(text, 4)
    if header in {"SYST:OTA:SLOT?", "SYSTEM:OTA:SLOT?"}:
        return _csv_uints_match(text, 5)
    if header in {"SYST:OTA:TXN?", "SYSTEM:OTA:TXN?"}:
        return _csv_uints_match(text, 8)
    if header in {"SYST:OTA:JOUR?", "SYSTEM:OTA:JOURNAL?"}:
        return _csv_uints_match(text, 13)
    if header in {
            "SYST:OTA:STREAM:CAP?", "SYSTEM:OTA:STREAM:CAPABILITY?"}:
        return _csv_uints_match(text, 3)
    if header in {"SYST:OTA:STAT?", "SYSTEM:OTA:STAT?"}:
        return re.fullmatch(r'"[^"]+",\s*\d+,\s*"[^"]+",\s*\d+', text) is not None
    if header in {"SYST:OTA:RES?", "SYSTEM:OTA:RES?"}:
        return re.fullmatch(r'\d+,\s*"[^"]+",\s*"[^"]+",\s*\d+,\s*\d+,\s*\d+', text) is not None
    if header in MARKER_CAPTURE_SAVE_HEADERS:
        return re.fullmatch(r'"?OK"?,\s*\d+,\s*"/[^"]+"', text) is not None
    if header in {
            "CAL:RING:CAPT:LATC", "CALIBRATION:RING:CAPTURE:LATCH"}:
        return _csv_uints_match(text, 2)
    if header in {
            "SYST:SYNC:VDC:DPLL:TRACE:ARM",
            "SYSTEM:SYNC:VDC:DPLL:TRACE:ARM",
            "SYST:SYNC:VDC:OBSERVER:WAVEFORM:ARM",
            "SYSTEM:SYNC:VDC:OBSERVER:WAVEFORM:ARM",
    }:
        return re.fullmatch(r'"?OK"?,\s*\d+\s*,\s*\d+', text) is not None
    if header in {
            "SYST:SYNC:VDC:DPLL:TRACE:STOP",
            "SYSTEM:SYNC:VDC:DPLL:TRACE:STOP",
            "SYST:SYNC:VDC:OBSERVER:WAVEFORM:STOP",
            "SYSTEM:SYNC:VDC:OBSERVER:WAVEFORM:STOP",
    }:
        return re.fullmatch(r'"?OK"?,\s*\d+\s*,\s*\d+', text) is not None
    if header in {
            "SYST:SYNC:VDC:DPLL:TRACE:SAVE",
            "SYSTEM:SYNC:VDC:DPLL:TRACE:SAVE",
    }:
        # TRACE:SAVE queues a Core0/StorageAO job and returns the immutable
        # capture identity.  Keep this tuple intact so maintenance tooling can
        # wait for the job and download the frozen SRAM image after STOP.
        return re.fullmatch(
            r'"?QUEUED"?,\s*\d+\s*,\s*"/[^"\r\n]+"\s*,\s*\d+',
            text,
        ) is not None
    if header in {
            "SYST:SYNC:VDC:OBSERVER:WAVEFORM:SAVE",
            "SYSTEM:SYNC:VDC:OBSERVER:WAVEFORM:SAVE",
    }:
        return re.fullmatch(
            r'"?OK"?,\s*\d+\s*,\s*"/[^"\r\n]+"', text
        ) is not None
    if header in STORAGE_COMPOSITE_QUERY_HEADERS:
        return re.match(r'^"?OK"?,', text) is not None
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
    header = command.strip().split(maxsplit=1)[0].upper()
    query = is_scpi_query(command)
    preserve_composite_ack = header in COMPOSITE_ACK_HEADERS
    while time.monotonic() < deadline:
        line = read_serial_line_idle(ser, deadline)
        if line is None or is_scpi_log_line(line):
            continue
        line = trim_embedded_scpi_log(line)
        if not preserve_composite_ack:
            without_ack = strip_scpi_ack_prefix(line)
            if not without_ack:
                # A bare ACK is the complete response to a write command. It
                # must be consumed in this transaction; discarding it makes
                # every action wait for the full query timeout and can leave
                # a delayed ACK in front of the next query.
                if not query:
                    return "OK"
                continue
            line = without_ack
        if not line:
            continue
        # Composite responses are command-specific result tuples whose first
        # field is itself "OK".  The input buffer was cleared immediately
        # before the command, so preserve and return the complete tuple rather
        # than treating its first field as a standalone acknowledgement.
        if (require_match and not preserve_composite_ack and
                not scpi_response_matches_command(command, line)):
            continue
        if (query and line in {'"OK"', "OK", "1"} and
                header not in SCALAR_ONE_QUERY_HEADERS):
            continue
        return line
    return "<timeout>"
