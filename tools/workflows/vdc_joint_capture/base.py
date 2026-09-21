"""Tracked four-board probe base with bounded command and STOP semantics."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import time
from types import SimpleNamespace

from serial.tools.list_ports import comports

from tools.calibration_ring_validate import trn03_closed_loop as trn
from tools.calibration_ring_validate.tdma_board_record import (
    decode_record, record_status)
from tools.domains.vdc.priority_trace import probe_support as tracked_probe_support
from tools.domains.vdc.priority_trace.probe_support import (
    IDS, OBSERVER_FIELDS, TRACE_FIELDS, bind_receipt, fields,
    native_input_summary, observer_delta, sha, uints)
from tools.dpll_observation_capture import dpll_observation_capture as capture
from tools.hardware_acceptance.p3_hardware_acceptance import (
    working_source_fingerprint, validate_ota)


ROOT = Path(__file__).resolve().parents[3]
BACKEND = sys.modules.get(trn.board_command.__module__)
if BACKEND is None or not hasattr(BACKEND, "_PERSISTENT_CONNECTIONS"):
    raise RuntimeError("TDMA backend owner is unavailable")
PIPE_QUERIES = {
    "observer": "SYSTem:SYNC:VDC:OBServer:RING?",
    "dpll": "SYSTem:SYNC:VDC:DPLL:STATus?",
    "role": "SYSTem:SYNC:VDC:DPLL:ROLE:STATus?",
    "rx": "SYSTem:REFMEM:SYNC:TDMA:VDC:RX?",
}
STOP = "SYSTem:TDMA:RING:STOP"
TRACE = "SYSTem:SYNC:VDC:DPLL:TRACe:"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def close_connections(uids):
    failures = []
    for uid in tuple(uids):
        connection = BACKEND._PERSISTENT_CONNECTIONS.pop(uid, None)
        if connection is None:
            continue
        try:
            connection.close()
        except Exception as exc:
            failures.append(f"{uid}: {type(exc).__name__}: {exc}")
    require(not failures, f"Connection close failures: {failures}")


class Probe:
    def __init__(self, options):
        self.opt = options
        self.opt.out.mkdir(parents=True, exist_ok=False)
        self.args = argparse.Namespace(
            baud=115200, timeout=3.0, read_timeout=0.005,
            action_timeout=0.25, settle=0.02, keep_open=True,
            short_open=False, arm_wait=3.0)
        self.deadline = time.monotonic() + 240.0
        self.block_queries = True
        self.barrier = False
        self.boards = []
        self.record_armed = set()
        self.trace_armed = set()
        self.original_command = trn.board_command
        source = Path(__file__).resolve()
        self.report = {
            "schema": "HAOFV_FOUR_BOARD_DPLL_INPUT_PROBE_V1",
            "utc": datetime.now(timezone.utc).isoformat(),
            "passed": False,
            "flow_completed": False,
            "hardware_started": False,
            "no5_operated": False,
            "ota_operated": False,
            "queries_during_acquisition": 0,
            "actions": [],
            "errors": [],
            "boards": {},
            "plan": {
                "bootstrap_s": 1.0, "autonomous_settle_s": 1.0,
                "trace_window_s": 4.5, "trial_s": 15,
                "rearm_ticks": 8192, "abort_polls": 256,
                "record_interval_us": 300000, "record_count": 20,
                "total_work_deadline_s": 240, "cleanup_stop_passes": 2,
                "no_first_frame_gate": True,
                "no_fixed_good_sample_gate": True,
            },
            "dpll_locked_qualified": False,
            "output_precision_qualified": False,
            "timing_gate_waived": False,
            "resource_order": (
                "DPLL fixed SRAM ARM may overlap TDMA record lease. After all STOP ACKs: "
                "TDMA save/readback and SAVED, then DPLL SAVE/readback."),
        }
        sources = (
            source, Path(tracked_probe_support.__file__),
            Path(capture.__file__), Path(trn.__file__),
            Path(BACKEND.__file__),
            ROOT / "tools/calibration_ring_validate/tdma_board_record.py",
            (ROOT / "tools/domains/vdc/dpll_observation_decode/"
             "dpll_observation_decode.py"),
        )
        self.report["tool_sources"] = [
            {"path": str(path), "sha256": sha(path)} for path in sources]

    def save(self):
        (self.opt.out / "input-probe.json").write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")

    def error(self, scope, exc):
        self.report["errors"].append({
            "scope": scope, "error": f"{type(exc).__name__}: {exc}"})
        self.save()

    def command(self, board, text, unused=None):
        row = {
            "uid": board.address, "port": board.port, "command": text,
            "start_ns": time.monotonic_ns(),
            "utc": datetime.now(timezone.utc).isoformat(),
        }
        self.report["actions"].append(row)
        try:
            require(board.address in IDS, "Board outside the four-UID allowlist")
            if "?" in text and self.block_queries:
                self.report["queries_during_acquisition"] += 1
                raise RuntimeError("Query forbidden before all STOP control ACKs")
            cleanup_control = text in (
                STOP, TRACE + "STOP", "CALibration:ORIGin:REVOKe")
            if not cleanup_control and (
                    time.monotonic() > self.deadline or
                    len(self.report["actions"]) > 2000):
                raise TimeoutError("Bounded probe work budget exhausted")
            connection = BACKEND._PERSISTENT_CONNECTIONS.get(board.address)
            require(connection is not None and connection.session is not None,
                    "Persistent session missing; implicit IDN/reopen forbidden")
            if "FILE:READ?" in text.upper():
                offset = int(fields(text.split("?", 1)[1])[1])
                require(offset < 65536,
                        "Bounded file download exceeded 64 KiB")
            row["raw"] = self.original_command(board, text, self.args)
            return row["raw"]
        except Exception as exc:
            row["error"] = f"{type(exc).__name__}: {exc}"
            raise
        finally:
            row["end_ns"] = time.monotonic_ns()
            with (self.opt.out / "actions.jsonl").open(
                    "a", encoding="utf-8") as stream:
                stream.write(json.dumps(row, ensure_ascii=False) + "\n")

    def control(self, board, text, expected="OK"):
        raw = self.command(board, text)
        normalized = raw.strip().strip('"')
        no_payload = normalized.startswith(
            "OK(no payload; verified by state readback)")
        require(normalized == str(expected) or no_payload,
                f"Control ACK mismatch: {text}: {raw!r}")
        return raw

    def trace_control(self, board, action):
        values = fields(self.command(board, TRACE + action))
        require(len(values) == 3 and values[0] == "OK" and
                all(int(value) >= 0 for value in values[1:]),
                f"Trace composite ACK mismatch: {values}")
        return list(map(int, values[1:]))

    def stop_all(self, label, passes=1):
        self.barrier = False
        pending = list(self.boards)
        for attempt in range(passes):
            failed = []
            for board in pending:
                try:
                    self.control(board, STOP)
                except Exception as exc:
                    self.error(
                        f"{label}:stop:{board.address}:attempt{attempt + 1}", exc)
                    failed.append(board)
            pending = failed
            if not pending:
                break
        require(not pending and len(self.boards) == len(IDS),
                "STOP ACK barrier incomplete; all readback remains prohibited")
        self.block_queries = False
        for index, board in enumerate(self.boards):
            state = trn.wait_runtime_stopped(board, self.args, index)
            self.report["boards"][board.address][label] = state
        self.barrier = True
        self.save()

    def poll_record(self, board, states, seconds=3):
        end = min(self.deadline, time.monotonic() + seconds)
        last = None
        while time.monotonic() < end:
            last = record_status(self.command(
                board, "SYSTem:TDMA:RECord:STATus?"))
            if last and last["state"] in states:
                return last
            if last and last["state"] == 9:
                raise RuntimeError(
                    f"Recorder failed; SRAM retained, no re-ARM: {last}")
            time.sleep(0.05)
        raise TimeoutError(f"Recorder state deadline: {last}")

    def export_tdma(self, board, label, expected_epoch=None):
        require(self.barrier, "TDMA export requires acknowledged STOP state")
        self.control(board, "SYSTem:TDMA:RECord:CANCel")
        status = self.poll_record(board, {0, 5, 8})
        row = {"initial_status": status}
        self.report["boards"][board.address][label + "_tdma"] = row
        if status["state"] == 0:
            require(expected_epoch is None, "Requested TDMA recording absent")
            row["reason"] = "no_prior_record"
            return
        if expected_epoch is not None:
            require(status["epoch"] == expected_epoch,
                    "TDMA recording epoch differs from this run")
        epoch = status["epoch"]
        raw_path = self.opt.out / f"{label}_{board.address}_tdma.bin"
        if status["state"] == 5:
            from tools.calibration_ring_validate.tdma_board_record import export_frozen
            row["ram"] = export_frozen(
                board, self.args, self.command, epoch=epoch,
                path=raw_path.with_suffix(".ram.bin"))
            self.control(board, "SYSTem:TDMA:RECord:SAVE")
            status = self.poll_record(board, {8}, seconds=12)
        row["saved_status"] = status
        sd_path = f"/logs/tdma_{epoch}.bin"
        data = capture.download(board, sd_path, self.args)
        raw_path.write_bytes(data)
        decoded = decode_record(
            data, expected_build=self.args.expected_build,
            expected_board=int(board.address, 16), expected_epoch=epoch)
        if "ram" in row:
            require(Path(row["ram"]["path"]).read_bytes() == data,
                    "TDMA SD readback differs from frozen SRAM")
        row.update(
            sd_path=sd_path, path=str(raw_path), sha256=sha(raw_path),
            bytes=len(data), native=decoded,
            input_summary=native_input_summary(decoded))
        self.save()

    def export_trace(self, board, label):
        require(self.barrier, "Trace export requires acknowledged STOP state")
        status = uints(self.command(board, TRACE + "STATus?"), TRACE_FIELDS)
        row = {"status": status}
        self.report["boards"][board.address][label + "_trace"] = row
        require(not status["armed"], "Trace is still armed")
        if not status["sample_count"]:
            row["reason"] = (
                "zero_update_records_preserved; no causal or lock conclusion "
                "from trace alone")
            self.save()
            return
        job_id, sd_path, count = capture.parse_save(
            self.command(board, TRACE + "SAVE"))
        row.update(job_id=job_id, sd_path=sd_path, sample_count=count)
        named = SimpleNamespace(
            **vars(board), name="NO" + str(IDS.index(board.address) + 1))
        row["job"] = capture.wait_job(named, job_id, self.args)
        data = capture.download(board, sd_path, self.args)
        path = self.opt.out / f"{label}_{board.address}_dpll.bin"
        path.write_bytes(data)
        decoded = capture.decode(path, named.name)
        require(count == status["sample_count"],
                "Trace SAVE count changed after freeze")
        row.update(
            path=str(path), sha256=sha(path), bytes=len(data), decoded=decoded,
            coverage_limit=(
                "TRACE ARM can first record an already-published update_seq. Nonzero "
                "record_count alone is not a new autonomous update. Independently "
                "compare sequence/time/evidence with baseline/end counters and TDMA "
                "native persona samples; bootstrap is included in pipeline deltas. "
                "Host delay is not proof of valid autonomous input."))
        self.save()

    def pipeline(self, board, label):
        raw = {key: self.command(board, text)
               for key, text in PIPE_QUERIES.items()}
        value = {
            "raw": raw,
            "observer": uints(raw["observer"], OBSERVER_FIELDS),
            "role": capture.parse_role_status(raw["role"]),
            "rx": capture.parse_refmem_vdc_follower_rx(raw["rx"]),
        }
        self.report["boards"][board.address][label] = value
        return value

    def configure(self):
        generation = self.receipt["calibration_generation"]
        BACKEND.COMPOSITE_ACK_HEADERS.add("SYSTEM:TDMA:FLIGHT:TX")
        for index, board in enumerate(self.boards):
            row = self.report["boards"][board.address]
            row["original_schedule_raw"] = self.command(
                board, "SYSTem:TDMA:SCHEDule?")
            row["original_load_mask"] = int(self.command(
                board, "SYSTem:TDMA:LOAD:MASK?").strip().strip('"'), 0)
            target = (row["original_load_mask"] |
                      trn.APP_REALTIME_LOAD_VDC_MASK |
                      trn.APP_REALTIME_LOAD_DPLL_MASK)
            if target != row["original_load_mask"]:
                row["load_mask_change_requested"] = True
                self.set_load_mask(board, target)
            row["enabled_schedule_raw"] = self.command(
                board, "SYSTem:TDMA:SCHEDule?")
            schedule = list(map(int, fields(row["enabled_schedule_raw"])))
            require(schedule[4] == target and not schedule[5] & 3,
                    "VDC/DPLL disabled or quarantined")
            row["original_diagnostic"] = int(self.command(
                board, "SYSTem:TDMA:RING:DIAGnostic?").strip('"'))
            if index == 0:
                row["original_burst"] = int(self.command(
                    board, "SYSTem:TDMA:RING:BURSt?").strip('"'))
            self.control(board, "SYSTem:TDMA:RING:GEOMetry 0", 0)
            self.control(board, "SYSTem:TDMA:FLIGHT:FIFO:RESet")
            require(not trn.validate_fifo_reset(
                trn.flight_snapshot(board, self.args)),
                "Stopped FIFO reset not acknowledged")
            self.control(board, "SYSTem:TDMA:RING:DIAGnostic 1", 1)
            if index == 0:
                self.control(board, "SYSTem:TDMA:RING:BURSt 0", 0)
            seed = 0x40 + index * 0x10
            values = fields(self.command(
                board,
                f"SYSTem:TDMA:FLIGHT:TX 32,{seed},{generation},{index + 1},1"))
            expected = [
                32, sum((seed + offset) & 255 for offset in range(32)),
                seed, generation, index + 1, 1]
            require(values[0] == "OK" and
                    list(map(int, values[1:])) == expected,
                    "TX prime composite ACK mismatch")
        for board in reversed(self.boards):
            index = IDS.index(board.address)
            row = self.report["boards"][board.address]
            row["arm"] = trn.arm_with_evidence(board, self.args, index)
            row["ready_config"] = trn.wait_runtime_config_applied(
                board, self.args, index)
        self.report["clock_hz"] = int(fields(self.command(
            self.boards[0], "SYSTem:TDMA:SCHEDule?"))[1])
        require(0 < self.report["clock_hz"] <= 500000000,
                "Invalid schedule clock")

    def set_load_mask(self, board, mask):
        require(self.barrier,
                "Load mask control requires all-board stopped state")
        response = fields(self.command(
            board, f"SYSTem:TDMA:LOAD:MASK {mask}"))
        require(response == ["OK", str(mask)],
                f"Load mask composite ACK mismatch: {response}")
        actual = int(self.command(
            board, "SYSTem:TDMA:LOAD:MASK?").strip().strip('"'), 0)
        require(actual == mask, "Load mask readback differs from request")

    def acquire(self):
        epoch = self.report["epoch"] = int(time.time()) & 0xFFFFFFFF
        for board in self.boards:
            self.record_armed.add(board.address)
            self.control(
                board, f"SYSTem:TDMA:RECord:ARM {epoch},300000,20")
            status = self.poll_record(board, {3})
            require(status["epoch"] == epoch, "TDMA ARM epoch mismatch")
            self.pipeline(board, "baseline")
        self.block_queries = True
        self.barrier = False
        for board in reversed(self.boards):
            self.control(board, "SYSTem:TDMA:RING:START")
        time.sleep(1.0)
        hz = self.report["clock_hz"]
        trial = int(self.command(
            self.boards[0],
            f"CALibration:ORIGin:TRIAL {epoch},8192,256,{hz * 15}"
        ).strip().strip('"'))
        require(trial > 0 and not trial & 1,
                "TRIAL grant response invalid")
        self.report["trial_epoch"] = trial
        time.sleep(1.0)
        for board in self.boards:
            self.trace_armed.add(board.address)
            row = self.report["boards"][board.address]
            row["trace_arm_ack"] = self.trace_control(board, "ARM")
            row["trace_arm_action"] = dict(self.report["actions"][-1])
        time.sleep(4.5)

    def collect(self):
        for board in self.boards:
            try:
                self.trace_control(board, "STOP")
                after = self.pipeline(board, "end")
                row = self.report["boards"][board.address]
                row["end_schedule_raw"] = self.command(
                    board, "SYSTem:TDMA:SCHEDule?")
                before = row.get("baseline")
                if before:
                    row["pipeline_deltas"] = {
                        "observer": observer_delta(
                            before["observer"], after["observer"]),
                        "role": capture.role_status_delta(
                            before["role"], after["role"]),
                        "rx": capture.refmem_vdc_follower_rx_delta(
                            before["rx"], after["rx"]),
                        "scope": (
                            "Baseline-to-post-STOP cumulative deltas; includes bootstrap "
                            "and cleanup. Post-STOP last_result is not a RUN rejection cause."),
                    }
            except Exception as exc:
                self.error("pipeline:" + board.address, exc)
        for key, text in (
                ("release", "READ:CALibration:ORIGin:RELease?"),
                ("handoff", "READ:CALibration:ORIGin:HANDoff?"),
                ("origin", "READ:CALibration:ORIGin:RUNTIme?")):
            try:
                self.report[key + "_raw"] = self.command(self.boards[0], text)
            except Exception as exc:
                self.error(key, exc)
        for board in self.boards:
            if board.address not in self.record_armed:
                continue
            try:
                self.export_tdma(board, "run", self.report["epoch"])
            except Exception as exc:
                self.error("tdma_export:" + board.address, exc)
                continue
            try:
                if board.address in self.trace_armed:
                    self.export_trace(board, "run")
            except Exception as exc:
                self.error("trace_export:" + board.address, exc)

    def run(self):
        stage_owner = sys.modules.get("trn03_stage")
        require(stage_owner is not None and hasattr(stage_owner, "board_command"),
                "trn03_stage owner is unavailable")
        original_stage = stage_owner.board_command
        original_capture_query = capture.query
        opened = set()
        try:
            self.receipt, self.report["binding"] = bind_receipt(
                ROOT, self.opt.p3, working_source_fingerprint, validate_ota)
            self.args.expected_build = int(self.receipt["build_id"])
            if self.opt.check_only:
                self.report.update(check_only=True, passed=True)
                return
            ports = {}
            for port in comports():
                if port.serial_number in IDS:
                    require(port.serial_number not in ports,
                            "Duplicate USB UID")
                    ports[port.serial_number] = port.device
            require(set(ports) == set(IDS),
                    f"Four-board USB UID scan incomplete: {ports}")
            self.boards = [
                trn.Board(ports[uid], uid, "", self.receipt["build_id"])
                for uid in IDS]
            self.report["ports"] = ports
            trn.board_command = self.command
            stage_owner.board_command = self.command
            capture.query = self.command
            for board in self.boards:
                self.report["boards"][board.address] = {}
                connection = BACKEND.BoardConnection(board, self.args)
                BACKEND._PERSISTENT_CONNECTIONS[board.address] = connection
                opened.add(board.address)
                connection.session = BACKEND.SerialSession(
                    board.port, self.args.baud, self.args.timeout,
                    self.args.settle, read_timeout_s=self.args.read_timeout)
                connection.session.open()
            self.report["hardware_started"] = True
            self.stop_all("initial_stop")
            for index, board in enumerate(self.boards):
                require(board.address in self.command(board, "*IDN?"),
                        "Online UID mismatch")
                require(self.command(
                    board, "SYSTem:FW:BUILD?").strip().strip('"') == board.build,
                    "Online build mismatch")
                require(int(self.command(board, "SYSTem:BOARD:NO?")) == index + 1,
                        "Online board number mismatch")
                self.trace_control(board, "STOP")
            for board in self.boards:
                self.export_tdma(board, "prior")
                self.export_trace(board, "prior")
            self.configure()
            self.acquire()
        except Exception as exc:
            self.error("run", exc)
        finally:
            if self.report["hardware_started"]:
                try:
                    self.stop_all("final_stop", passes=2)
                except Exception as exc:
                    self.error("final_stop", exc)
                if self.barrier:
                    try:
                        self.collect()
                    except Exception as exc:
                        self.error("final_collect", exc)
                    for board in self.boards:
                        row = self.report["boards"][board.address]
                        cleanup = []
                        if "original_diagnostic" in row:
                            cleanup.append((
                                "diagnostic", lambda board=board, row=row:
                                self.control(
                                    board,
                                    f"SYSTem:TDMA:RING:DIAGnostic {row['original_diagnostic']}",
                                    row["original_diagnostic"])))
                        if "original_burst" in row:
                            cleanup.append((
                                "burst", lambda board=board, row=row:
                                self.control(
                                    board,
                                    f"SYSTem:TDMA:RING:BURSt {row['original_burst']}",
                                    row["original_burst"])))
                        if row.get("load_mask_change_requested"):
                            cleanup.append((
                                "load-mask", lambda board=board, row=row:
                                self.set_load_mask(board, row["original_load_mask"])))
                        for name, action in cleanup:
                            try:
                                action()
                                if name == "load-mask":
                                    row["load_mask_restored"] = row["original_load_mask"]
                            except Exception as exc:
                                self.error(
                                    f"restore:{board.address}:{name}", exc)
                    try:
                        self.control(self.boards[0], "CALibration:ORIGin:REVOKe")
                    except Exception as exc:
                        self.error("revoke", exc)
            try:
                close_connections(opened)
            except Exception as exc:
                self.error("close", exc)
            finally:
                trn.board_command = self.original_command
                stage_owner.board_command = original_stage
                capture.query = original_capture_query
            if not self.opt.check_only:
                self.report["flow_completed"] = all(
                    "run_tdma" in self.report["boards"].get(uid, {}) and
                    "run_trace" in self.report["boards"].get(uid, {})
                    for uid in IDS)
                self.report["passed"] = (
                    self.report["flow_completed"] and not self.report["errors"])
            self.report["conclusion_scope"] = (
                "Acquisition/export status only. Zero trace is preserved. Native "
                "timestamps, pipeline deltas and actual trace coverage require "
                "independent diagnosis; no lock, formal accuracy, perfect window or "
                "timing qualification is granted.")
            self.save()


def load_base():
    """Return the tracked base namespace without loading archived evidence code."""
    source = Path(__file__).resolve()
    return SimpleNamespace(
        __file__=str(source), Probe=Probe, BACKEND=BACKEND, trn=trn,
        stage_owner=sys.modules.get("trn03_stage"),
        scpi_serial=sys.modules.get(BACKEND.SerialSession.__module__),
        IDS=IDS, OBSERVER_FIELDS=OBSERVER_FIELDS, TRACE_FIELDS=TRACE_FIELDS,
        PIPE_QUERIES=PIPE_QUERIES, STOP=STOP, TRACE=TRACE,
        fields=fields, sha=sha, uints=uints,
        native_input_summary=native_input_summary,
        observer_delta=observer_delta, comports=comports,
        close_connections=close_connections,
        source_path=source,
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    )
