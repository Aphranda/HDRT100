"""Tracked four-board STOP, child-capture, restore, and close lifecycle."""

from __future__ import annotations

from contextlib import contextmanager
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

from tools.common.scpi import scpi_serial
from tools.common.scpi.board_identity import parse_idn_response
from tools.tdma_ring_monitor.tdma_field_parse import RUNTIME_FIELDS


OUTPUT = "SYSTem:VDC:OUTPut:RUN"
TIMING = "SYSTem:VDC:OUTPut:TIMing"
DELAY = "SYSTem:VDC:OUTPut:DELay"
BASELINE = "SYSTem:VDC:PRIORity:FOLLow:BASEline?"
STOP = "SYSTem:TDMA:RING:STOP"
STATUS = "SYSTem:TDMA:RING:STATus?"
ERROR = "SYSTem:ERR?"
PI = "SYSTem:SYNC:VDC:DPLL:COEFficient?"
ROLE = "SYSTem:SYNC:VDC:DPLL:ROLE?"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def integers(raw, count, signed=False):
    fields = next(csv.reader([raw.strip()]))
    require(len(fields) == count, f"Expected {count} fields: {raw!r}")
    result = []
    for field in fields:
        require(re.fullmatch(r"-?\d+" if signed else r"\d+", field.strip()),
                f"Invalid integer: {raw!r}")
        value = int(field)
        require(-(1 << 31) <= value <= 0xFFFFFFFF if signed else
                0 <= value <= 0xFFFFFFFF, f"Integer out of range: {raw!r}")
        result.append(value)
    return tuple(result)


def timing_tuple(raw):
    value = integers(raw, 3)
    plan, commit, low = value
    require(1000 <= low < plan <= commit <= 1_000_000,
            f"Invalid output-timing readback: {raw!r}")
    return value


def error_code(raw):
    require(re.fullmatch(r"-?\d+(?:,.*)?", raw.strip()),
            f"Invalid error response: {raw!r}")
    return int(raw.split(",")[0])


def stopped(raw):
    row = dict(zip(RUNTIME_FIELDS, integers(raw, len(RUNTIME_FIELDS), True)))
    return (row["ring_enabled"] == row["ring_adapter_started"] == 0 and
            row["ring_config_seq"] == row["ring_applied_config_seq"])


def response_matches(command, line):
    """Recognize timing/delay responses without changing global SCPI state."""
    header = command.strip().split(maxsplit=1)[0].upper()
    text = scpi_serial.strip_scpi_ack_prefix(
        scpi_serial.trim_embedded_scpi_log(line)).strip()
    if header in (TIMING.upper(), TIMING.upper() + "?"):
        return re.fullmatch(r"\d+\s*,\s*\d+\s*,\s*\d+", text) is not None
    if header in (DELAY.upper(), DELAY.upper() + "?"):
        return (re.fullmatch(r"-?\d+", text) is not None and
                -(1 << 31) <= int(text) < (1 << 31))
    return scpi_serial.scpi_response_matches_command(command, line)


def allowed_ports(ports, board_uids):
    """Return one dynamic COM mapping for each authorized USB UID only."""
    allowed = set(board_uids)
    found = {}
    for port in ports:
        uid = (port.serial_number or "").upper()
        if uid not in allowed:
            continue
        require(uid not in found, f"Duplicate USB UID {uid}")
        found[uid] = port.device
    return found


