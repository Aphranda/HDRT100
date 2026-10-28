"""Frozen four-channel HDO RAW records with explicit acquisition evidence.

Importing this module never opens VISA or accesses a board. Scope owns the
provided instrument after construction; close() also closes that instrument.
"""

import hashlib
import json
import math
from pathlib import Path
import time

from tools.common.host_evidence_clock import identity as host_clock_identity, now_ns


DEFAULT_RESOURCE = "USB0::0x1AB1::0x0610::HDO4A244301137::INSTR"
DEFAULT_SERIAL = "HDO4A244301137"
RESOURCE = DEFAULT_RESOURCE
BLOCK_POINTS = 1_000_000


def decode_block(raw, expected):
    """Validate a definite BYTE block, retaining only CR/LF terminators."""
    if len(raw) < 3 or raw[:1] != b"#" or not raw[1:2].isdigit():
        raise ValueError("Invalid binary header")
    digits = int(raw[1:2])
    if not digits or len(raw) < 2 + digits or not raw[2:2 + digits].isdigit():
        raise ValueError("Invalid definite block length")
    count = int(raw[2:2 + digits])
    begin = 2 + digits
    if count != expected or len(raw) < begin + count or raw[begin + count:].strip(b"\r\n"):
        raise ValueError("Wrong waveform block length or suffix")
    return raw[begin:begin + count]


