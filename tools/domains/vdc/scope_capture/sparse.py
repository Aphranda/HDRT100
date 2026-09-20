"""Verified short-window HDO setup without historical capture-script imports."""

import math
from pathlib import Path
import time

from tools.common.hdo_scope.reader import Scope


class SparseScope(Scope):
    def query(self, command):
        if self.quiet:
            raise RuntimeError("Scope query forbidden inside board acquisition")
        if command == ":WAV:PRE?":
            # Compare channel axes only after normalizing HDO's retained range.
            self.write(":WAV:STAR 1")
            self.write(f":WAV:STOP {self.points}")
        return super().query(command)

    def write(self, command):
        if getattr(self, "sampling", False):
            row = dict(command=command, start_ns=time.monotonic_ns())
            self.report["commands"].append(row)
            try:
                self.scope.write(command)
                # RAW setters finish asynchronously; exact readbacks follow.
                if command.startswith(":WAV:") and "?" not in command:
                    time.sleep(.08)
            except Exception as exc:
                row["error"] = repr(exc)
                raise
            finally:
                row["end_ns"] = time.monotonic_ns()
            return
        if command.startswith(":TIM:SCAL "):
            if self.quiet:
                raise RuntimeError("Timebase setup forbidden during board RUN")
            for cmd in (":TRIG:SWE NORM", ":RUN", ":TIM:MODE MAIN", command, ":TIM:MODE MAIN"):
                super().write(cmd)
                if self.query("*OPC?") != "1":
                    raise RuntimeError("Scope setup command incomplete")
                if not self.query(":SYST:ERR?").startswith(("0,", "+0,")):
                    raise RuntimeError("Scope setup command rejected")
            return
        super().write(command)
        if command.startswith(":ACQ:MDEP "):
            time.sleep(.25)
            super().write(f":TIM:OFFS {self.offset}")

    def prepare(self):
        self.sampling = False
        prior = self.report["initial_error_queue"] = []
        for _ in range(32):
            raw = self.query(":SYST:ERR?")
            prior.append(raw)
            if raw.startswith(("0,", "+0,")):
                break
        else:
            raise RuntimeError("Scope initial error queue did not drain")
        super().prepare()
        self.write(":STOP")
        # Shrinking memory leaves an invalid cached RAW range on HDO4404.
        self.write(":WAV:MODE NORM")
        self.write(":WAV:MODE RAW")
        if self.query(":WAV:STAR?") != "1" or float(self.query(":WAV:STOP?")) != self.points:
            raise ValueError("RAW transfer range did not reset to requested memory depth")
        if not self.query(":SYST:ERR?").startswith(("0,", "+0,")):
            raise ValueError("RAW transfer range reset rejected")
        self.identity = self.report["identity"]
        self.settings = dict(self.report["settings"])
        self.save()

    def fresh_snapshot(self, folder):
        if self.quiet:
            raise RuntimeError("Scope snapshot forbidden inside board acquisition")
        folder = Path(folder)
        folder.mkdir(parents=True, exist_ok=False)
        self.folder = folder
        self.report = dict(resource=self.resource, expected_serial=self.expected_serial,
                           commands=[], channels=[], capture_complete=False,
                           voltage_formula="(code-y_origin-y_reference)*y_increment",
                           time_formula="x_origin + (start-1 + index - x_reference) * x_increment",
                           identity=self.identity, settings=self.settings,
                           physical_lock_qualified=False)
        self.sampling = True
        self.scope.timeout = 1800
        begun = time.monotonic()
        try:
            self.write(":STOP")
            if self.query(":TRIG:STAT?") != "STOP":
                raise ValueError("Scope failed to STOP")
            self.write(":TRIG:EDGE:LEV 7")
            if not math.isclose(float(self.query(":TRIG:EDGE:LEV?")), 7.0):
                raise ValueError("Fresh-arm inhibit threshold rejected")
            self.write(":SING")
            deadline = time.monotonic() + 1.0
            while self.query(":TRIG:STAT?") != "WAIT":
                if time.monotonic() >= deadline:
                    raise TimeoutError("Fresh single WAIT was not observed")
                time.sleep(.01)
            if self.query(":TRIG:SWE?") != "SING" or self.query(":TRIG:EDGE:SOUR?") != "CHAN1":
                raise ValueError("Fresh arm source/mode mismatch")
            if not self.query(":SYST:ERR?").startswith(("0,", "+0,")):
                raise ValueError("Fresh arm rejected")
            self.report["fresh_wait_ns"] = time.monotonic_ns()
            self.report["armed_ns"] = time.monotonic_ns()
            self.write(":TRIG:EDGE:LEV 1.5")
            self.report["trigger_admitted_ns"] = time.monotonic_ns()
            deadline = time.monotonic() + 1.0
            while self.query(":TRIG:STAT?") != "STOP":
                if time.monotonic() >= deadline:
                    raise TimeoutError("No new NO1-triggered complete acquisition")
                time.sleep(.01)
            if not math.isclose(float(self.query(":TRIG:EDGE:LEV?")), 1.5):
                raise ValueError("Trigger admission threshold rejected")
            self.export()
            return self.report
        except Exception as exc:
            self.report["error"] = repr(exc)
            raise
        finally:
            self.report["elapsed_s"] = time.monotonic() - begun
            self.save()