class SerialBackend:
    def __init__(self, board_uids, timeout_s=3.0, *, ports_provider=None,
                 session_factory=None):
        self.board_uids = tuple(board_uids)
        require(self.board_uids and len(set(self.board_uids)) == len(self.board_uids),
                "Board UID order must be nonempty and unique")
        self.sessions = {}
        self.ports = {}
        self.timeout_s = timeout_s
        self._ports_provider = ports_provider
        self._session_factory = session_factory

    def _available_ports(self):
        if self._ports_provider is not None:
            return self._ports_provider()
        from serial.tools import list_ports
        return list_ports.comports()

    def _new_session(self, port):
        if self._session_factory is not None:
            return self._session_factory(port)
        return scpi_serial.SerialSession(
            port, 115200, self.timeout_s, .05, read_timeout_s=.02,
            response_matcher=response_matches)

    def open(self, uid):
        require(uid in self.board_uids, f"Unauthorized UID {uid}")
        ports = allowed_ports(self._available_ports(), self.board_uids)
        require(uid in ports, f"USB UID missing: {uid}")
        session = self._new_session(ports[uid])
        self.ports[uid] = ports[uid]
        try:
            self.sessions[uid] = session.open()
        except Exception:
            self.sessions.pop(uid, None)
            try:
                session.close()
            except Exception:
                pass
            raise
        return ports[uid]

    def execute(self, uid, command):
        require(uid in self.sessions, f"USB UID is not open: {uid}")
        return self.sessions[uid].execute(command)

    def close(self, uid):
        session = self.sessions.pop(uid, None)
        if session is not None:
            session.close()

    def sleep(self, seconds):
        time.sleep(seconds)


def child_capture(command, timeout, output):
    """Run a child with isolated logs; timeout kills and waits for it."""
    with ((output / "child.stdout.log").open("x", encoding="utf-8") as stdout,
          (output / "child.stderr.log").open("x", encoding="utf-8") as stderr):
        return subprocess.run(
            command, timeout=timeout, stdout=stdout, stderr=stderr).returncode