class Scope:
    def __init__(self, folder, instrument=None, scale=.02, points=1_000_000,
                 offset=0.0, *, resource=DEFAULT_RESOURCE, expected_serial=DEFAULT_SERIAL):
        if not math.isfinite(scale) or scale <= 0 or not math.isfinite(offset):
            raise ValueError("Finite positive time scale and finite offset required")
        if isinstance(points, bool) or not isinstance(points, int) or points <= 0:
            raise ValueError("Positive integer memory depth required")
        if not resource or not expected_serial:
            raise ValueError("Resource and expected serial are required")
        self.scale = scale
        self.points = points
        self.offset = offset
        self.resource = resource
        self.expected_serial = expected_serial
        # Trigger channel is selectable so a bench wired to another channel can
        # still be qualified; it must be set before prepare().
        self.trigger_source = "CHAN1"
        self.folder = Path(folder)
        self.folder.mkdir(parents=True, exist_ok=True)
        self.rm = None
        try:
            if instrument is None:
                import pyvisa
                self.rm = pyvisa.ResourceManager()
                instrument = self.rm.open_resource(resource)
            self.scope = instrument
            self.scope.timeout = 10000
            self.scope.chunk_size = 1048576
        except Exception:
            # No caller can close a partially constructed Scope. Attempt both
            # handles without replacing the original initialization failure.
            for handle in (instrument, self.rm):
                if handle is not None:
                    try:
                        handle.close()
                    except Exception:
                        pass
            raise
        self.quiet = False
        self.report = dict(
            resource=resource, expected_serial=expected_serial,
            host_clock=host_clock_identity(),
            wiring="CH1..CH4 = NO1..NO4, 1x probes", commands=[], channels=[],
            capture_complete=False,
            voltage_formula="(code - y_origin - y_reference) * y_increment",
            time_formula="x_origin + (start-1 + index - x_reference) * x_increment",
            physical_lock_qualified=False)

    def save(self):
        (self.folder / "scope.json").write_text(
            json.dumps(self.report, indent=2), encoding="utf-8")

    def write(self, command):
        row = dict(command=command, start_ns=now_ns())
        self.report["commands"].append(row)
        try:
            self.scope.write(command)
            time.sleep(.08)
        except Exception as exc:
            row["error"] = repr(exc)
            raise
        finally:
            row["end_ns"] = now_ns()
            self.save()

    def query(self, command):
        if self.quiet:
            raise RuntimeError("Scope query forbidden inside board acquisition")
        row = dict(command=command, start_ns=now_ns())
        self.report["commands"].append(row)
        try:
            row["raw"] = self.scope.query(command).strip()
            # Some HDO firmware revisions reject the abbreviated error query
            # while accepting the SCPI long form.  The failed abbreviation
            # itself is retained in the command log; immediately retry once
            # with the long form so a stale parser error cannot make a valid
            # scope session fail closed.
            if command == ":SYST:ERR?" and row["raw"].startswith("-100,"):
                fallback = ":SYST:ERRor?"
                row["fallback_command"] = fallback
                row["fallback_raw"] = self.scope.query(fallback).strip()
                row["raw"] = row["fallback_raw"]
            return row["raw"]
        except Exception as exc:
            row["error"] = repr(exc)
            raise
        finally:
            row["end_ns"] = now_ns()
            self.save()

    def prepare(self):
        self.report["identity"] = self.query("*IDN?")
        identity_fields = [field.strip() for field in self.report["identity"].split(",")]
        if len(identity_fields) < 3 or identity_fields[2] != self.expected_serial:
            raise ValueError("Wrong scope serial")
        self.write(":STOP")
        if self.query(":TRIG:STAT?") != "STOP":
            raise ValueError("Scope not stopped")
        for ch in range(1, 5):
            for command in (f":CHAN{ch}:DISP 1", f":CHAN{ch}:PROB 1",
                            f":CHAN{ch}:COUP DC", f":CHAN{ch}:SCAL 2", f":CHAN{ch}:OFFS 0"):
                self.write(command)
        for command in (":ACQ:TYPE NORM", ":TIM:MODE MAIN", f":TIM:SCAL {self.scale}",
                        f":TIM:OFFS {self.offset}", f":ACQ:MDEP {self.points}",
                        ":TRIG:MODE EDGE", ":TRIG:EDGE:SOUR CHAN1", ":TRIG:EDGE:SLOP POS",
                        ":TRIG:EDGE:LEV 1.5", ":TRIG:SWE NORM"):
            self.write(command)
        keys = [":TRIG:MODE?", ":TRIG:EDGE:SOUR?", ":TRIG:EDGE:SLOP?", ":TRIG:EDGE:LEV?",
                ":TRIG:SWE?", ":TIM:SCAL?", ":TIM:OFFS?", ":ACQ:MDEP?", ":ACQ:TYPE?"]
        keys += [f":CHAN{ch}:{key}?" for ch in range(1, 5)
                 for key in ("PROB", "DISP", "COUP", "SCAL", "OFFS")]
        settings = {key: self.query(key) for key in keys}
        self.report["settings"] = settings
        self.save()
        if settings[":TRIG:EDGE:SOUR?"] != "CHAN1" or settings[":TRIG:EDGE:SLOP?"] != "POS":
            raise ValueError("Trigger source/slope mismatch")
        if float(settings[":ACQ:MDEP?"]) != self.points:
            raise ValueError("Acquisition memory depth mismatch")
        if not math.isclose(float(settings[":TIM:SCAL?"]), self.scale, rel_tol=1e-9):
            raise ValueError("Time scale mismatch")
        if not math.isclose(float(settings[":TIM:OFFS?"]), self.offset, abs_tol=1e-10):
            raise ValueError("Time offset mismatch")
        if not all(float(settings[f":CHAN{ch}:PROB?"]) == 1 for ch in range(1, 5)):
            raise ValueError("Probe attenuation mismatch")
        if not self.query(":SYST:ERR?").startswith(("0,", "+0,")):
            raise ValueError("Scope setup error")

    def arm(self):
        self.report.pop("armed_ns", None)
        self.report["capture_complete"] = False
        self.write(":SING")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.query(":TRIG:STAT?") == "WAIT":
                break
            time.sleep(.03)
        else:
            raise TimeoutError("No new WAIT; previous STOP is not a capture")
        if self.query(":TRIG:SWE?") != "SING" or self.query(":TRIG:EDGE:SOUR?") != "CHAN1":
            raise ValueError("Armed source/mode changed")
        self.report["armed_ns"] = now_ns()
        self.save()

    def export(self):
        if self.quiet:
            raise RuntimeError("Export before all-board STOP")
        if not self.report.get("armed_ns"):
            raise RuntimeError("No proven new single acquisition arm")
        if self.query(":TRIG:STAT?") != "STOP":
            raise RuntimeError("No completed single trigger")
        self.report["capture_complete"] = False
        self.report["stopped_ns"] = now_ns()
        self.report["sample_rate"] = self.query(":ACQ:SRAT?")
        axis = None
        for ch in range(1, 5):
            for command in (f":WAV:SOUR CHAN{ch}", ":WAV:MODE RAW", ":WAV:FORM BYTE"):
                self.write(command)
            pre = self.query(":WAV:PRE?")
            fields = list(map(float, pre.split(",")))
            if len(fields) != 10 or fields[:2] != [0, 2] or fields[3] != 1:
                raise ValueError("Not one frozen RAW BYTE acquisition")
            if not all(math.isfinite(x) for x in fields) or fields[4] <= 0 or fields[7] <= 0:
                raise ValueError("Invalid waveform scaling")
            if float(self.query(":ACQ:MDEP?")) != self.points:
                raise ValueError("Acquisition memory depth changed")
            total = self.points
            # HDO PRE point count describes the selected transfer range.
            if not 0 < fields[2] <= total or not fields[2].is_integer():
                raise ValueError("Invalid selected transfer size")
            current_axis = tuple(fields[2:7])
            if axis is not None and current_axis != axis:
                raise ValueError("Different channel time axes")
            axis = current_axis
            row = dict(channel=ch, preamble=pre, sample_count=total, blocks=[])
            self.report["channels"].append(row)
            for start in range(1, total + 1, BLOCK_POINTS):
                stop = min(start + BLOCK_POINTS - 1, total)
                self.write(f":WAV:STAR {start}")
                self.write(f":WAV:STOP {stop}")
                readback = {q: self.query(q) for q in
                            (":WAV:SOUR?", ":WAV:MODE?", ":WAV:FORM?", ":WAV:STAR?", ":WAV:STOP?")}
                if (readback[":WAV:SOUR?"] != f"CHAN{ch}" or readback[":WAV:MODE?"] != "RAW"
                        or readback[":WAV:FORM?"] != "BYTE"
                        or float(readback[":WAV:STAR?"]) != start
                        or float(readback[":WAV:STOP?"]) != stop):
                    raise ValueError("Waveform transfer readback mismatch")
                if self.query(":TRIG:STAT?") != "STOP":
                    raise ValueError("Acquisition changed during export")
                self.write(":WAV:DATA?")
                raw = self.scope.read_raw()
                name = f"ch{ch}-{start:08d}.block"
                (self.folder / name).write_bytes(raw)
                block = dict(start=start, stop=stop, path=name, readback=readback,
                             sha256=hashlib.sha256(raw).hexdigest())
                row["blocks"].append(block)
                try:
                    decode_block(raw, stop - start + 1)
                except Exception as exc:
                    block["error"] = repr(exc)
                    raise
                finally:
                    self.save()
        if self.query(":TRIG:STAT?") != "STOP":
            raise ValueError("Acquisition changed after export")
        if not self.query(":SYST:ERR?").startswith(("0,", "+0,")):
            raise ValueError("Scope export error")
        self.report["capture_complete"] = True
        self.save()

    def close(self):
        try:
            self.save()
        finally:
            try:
                self.scope.close()
            finally:
                if self.rm is not None:
                    self.rm.close()
