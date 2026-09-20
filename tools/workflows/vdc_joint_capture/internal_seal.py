"""Tracked internal-seal adapter layers over the admitted historical base."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import re
import time
from types import SimpleNamespace

from tools.vdc_priority_trace import vdc_priority_trace_capture as helper


GUARD_FIELDS = (
    "schema capture_id session generation target_s state reason_mask "
    "first_failure_ms checked_s passed_mask elapsed_ms ring_config_seq "
    "stop_config_seq stop_accepted ring_retired output_retired output_request "
    "output_state output_reason output_ordinal_lo output_ordinal_hi"
).split()
OUTPUT_FLAGS = (1 << 22) | (1 << 23) | (1 << 24)
MISSING_FLAGS = 2 | 128 | 32 | (1 << 17)
PLANNED_FIELDS = (
    "planned_calls planned_submissions planned_rebuilds planned_wall_samples "
    "planned_wall_max_cycles planned_budget_overruns"
).split()
PLANNED_BUDGET_CYCLES = 100000


def _load_module(path: Path):
    spec = importlib.util.spec_from_file_location("vdc_joint_runtime_base", path)
    if spec is None or spec.loader is None:
        raise ValueError("Historical runtime entrypoint cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_adapter(runtime_entrypoint: Path, *, runtime_module=None):
    """Create the tracked adapter after manifest admission has completed."""
    external = runtime_module if runtime_module is not None else _load_module(runtime_entrypoint)
    for name in ("Probe", "base", "capture", "follow", "phase", "startup"):
        if not hasattr(external, name):
            raise ValueError(f"Historical runtime base missing {name}")
    capture, follow, phase = external.capture, external.follow, external.phase
    trace = helper.TRACE_ROOT
    run_fields = tuple(capture.FIELDS) + tuple(PLANNED_FIELDS)
    if len(capture.FIELDS) != 119 or len(run_fields) != 125:
        raise ValueError("Historical RUN field contract differs")
    tracked_path = Path(__file__).resolve()
    tracked_sha = hashlib.sha256(tracked_path.read_bytes()).hexdigest()

    def parse_run_output(raw):
        tokens = [value.strip().strip('"') for value in raw.split(",")]
        if (len(tokens) != len(run_fields)
                or not all(re.fullmatch(r"[+-]?[0-9]+", value) for value in tokens)):
            raise ValueError("RUN schema12 requires exactly 125 decimal integer fields")
        run = dict(zip(run_fields, map(int, tokens)))
        if run["schema"] != 12:
            raise ValueError("Expected RUN schema12")
        for name, value in run.items():
            if name == "delay_ns":
                if not -(1 << 31) <= value < (1 << 31):
                    raise ValueError("Signed delay outside int32")
            else:
                bits = 64 if name in capture.WIDE_FIELDS else 32
                if not 0 <= value < (1 << bits):
                    raise ValueError(f"RUN field outside uint{bits}: {name}")
        return run

    def qualify_planned(run):
        checks = {
            "calls": run["planned_calls"] > 0,
            "complete_wall_samples": run["planned_wall_samples"] == run["planned_calls"],
            "rebuilds": run["planned_rebuilds"] > 0,
            "submissions": run["planned_submissions"] > 0,
            "wall_budget": run["planned_wall_max_cycles"] <= PLANNED_BUDGET_CYCLES,
            "no_overruns": run["planned_budget_overruns"] == 0,
        }
        return {
            "diagnostics": {key: run[key] for key in PLANNED_FIELDS},
            "checks": checks,
            "passed": all(checks.values()),
            "budget_cycles": PLANNED_BUDGET_CYCLES,
        }

    def add_replan_report(report):
        rows = []
        for uid, row in report["boards"].items():
            if "run_output_raw" not in row:
                continue
            rows.append({"uid": uid, **qualify_planned(parse_run_output(row["run_output_raw"]))})
        criteria = len(rows) == 4 and all(row["passed"] for row in rows)
        report["planned_path_diagnostics"] = rows
        report["strict_replan_qualification"] = {
            "criteria_passed": criteria,
            "base_health_passed": bool(report["passed"]),
            "passed": bool(criteria and report["passed"]),
            "budget_cycles": PLANNED_BUDGET_CYCLES,
            "scope": "Measured planned CPU path only; not physical GPIO precision or full TDMA WCET.",
        }

    def trial_passed(report, checks, mode):
        return bool(
            not report["errors"] and not report.get("cleanup_errors") and
            report.get("queries_during_acquisition", 0) == 0 and
            len(checks) == 4 and all(check["passed"] for check in checks) and
            (mode != "positive" or report["internal_health_passed"]))

    def evaluate_board(row, decoded, index, seconds, mode):
        guard = row["guard"]
        assessment = row["internal_assessment"]
        arm = row["trace_arm_ack"]["status"]
        status, records = decoded["status"], decoded["records"]
        run = parse_run_output(row["run_output_raw"])
        tick_hz = status["tick_hz"]
        first = records[0]["observed_start_raw"] if records else 0
        no4 = mode == "expiry" and index == 3
        negative = mode != "positive"
        identity = all(
            guard[key] == arm[key] == status[key]
            for key in ("capture_id", "session", "generation"))
        stop = row["trace_stop_ack"]["status"]
        release = row["trace_release_ack"]["status"]
        common = {
            "identity": identity,
            "target": guard["schema"] == 3 and guard["target_s"] == seconds,
            "native": (
                assessment["crc_verified"] and assessment["schema_ok"] and
                assessment["interval_ok"] and assessment["terminal"] and
                assessment["freeze_reason"] == (8 if guard["state"] == 3 else 1) and
                bool(records) and tick_hz > 0),
            "trace_retirement": (
                stop["state"] == 3 and stop["command"] == 2 and
                stop["capture_id"] == guard["capture_id"] and
                stop["request_seq"] == stop["ack_seq"] and
                release["state"] == 0 and release["command"] == 3 and
                release["request_seq"] == release["ack_seq"]),
            "output_identity": (
                run["schema"] == 12 and
                run["generation"] == guard["output_request"] == row["run_request"] and
                row["run_request"] > 0 and run["session"] == guard["session"]),
            "output_retired": (
                run["state"] in (0, 4) and run["pio_enabled"] == 0 and
                run["dma_busy"] == 0),
            "output_duration": run["duration_ms"] == (20000 if no4 else 0),
            "output_reason": run["reason"] == (7 if no4 else 1),
            "output_activity": mode == "missing" or run["edges"] > 0,
        }
        forbidden = {"SERVICE_GAP", "COUNTER_RESET", "CLOCK_INVALID", "COUNTER_SATURATED"}
        common["native_faults"] = not any(
            assessment["flags"].get(key, 0) for key in forbidden)
        common["record_faults"] = all(
            not forbidden.intersection(record["flag_names"]) and
            ("FIELD_SATURATED" not in record["flag_names"] or
             record["max_success_gap_ticks"] == 0xFFFFFFFF)
            for record in records)
        common["coverage_to_decision"] = (
            assessment["observed_s"] + 0.01 >= guard["elapsed_ms"] / 1000)
        if not negative:
            verdict = (
                guard["state"] == 3 and guard["reason_mask"] == 0 and
                guard["checked_s"] == seconds and
                guard["passed_mask"] == (1 << (seconds // 60)) - 1 and
                seconds * 1000 <= guard["elapsed_ms"] < (seconds + 1) * 1000)
            health = (
                assessment["coverage_complete"] and
                assessment["all_bins_have_success"] and assessment["success_gap_passed"])
        elif mode == "missing":
            verdict = (
                guard["state"] == 4 and guard["reason_mask"] & 130 == 130 and
                not guard["reason_mask"] & ~(MISSING_FLAGS | OUTPUT_FLAGS) and
                guard["checked_s"] == 60 and guard["passed_mask"] == 0 and
                60000 <= guard["elapsed_ms"] < 61000 and guard["output_reason"] in (0, 1))
            health = (
                assessment["success_count"] == 0 and
                assessment["flags"]["NO_SUCCESS"] > 0 and
                assessment["flags"]["UNBOUND"] > 0)
        else:
            healthy_prefix = []
            for record in records:
                end = (record["observed_end_raw"] - first) / tick_hz
                if end <= 58 and record["observed_end_raw"] > record["observed_start_raw"]:
                    healthy_prefix.append(record)
                if set(record["flag_names"]) & {"NO_SUCCESS", "UNBOUND", "FIELD_SATURATED"}:
                    if (record["observed_start_raw"] - first) / tick_hz < 58:
                        common["record_faults"] = False
            health = (
                bool(healthy_prefix) and
                max((record["observed_end_raw"] - first) / tick_hz
                    for record in healthy_prefix) >= 49 and
                all(record["success_count"] > 0 and
                    record["max_success_gap_ticks"] <= tick_hz
                    for record in healthy_prefix))
            if no4:
                verdict = (
                    guard["state"] == 4 and guard["checked_s"] == 60 and
                    guard["passed_mask"] == 0 and 60000 <= guard["elapsed_ms"] < 61000 and
                    guard["output_reason"] == 7 and guard["output_state"] in (0, 4) and
                    guard["reason_mask"] == ((1 << 22) | (1 << 23)))
            elif guard["state"] == 3:
                verdict = (
                    guard["reason_mask"] == 0 and guard["checked_s"] == seconds and
                    guard["passed_mask"] == (1 << (seconds // 60)) - 1 and
                    seconds * 1000 <= guard["elapsed_ms"] < (seconds + 1) * 1000)
            else:
                verdict = (
                    guard["state"] == 4 and guard["checked_s"] == 120 and
                    guard["passed_mask"] == 1 and
                    120000 <= guard["elapsed_ms"] < 121000 and
                    guard["reason_mask"] != 0 and
                    not guard["reason_mask"] & ~(MISSING_FLAGS | OUTPUT_FLAGS) and
                    bool(guard["reason_mask"] & (2 | 128 | (1 << 17))) and
                    guard["output_reason"] in (0, 1))
        common.update(
            verdict=verdict,
            health=health,
            target_seal=(
                guard["state"] != 3 or
                (status["state"] == 3 and status["reason"] == 8 and
                 assessment["observed_s"] >= seconds and
                 assessment["observed_s"] < seconds + 1)),
            stop_policy=all(
                guard[key] == (1 if guard["state"] == 4 else 0)
                for key in ("stop_accepted", "ring_retired", "output_retired")),
        )
        return {
            "uid": row["uid"], "checks": common, "passed": all(common.values()),
            "endpoint": {key: run[key] for key in (
                "generation", "session", "state", "reason", "duration_ms",
                "edges", "pio_enabled", "dma_busy")},
        }

    class SummaryProbe(external.Probe):
        def configure(self):
            for board in self.boards:
                if self.trace_status(board)["state"] != 0:
                    raise RuntimeError("Prior trace is not released")
            self.report["plan"].update(
                internal_trace=True, external_scope=False, no_runtime_queries=True,
                summary_pool_limit_s=600,
                summary_interval_ms=self.opt.summary_interval_ms)
            self.report.setdefault("tool_sources", []).append({
                "path": str(tracked_path), "sha256": tracked_sha})
            capture.RunProbe.configure(self)

        def control(self, board, text, expected="OK"):
            result = external.Probe.control(self, board, text, expected)
            if (text.upper().startswith(follow.parent.SYNC.upper() + " ") and
                    int(text.split()[-1]) > 0 and board == self.boards[0]):
                for index, participant in enumerate(self.boards):
                    row = self.report["boards"][participant.address]
                    before = self.trace_status(participant)
                    capture_id = max(
                        before["capture_id"] + 1, self.report["binding_generation"] + index)
                    row.update(trace_capture_id=capture_id, trace_arm_requested=True)
                    self.save()
                    command = helper.arm_command(
                        capture_id, origin=index == 0,
                        summary_interval_ms=self.opt.summary_interval_ms)
                    follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[
                        command.split()[0].upper()] = 1
                    follow.FollowProbe.control(self, participant, command, capture_id)
                    row["trace_arm_ack"] = self.trace_ack(participant, 1, 1, capture_id)
                    if row["trace_arm_ack"]["status"]["schema"] != helper.summary_schema(
                            index == 0, self.opt.summary_interval_ms):
                        raise ValueError("Summary trace schema mismatch")
                    expected_interval = self.opt.summary_interval_ms or 1000
                    if row["trace_arm_ack"]["status"]["sample_interval_ms"] != expected_interval:
                        raise ValueError("Summary trace interval mismatch")
                    self.save()
            return result

        def acquire(self):
            self.block_queries = True
            self.barrier = False
            self.scope = SimpleNamespace(quiet=True)
            try:
                external.startup.parallel_start(self)
            finally:
                self.scope = None
            time.sleep(.2)
            epoch = int(time.time()) & 0xFFFFFFFF
            grant = int(self.command(
                self.boards[0], f"CALibration:ORIGin:TRIAL {epoch},8192,256,0").strip('"'))
            if not grant or grant & 1:
                raise RuntimeError("Origin trial grant rejected")
            self.report["trial_epoch"] = grant
            self.save()
            begun = time.monotonic()
            helper.quiet_wait(self.opt.seconds)
            self.report["quiet_elapsed_s"] = time.monotonic() - begun

        def collect(self):
            if not self.barrier or self.block_queries:
                raise RuntimeError("Summary collection requires all-board STOP")
            for board in self.boards:
                row = self.report["boards"][board.address]
                for key, command in (
                        ("origin_release", "READ:CALibration:ORIGin:RELease?"),
                        ("origin_handoff", "READ:CALibration:ORIGin:HANDoff?"),
                        ("origin_runtime", "READ:CALibration:ORIGin:RUNTIme?"),
                        ("priority_tx", follow.parent.TX),
                        ("match", "SYSTem:VDC:PRIORity:MATCH:STATus?")):
                    row[key] = self.command(board, command)
            for board in self.boards:
                row = self.report["boards"][board.address]
                if row.get("trace_arm_requested"):
                    self.control(board, trace + ":STOP", 1)
                    row["trace_stop_ack"] = self.trace_ack(
                        board, 2, 3, row["trace_capture_id"])
            assessments = []
            for index, board in enumerate(self.boards):
                row = self.report["boards"][board.address]
                if not row.get("trace_arm_requested"):
                    continue
                try:
                    data, pages = helper.download_capture(
                        lambda command: self.command(board, command), row["trace_capture_id"])
                    folder = self.opt.out / f"NO{index + 1}"
                    folder.mkdir(parents=True, exist_ok=True)
                    (folder / "summary.ram.bin").write_bytes(data)
                    (folder / "read-pages.json").write_text(
                        json.dumps(pages, indent=2), encoding="utf-8")
                    decoded = helper.decode(data, row["trace_capture_id"])
                    (folder / "decoded.json").write_text(
                        json.dumps(decoded, indent=2), encoding="utf-8")
                    assessment = helper.assess_capture(
                        decoded, self.opt.seconds, index == 0,
                        summary_interval_ms=self.opt.summary_interval_ms)
                    row["internal_assessment"] = assessment
                    assessments.append(assessment)
                    self.control(board, trace + ":RELease", 1)
                    row["trace_release_ack"] = self.trace_ack(board, 3, 0)
                    row["run_output_raw"] = self.command(board, capture.PREFIX + "?")
                    row["final_follow_raw"] = self.command(board, follow.STATUS)
                    row["final_phase_raw"] = self.command(board, phase.PHASE + ":STATus?")
                except Exception as exc:
                    self.error("internal:" + board.address, exc)
                self.save()
            self.report["passed"] = (
                len(assessments) == 4 and not self.report["errors"] and
                all(item["coverage_complete"] and item["all_bins_have_success"] and
                    item["success_gap_passed"] for item in assessments))
            self.report["physical_lock_qualified"] = False
            self.save()

    class StartupProbe(SummaryProbe):
        def configure(self):
            sources = self.opt.workflow_inputs_evidence["adapter"]["migrated_sources"]
            self.report["startup_adapter"] = {
                "path": tracked_path.as_posix(),
                "sha256": tracked_sha,
                "base_sha256": sources["internal_summary"]["sha256"],
                "migrated_sources": sources,
                "omitted_optional_queries": [],
            }
            self.configuring_without_model_sample = True
            try:
                super().configure()
            finally:
                self.configuring_without_model_sample = False
            if self.report["model_before_start"] is not None:
                raise ValueError("Optional pre-START model sample was not omitted")
            if len(self.report["startup_adapter"]["omitted_optional_queries"]) != 1:
                raise ValueError("Unexpected optional pre-START model consumers")
            self.save()

        def command(self, board, text, unused=None):
            if (getattr(self, "configuring_without_model_sample", False) and
                    text == follow.parent.MODEL):
                if board != self.boards[0]:
                    raise ValueError("Optional model consumer is not the master")
                skipped = self.report["startup_adapter"]["omitted_optional_queries"]
                if skipped:
                    raise ValueError("Unexpected additional pre-START model consumer")
                skipped.append({
                    "uid": board.address,
                    "command": text,
                    "transmitted": False,
                    "reason": "Optional management snapshot is not a START prerequisite; native evidence is read after STOP.",
                })
                return None
            return super().command(board, text, unused)

    class SealProbe(StartupProbe):
        def command(self, board, text, unused=None):
            prefix = trace + ":SUMMary:"
            if text.upper().startswith(prefix.upper()):
                role = "ORIGin" if board == self.boards[0] else "PHASe"
                capture_id = text.split(" ", 1)[1].split(",")[0]
                text = f"{trace}:GUARd:{role} {capture_id},{self.opt.seconds}"
                follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[
                    text.split()[0].upper()] = 1
            if (self.opt.expire_output and board == self.boards[3] and
                    text.upper().startswith("SYSTEM:VDC:OUTPUT:RUN ")):
                if not text.endswith(",0"):
                    raise ValueError("Unexpected output duration command")
                text = text[:-1] + "20000"
            return super().command(board, text, unused)

        def configure(self):
            super().configure()
            self.report["plan"]["run_status_schema"] = 12
            self.report["guard_plan"] = {
                "target_s": self.opt.seconds,
                "checkpoints_s": list(range(60, self.opt.seconds + 1, 60)),
                "no_reference": self.opt.no_reference,
                "expire_output": self.opt.expire_output,
                "runtime_queries": 0,
                "scope": False,
                "autonomous_local_stop_on_failure": True,
                "simultaneous_four_board_stop": False,
                "script_sha256": tracked_sha,
            }
            self.save()

        def acquire(self):
            self.block_queries = True
            self.barrier = False
            self.scope = SimpleNamespace(quiet=True)
            try:
                external.startup.parallel_start(self)
            finally:
                self.scope = None
            time.sleep(.2)
            if not self.opt.no_reference:
                epoch = int(time.time()) & 0xFFFFFFFF
                grant = int(self.command(
                    self.boards[0],
                    f"CALibration:ORIGin:TRIAL {epoch},8192,256,0").strip('"'))
                if not grant or grant & 1:
                    raise RuntimeError("Origin trial grant rejected")
                self.report["trial_epoch"] = grant
            else:
                self.report["trial_intentionally_omitted"] = True
            self.save()
            begun = time.monotonic()
            helper.quiet_wait(self.opt.seconds + 2)
            self.report["quiet_elapsed_s"] = time.monotonic() - begun

        def collect(self):
            if not self.barrier or self.block_queries:
                raise RuntimeError("GUARD collection requires all-board STOP")
            for board in self.boards:
                raw = self.command(board, trace + ":GUARd?")
                values = [int(value.strip().strip('"')) for value in raw.split(",")]
                if len(values) != len(GUARD_FIELDS):
                    raise ValueError("GUARD field count mismatch")
                self.report["boards"][board.address]["guard_raw"] = raw
                self.report["boards"][board.address]["guard"] = dict(
                    zip(GUARD_FIELDS, values))
                self.save()
            super().collect()
            self.report["internal_health_passed"] = self.report["passed"]
            mode = ("missing" if self.opt.no_reference else
                    "expiry" if self.opt.expire_output else "positive")
            checks = []
            for index, board in enumerate(self.boards):
                row = self.report["boards"][board.address]
                row["uid"] = board.address
                data = (self.opt.out / f"NO{index + 1}" / "summary.ram.bin").read_bytes()
                decoded = helper.decode(data, row["trace_capture_id"])
                checks.append(evaluate_board(
                    row, decoded, index, self.opt.seconds, mode))
            self.report["guard_checks"] = checks
            self.report["output_endpoints"] = [
                {"uid": check["uid"], **check["endpoint"]} for check in checks]
            self.report["expected_missing_reference"] = self.opt.no_reference
            self.report["expected_output_expiry"] = self.opt.expire_output
            self.report["passed"] = trial_passed(self.report, checks, mode)
            add_replan_report(self.report)
            self.save()

        def run(self):
            try:
                super().run()
            finally:
                if "strict_replan_qualification" in self.report:
                    qualification = self.report["strict_replan_qualification"]
                    qualification["base_health_passed"] = bool(self.report["passed"])
                    qualification["passed"] = bool(
                        qualification["criteria_passed"] and self.report["passed"])
                self.report["conclusion_scope"] = (
                    "Internal SRAM reference/follow coverage and diagnostic and output engine "
                    "checkpoint verdicts. FAIL requests local retirement; PASS leaves the ring "
                    "running until final host STOP. No runtime queries or scope acquisition; "
                    "actual GPIO precision is unqualified; engine state is not GPIO evidence. "
                    "A negative injection pass means the expected selected failure and "
                    "retirement were observed, not healthy DPLL operation.")
                self.save()

    base = SimpleNamespace(
        external=external, helper=helper, capture=capture, follow=follow,
        phase=phase, trace=trace)
    return SimpleNamespace(
        Probe=SealProbe,
        base=base,
        parse_run_output=parse_run_output,
        evaluate_board=evaluate_board,
        source_path=tracked_path,
        source_sha256=tracked_sha,
    )