class _Acceptance:
    def __init__(self, backend, receipt, output, board_uids, deadline_s=120.0):
        self.backend = backend
        self.receipt = receipt
        self.output = Path(output)
        self.board_uids = tuple(board_uids)
        require(receipt.get("tdma_board_ids") == list(self.board_uids),
                "Receipt board UID order differs")
        require(str(receipt.get("build_id", "")).isdigit(), "Invalid receipt build ID")
        self.output.mkdir(parents=True, exist_ok=False)
        source_sha256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        self.report = {
            "schema": "DPLL_RAM_TIMING_TRIAL_V1",
            "lifecycle_version": "VDC_JOINT_OUTER_LIFECYCLE_V1",
            "basis_script_sha256": source_sha256,
            "passed": False,
            "complete": False,
            "main_completed": False,
            "boards": {},
            "stages": [],
            "errors": [],
            "cleanup_errors": [],
            "commands": [],
            "receipt_build": receipt["build_id"],
            "source_tree_sha256": receipt["source_tree_sha256"],
            "lifecycle_source": {
                "path": Path(__file__).resolve().relative_to(
                    Path(__file__).resolve().parents[3]).as_posix(),
                "sha256": source_sha256,
            },
            "limitations": [
                "RAM timing and output delay only; no Flash or reboot",
                "Child evidence determines capture outcome",
                "No GPIO phase-lock claim",
            ],
        }
        self.opened = set()
        self.verified = set()
        self.initial = {}
        self.stage_name = "setup"
        self.deadline = time.monotonic() + deadline_s

    def save(self):
        (self.output / "report.json").write_text(
            json.dumps(self.report, indent=2) + "\n", encoding="utf-8")

    @contextmanager
    def stage(self, name):
        previous, self.stage_name = self.stage_name, name
        row = {"name": name, "start_ns": time.monotonic_ns(), "passed": False}
        self.report["stages"].append(row)
        try:
            yield
            row["passed"] = True
        except (Exception, KeyboardInterrupt) as exc:
            row["error"] = repr(exc)
            self.failure_stage = name
            raise
        finally:
            row["end_ns"] = time.monotonic_ns()
            row["duration_s"] = (row["end_ns"] - row["start_ns"]) / 1e9
            self.stage_name = previous
            self.save()

    def command(self, uid, command):
        require(time.monotonic() < self.deadline, "Acceptance deadline exhausted")
        require(uid in self.board_uids, f"Unauthorized UID {uid}")
        row = {"uid": uid, "command": command, "stage": self.stage_name,
               "start_ns": time.monotonic_ns()}
        self.report["commands"].append(row)
        try:
            row["raw"] = self.backend.execute(uid, command)
            return row["raw"].strip()
        except Exception as exc:
            row["error"] = repr(exc)
            raise
        finally:
            row["end_ns"] = time.monotonic_ns()
            row["duration_s"] = (row["end_ns"] - row["start_ns"]) / 1e9
            with (self.output / "commands.jsonl").open("a", encoding="utf-8") as log:
                log.write(json.dumps(row) + "\n")
            self.save()

    def ok(self, uid, command):
        raw = self.command(uid, command)
        require(raw.strip('"') == "OK", f"{command} rejected: {raw!r}")

    def open_board(self, uid):
        self.verified.discard(uid)
        event = {"uid": uid, "action": "open", "stage": self.stage_name,
                 "start_ns": time.monotonic_ns()}
        try:
            port = self.backend.open(uid)
            self.opened.add(uid)
            self.report["boards"].setdefault(uid, {}).setdefault("ports", []).append(port)
        except Exception as exc:
            event["error"] = repr(exc)
            try:
                self.backend.close(uid)
            except Exception as close_error:
                event["close_error"] = repr(close_error)
            self.opened.discard(uid)
            raise
        finally:
            event["end_ns"] = time.monotonic_ns()
            self.report.setdefault("connection_events", []).append(event)
            self.save()

    def close_all(self):
        failures = []
        for uid in self.board_uids:
            event = {"uid": uid, "action": "close", "stage": self.stage_name,
                     "start_ns": time.monotonic_ns()}
            try:
                self.backend.close(uid)
            except Exception as exc:
                event["error"] = repr(exc)
                failures.append(f"{uid}: {exc!r}")
            finally:
                self.opened.discard(uid)
                self.verified.discard(uid)
                event["end_ns"] = time.monotonic_ns()
                self.report.setdefault("connection_events", []).append(event)
                self.save()
        require(not failures, f"Close failures: {failures}")

    def wait_stopped(self, uid):
        for _ in range(40):
            if stopped(self.command(uid, STATUS)):
                return
            self.backend.sleep(.05)
        raise RuntimeError(f"{uid}: STOP was not applied")

    def identity(self, uid):
        idn = self.command(uid, "*IDN?")
        require(parse_idn_response(idn).address == uid, f"IDN UID mismatch: {idn!r}")
        build = self.command(uid, "SYSTem:FW:BUILD?").strip('"')
        require(build == str(self.receipt["build_id"]), f"Build mismatch: {build!r}")
        number = integers(self.command(uid, "SYSTem:BOARD:NO?"), 1)[0]
        require(number == self.board_uids.index(uid) + 1, f"BOARD:NO mismatch: {number}")
        self.verified.add(uid)
        return {"idn": idn, "build": build, "board_number": number}

    def barrier(self):
        failures = []
        for uid in self.board_uids:
            try:
                if uid not in self.opened:
                    self.open_board(uid)
            except Exception as exc:
                failures.append(f"{uid}: open: {exc!r}")
        for command in (OUTPUT + ":STOP", STOP):
            for uid in self.board_uids:
                try:
                    require(uid in self.opened, f"{uid}: no connection")
                    self.ok(uid, command)
                except Exception as exc:
                    failures.append(f"{uid}: {command}: {exc!r}")
        for uid in self.board_uids:
            try:
                require(uid in self.opened, f"{uid}: no connection")
                self.wait_stopped(uid)
                self.identity(uid)
                for _ in range(40):
                    fields = self.command(uid, OUTPUT + "?").split(",")
                    require(len(fields) >= 3, "Incomplete output status")
                    if integers(",".join(fields[:3]), 3)[2] in (0, 4):
                        break
                    self.backend.sleep(.05)
                else:
                    raise RuntimeError(f"{uid}: output not retired")
            except Exception as exc:
                failures.append(f"{uid}: verify STOP/identity: {exc!r}")
        require(not failures, f"All-board STOP barrier failed: {failures}")
        require(self.verified == set(self.board_uids),
                "All board identities must be verified")

    def snapshot(self, uid):
        return {
            "identity": self.identity(uid),
            "output_timing": timing_tuple(self.command(uid, TIMING + "?")),
            "output_delay": integers(self.command(uid, DELAY + "?"), 1, True),
            "baseline": integers(self.command(uid, BASELINE), 2),
            "pi": integers(self.command(uid, PI), 9, True),
            "role": integers(self.command(uid, ROLE), 5),
        }

    def unchanged(self, uid, value):
        original = self.initial[uid]
        require(value["identity"] == original["identity"], f"{uid}: identity changed")
        require(value["pi"][:6] == original["pi"][:6], f"{uid}: PI profile changed")
        require(value["role"][:2] == original["role"][:2], f"{uid}: role changed")
        require(value["baseline"] == original["baseline"], f"{uid}: baseline changed")
        require(value["output_delay"] == original["output_delay"],
                f"{uid}: output delay changed")

    def clean_errors(self, uid):
        require(error_code(self.command(uid, ERROR)) == 0,
                f"{uid}: SCPI error queue not empty")

    def set_timing(self, uid, value):
        require(self.verified == set(self.board_uids),
                "All current identities required before RAM mutation")
        self.dirty.add(uid)
        raw = self.command(uid, TIMING + " " + ",".join(map(str, value)))
        require(timing_tuple(raw) == tuple(value), f"{uid}: SET readback mismatch")
        require(timing_tuple(self.command(uid, TIMING + "?")) == tuple(value),
                f"{uid}: query readback mismatch")
        self.clean_errors(uid)

    def restore_ram(self):
        failures = []
        for uid in self.board_uids:
            if uid not in self.dirty:
                continue
            try:
                require(uid in self.initial, f"{uid}: original RAM unavailable")
                original = self.initial[uid]["output_timing"]
                self.set_timing(uid, original)
                self.report["boards"][uid]["restored"] = original
            except Exception as exc:
                failures.append(f"{uid}: {exc!r}")
        require(not failures, f"RAM restoration failures: {failures}")

    def verify_final(self):
        failures = []
        for uid in self.board_uids:
            try:
                self.wait_stopped(uid)
                value = self.snapshot(uid)
                self.report["boards"][uid]["final"] = value
                require(uid in self.initial, f"{uid}: no initial state for comparison")
                self.unchanged(uid, value)
                require(value["output_timing"] == self.initial[uid]["output_timing"],
                        f"{uid}: requested output timing not restored")
                self.clean_errors(uid)
                self.dirty.discard(uid)
                self.delay_dirty.discard(uid)
            except Exception as exc:
                failures.append(f"{uid}: {exc!r}")
        require(not failures, f"Final verification failures: {failures}")


class ResumeTrial(_Acceptance):
    def __init__(self, backend, receipt, output, profile, capture_command, *,
                 board_uids, output_delays, child=child_capture,
                 capture_timeout=240.0, cleanup_timeout=120.0):
        super().__init__(backend, receipt, output, board_uids)
        require(len(output_delays) == len(self.board_uids),
                "Exactly one output-delay value per board is required")
        timing_tuple(",".join(map(str, profile)))
        self.report.update(profile=list(profile), capture={})
        self.profile = tuple(profile)
        self.capture_command = list(capture_command)
        self.output_delays = tuple(output_delays)
        self.child = child
        self.capture_timeout = capture_timeout
        self.cleanup_timeout = cleanup_timeout
        self.original_delays = {}
        self.dirty = set()
        self.delay_dirty = set()
        self.save()

    def _configure_delays(self, values, phase):
        rows = self.report.setdefault("boards", {})
        for uid, value in zip(self.board_uids, values):
            require(-(1 << 31) <= value < (1 << 31), "Output delay is outside int32")
            row = rows.setdefault(uid, {})
            before = integers(self.command(uid, DELAY + "?"), 1, True)[0]
            if phase == "initial":
                self.original_delays[uid] = before
                row["output_delay_original_ns"] = before
            else:
                # Final handoff, rather than an immediate SET readback, clears
                # this uncertainty after every invariant is checked.
                self.delay_dirty.add(uid)
            applied = integers(self.command(uid, f"{DELAY} {value}"), 1, True)[0]
            check = integers(self.command(uid, DELAY + "?"), 1, True)[0]
            require(applied == value and check == value,
                    f"{uid}: output-delay mismatch requested={value} "
                    f"applied={applied} readback={check}")
            row[f"output_delay_{phase}_requested_ns"] = value
            row[f"output_delay_{phase}_readback_ns"] = check
            self.save()

    def _snapshot_delays(self):
        for uid in self.board_uids:
            value = integers(self.command(uid, DELAY + "?"), 1, True)[0]
            self.original_delays[uid] = value
            self.report["boards"][uid]["output_delay_original_ns"] = value
        self.save()

    def set_timing(self, uid, value):
        super().set_timing(uid, value)
        if self.stage_name == "apply_ram_timing" and uid == self.board_uids[-1]:
            self._configure_delays(self.output_delays, "apply")

    def barrier(self):
        super().barrier()
        if self.stage_name == "initial_output_and_tdma_stop":
            for uid in self.board_uids:
                errors = self.report["boards"][uid]["initial_error_queue"] = []
                for _ in range(32):
                    raw = self.command(uid, ERROR)
                    errors.append(raw)
                    self.save()
                    if error_code(raw) == 0:
                        break
                else:
                    raise RuntimeError(f"{uid}: initial error queue did not drain")
            self._snapshot_delays()
        elif self.stage_name == "final_output_and_tdma_stop":
            require(set(self.original_delays) == set(self.board_uids),
                    "Initial output-delay snapshot is incomplete")
            self._configure_delays(
                tuple(self.original_delays[uid] for uid in self.board_uids), "final")

    def run(self):
        started = time.monotonic_ns()
        try:
            with self.stage("initial_output_and_tdma_stop"):
                self.barrier()
            with self.stage("original_ram_and_invariants"):
                for uid in self.board_uids:
                    self.initial[uid] = self.snapshot(uid)
                    self.report["boards"][uid]["initial"] = self.initial[uid]
                    self.clean_errors(uid)
            with self.stage("apply_ram_timing"):
                for uid in self.board_uids:
                    self.set_timing(uid, self.profile)
                    self.report["boards"][uid]["applied"] = self.profile
            with self.stage("close_before_capture"):
                self.close_all()
            with self.stage("child_capture"):
                self.report["capture"] = {
                    "command": self.capture_command,
                    "timeout_s": self.capture_timeout,
                    "started_ns": time.monotonic_ns(),
                }
                self.save()
                code = self.child(self.capture_command, self.capture_timeout, self.output)
                self.report["capture"].update(
                    returncode=code, ended_ns=time.monotonic_ns())
                require(code == 0, f"Capture exited {code}")
            self.report["main_completed"] = True
        except (Exception, KeyboardInterrupt) as exc:
            self.report["errors"].append({
                "stage": getattr(self, "failure_stage", self.stage_name),
                "error": repr(exc),
            })
        finally:
            self.deadline = time.monotonic() + self.cleanup_timeout
            try:
                with self.stage("final_output_and_tdma_stop"):
                    self.barrier()
            except (Exception, KeyboardInterrupt) as exc:
                self.report["cleanup_errors"].append(
                    {"stage": "final_stop", "error": repr(exc)})
            else:
                for name, action in (("restore_original_ram", self.restore_ram),
                                     ("final_readbacks", self.verify_final)):
                    try:
                        with self.stage(name):
                            action()
                    except (Exception, KeyboardInterrupt) as exc:
                        self.report["cleanup_errors"].append(
                            {"stage": name, "error": repr(exc)})
            try:
                with self.stage("close_final_connections"):
                    self.close_all()
            except (Exception, KeyboardInterrupt) as exc:
                self.report["cleanup_errors"].append(
                    {"stage": "close", "error": repr(exc)})
            restore_needed = self.dirty | self.delay_dirty
            self.report["restore_needed"] = sorted(restore_needed)
            self.report["restore_needed_detail"] = {
                "timing": sorted(self.dirty),
                "delay": sorted(self.delay_dirty),
            }
            self.report["complete"] = True
            self.report["passed"] = (
                self.report["main_completed"] and not self.report["errors"] and
                not self.report["cleanup_errors"] and not restore_needed)
            self.report["duration_s"] = (time.monotonic_ns() - started) / 1e9
            self.save()
        return self.report
