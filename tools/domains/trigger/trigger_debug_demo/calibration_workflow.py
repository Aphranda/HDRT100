"""Single-port calibration guide workflow (10 MHz / 10 kHz).

One coordinator port only.  The workflow sends the same GUIDE C/T commands used
by the bench flow, then confirms gen/epoch/progress through the coordinator's
PLAN? readback.  Four-board agreement still has to be established by the
caller; this class never opens another port.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

try:
    from .ui.runlog import RunLog
    from .ui.workflow import StepSpec, WorkflowContext, WorkflowRunner, WorkflowSpec
except ImportError:  # direct script execution
    from ui.runlog import RunLog  # type: ignore
    from ui.workflow import (  # type: ignore
        StepSpec,
        WorkflowContext,
        WorkflowRunner,
        WorkflowSpec,
    )

PLAN_FIELDS = (
    "valid", "action", "crc", "gen", "level", "window", "trials", "pending",
    "epoch", "ctrqs", "progress_valid", "progress_status", "trials_done",
    "deepest", "reason", "reconcile", "resume_trial", "commit",
    "commit_applicable", "commit_ok", "ladder_index", "ladder_step",
    "passed_mask", "failed_mask", "abort_site",
)


class GuideWorkflow:
    def __init__(self, page, level: int = 7, label: str = "10m",
                 state_dir=None, report_root=None, log_root=None,
                 log_label=None, budget_level=None) -> None:
        self.page = page
        self.level = int(level)
        self.budget_level = int(budget_level if budget_level is not None
                                else level)
        self.label = label
        self.report_root = Path(report_root) if report_root else Path("out")
        self.log_root = (Path(log_root) if log_root
                         else Path("out") / "calibration" / "guide-10k" / "logs")
        self.run_log = RunLog(self.log_root, log_label or label)
        if self.budget_level == 23:
            self.defaults = {"gen_timeout_s": 120.0, "epoch_timeout_s": 180.0,
                             "watch_timeout_s": 360.0, "request_timeout_s": 30.0}
        else:
            self.defaults = {"gen_timeout_s": 30.0, "epoch_timeout_s": 30.0,
                             "watch_timeout_s": 90.0, "request_timeout_s": 15.0}
        self.spec = WorkflowSpec(
            f"cal.guide.{label}",
            "1",
            (
                StepSpec("precheck", "Check single port / ring", self._precheck),
                StepSpec("config", f"GUIDE:CONFig (level {self.level})", self._config),
                StepSpec("wait_gen", "Wait for plan generation", self._wait_gen),
                StepSpec("trigger", "GUIDE:TRIGger", self._trigger),
                StepSpec("wait_epoch", "Wait for trigger epoch", self._wait_epoch),
                StepSpec("watch", "Wait for COMPLETE", self._watch),
                StepSpec("report", "Write report", self._report),
            ),
        )
        self.runner = WorkflowRunner(self.spec, state_dir)

    def _log(self, text: str) -> None:
        try:
            self.run_log.write(text)
        except OSError:
            pass
        self.page._emit(text)

    def start(self, inputs: dict):
        try:
            return self.runner.start(inputs)
        except Exception as exc:
            self._write_failure(exc)
            raise

    def _write_failure(self, exc: Exception) -> None:
        run_id = self.runner.last_run_id
        if not run_id:
            return
        try:
            state = self.runner.load(run_id)
        except Exception:
            return
        try:
            final_plan = self._plan()
        except Exception:
            final_plan = {}
        try:
            ring_after = _ints(self.page._request(
                "SYSTem:TDMA:RING:STATus?", 5.0))
        except Exception:
            ring_after = []
        body = {
            "schema": f"HDRT100_GUIDE_{self.label.upper()}_FLOW_V1",
            "run_id": run_id,
            "idn": state.step_data.get("idn", ""),
            "build": state.step_data.get("build", ""),
            "inputs": state.inputs,
            "final_plan": final_plan,
            "ring_after": ring_after,
            "failure_class": _failure_class(final_plan),
            "passed": False,
            "log_path": str(self.run_log.path),
            "error": str(exc),
            "state": state.to_dict(),
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        }
        path = (self.report_root / "calibration" / f"guide-{self.label}" /
                f"{run_id}-failed.json")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(body, ensure_ascii=False, indent=2),
                        encoding="utf-8")
        self._log(f"failure report: {path}")

    def cancel(self) -> None:
        self.runner.cancel()

    def latest(self):
        return self.runner.latest()

    # ------------------------------------------------------------------ steps
    def _precheck(self, ctx: WorkflowContext) -> None:
        idn = self.page._request("*IDN?", 5.0)
        build = self.page._request("SYSTem:FW:BUILD?", 5.0)
        self._log(
            f"precheck: {idn.strip()} build={build.strip()} level={self.level}")
        ring = []
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            ring = _ints(self.page._request("SYSTem:TDMA:RING:STATus?", 5.0))
            if (len(ring) >= 9 and ring[0] == 1 and ring[4] == 1
                    and ring[5] == 1 and ring[8] == 1):
                break
            if ctx.cancel_requested():
                raise RuntimeError("用户取消")
            time.sleep(1.0)
        if len(ring) < 9 or ring[0] != 1 or ring[4] != 1 or ring[5] != 1 or ring[8] != 1:
            raise RuntimeError(
                f"ring is not running (need enabled/up/down/adapter=1); "
                f"start the level-{self.level} guide ring before this flow")
        ctx.checkpoint(idn=idn, build=build, ring=ring[:12])

    def _config(self, ctx: WorkflowContext) -> None:
        inputs = ctx.state.inputs
        parts = [
            inputs["action"], inputs["crc"], inputs["generation"], self.level,
            inputs["window_ms"], inputs["trials"], inputs["candidate_count"],
            *inputs["candidates"], inputs["threshold_low"],
            inputs["threshold_high"],
        ]
        command = "SYST:TDMA:GUIDE:CONFig " + ",".join(str(item) for item in parts)
        self._log(f"CONFig: {command}")
        last_error = None
        for attempt in range(6):
            try:
                response = self.page._request(
                    command, float(ctx.state.inputs.get(
                        "request_timeout_s", self.defaults["request_timeout_s"])))
                break
            except Exception as exc:
                last_error = exc
                text = str(exc)
                if "-200" not in text and "ONLINE" not in text.upper():
                    raise
                self._log(
                    f"CONFig refused (attempt {attempt + 1}/6): {text}; retrying")
                time.sleep(2.0)
        else:
            raise RuntimeError(f"CONFig not accepted: {last_error}")
        self._log(f"CONFig ack: {response.strip()}")
        ctx.checkpoint(config_command=command, config_response=response)

    def _wait_gen(self, ctx: WorkflowContext) -> None:
        target = int(ctx.state.inputs["generation"])
        timeout = float(ctx.state.inputs.get(
            "gen_timeout_s", self.defaults["gen_timeout_s"]))
        self._wait_plan(ctx, "gen", target, timeout, "generation")

    def _trigger(self, ctx: WorkflowContext) -> None:
        action = int(ctx.state.inputs["action"])
        epoch = int(ctx.state.inputs["epoch"])
        command = f"SYST:TDMA:GUIDE:TRIGger {action},{epoch}"
        self._log(f"TRIGger: {command}")
        response = self.page._request(
            command, float(ctx.state.inputs.get(
                "request_timeout_s", self.defaults["request_timeout_s"])))
        self._log(f"TRIGger ack: {response.strip()}")
        ctx.checkpoint(trigger_command=command, trigger_response=response)

    def _wait_epoch(self, ctx: WorkflowContext) -> None:
        target = int(ctx.state.inputs["epoch"])
        timeout = float(ctx.state.inputs.get(
            "epoch_timeout_s", self.defaults["epoch_timeout_s"]))
        self._wait_plan(ctx, "epoch", target, timeout, "epoch")

    def _wait_plan(self, ctx: WorkflowContext, field: str, target: int,
                   timeout: float, label: str) -> None:
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            if ctx.cancel_requested():
                raise RuntimeError("用户取消")
            values = self._plan()
            value = values.get(field, -1)
            if (values.get("gen"), values.get("epoch"), value) != last:
                self._log(f"{label}: {field}={value} (target {target})")
                last = (values.get("gen"), values.get("epoch"), value)
            if value == target:
                ctx.checkpoint(**{f"{label}_value": value})
                return
            time.sleep(1.0)
        raise RuntimeError(f"{label} not reached within {timeout:.0f}s")

    def _watch(self, ctx: WorkflowContext) -> None:
        timeout = float(ctx.state.inputs.get(
            "watch_timeout_s", self.defaults["watch_timeout_s"]))
        deadline = time.monotonic() + timeout
        last = None
        saw_active = False
        action = int(ctx.state.inputs.get("action", 1))
        while time.monotonic() < deadline:
            if ctx.cancel_requested():
                raise RuntimeError("用户取消")
            values = self._plan()
            ctrqs = values.get("ctrqs", -1)
            if ctrqs not in (0, 28, 29):
                saw_active = True
            key = (ctrqs, values.get("progress_valid"),
                   values.get("progress_status"), values.get("trials_done"),
                   values.get("reason"), values.get("reconcile"),
                   values.get("ladder_index"), values.get("ladder_step"))
            if key != last:
                self.page._emit(
                    "progress: ctrqs={} valid={} status={} trials={} "
                    "reason={} reconcile={} ladder={}/{}".format(
                        ctrqs, values.get("progress_valid"),
                        values.get("progress_status"), values.get("trials_done"),
                        values.get("reason"), values.get("reconcile"),
                        values.get("ladder_index"), values.get("ladder_step")))
                last = key
            if values.get("progress_valid") == 1:
                if action == 7:
                    fresh = (values.get("ladder_index") == 6
                             and values.get("ladder_step") == 2)
                else:
                    fresh = values.get("ladder_index") == action
                terminal_fail = values.get("progress_status") in (2, 3)
                if (saw_active and fresh) or terminal_fail:
                    ctx.checkpoint(final_plan=values)
                    return
            time.sleep(1.0)
        raise RuntimeError(f"GUIDE progress not valid within {timeout:.0f}s")

    def _report(self, ctx: WorkflowContext) -> None:
        values = self._plan()
        trials = max(1, int(ctx.state.inputs.get(
            "expected_trials", ctx.state.inputs.get("trials", 1))))
        want_mask = (1 << trials) - 1
        # abort_site is appended by current firmware (PLAN? field 25).  On an
        # older 24-field readback it is absent and _plan() returns -1, so the
        # gate fails closed instead of treating an unknown abort as success.
        passed = (
            values.get("valid") == 1
            and values.get("progress_valid") == 1
            and values.get("progress_status") == 1
            and values.get("reconcile") == 1
            and values.get("trials_done") == trials
            and values.get("passed_mask") == want_mask
            and values.get("failed_mask", 1) == 0
            and values.get("abort_site", 1) == 0
        )
        body = {
            "schema": f"HDRT100_GUIDE_{self.label.upper()}_FLOW_V1",
            "run_id": ctx.state.run_id,
            "idn": ctx.state.step_data.get("idn", ""),
            "build": ctx.state.step_data.get("build", ""),
            "inputs": ctx.state.inputs,
            "final_plan": values,
            "ring_after": _ints(self.page._request("SYSTem:TDMA:RING:STATus?", 5.0)),
            "failure_class": "NONE" if passed else _failure_class(values),
            "passed": passed,
            "log_path": str(self.run_log.path),
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        }
        path = (self.report_root / "calibration" / f"guide-{self.label}" /
                f"{ctx.state.run_id}.json")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(body, ensure_ascii=False, indent=2),
                        encoding="utf-8")
        ctx.checkpoint(report_path=str(path), passed=passed)
        self._log(f"report: {path} passed={passed}")
        if not passed:
            raise RuntimeError(f"{self.label} guide did not reach COMPLETE")

    def _plan(self) -> dict[str, int]:
        values = _ints(self.page._request("SYSTem:TDMA:GUIDE:PLAN?", 5.0))
        return {name: (values[index] if index < len(values) else -1)
                for index, name in enumerate(PLAN_FIELDS)}


def _ints(text: str) -> list[int]:
    result: list[int] = []
    for token in text.replace('"', "").replace(",", " ").split():
        try:
            result.append(int(token, 0))
        except ValueError:
            pass
    return result


def _failure_class(plan: dict[str, int]) -> str:
    if plan.get("progress_status") == 3 and plan.get("reason") == 14:
        return "Q_RING_RESTORE"
    if plan.get("progress_status") == 3:
        return "Q_FAILED"
    if plan.get("progress_valid") != 1:
        return "Q_NOT_REACHED"
    if plan.get("reconcile") != 1:
        return "Q_NOT_RECONCILED"
    if plan.get("failed_mask", 0) != 0:
        return "TRIAL_FAILED"
    if plan.get("abort_site", 1) != 0:
        return "ABORT_SITE"
    if plan.get("progress_status") == 1:
        return "TRIAL_COUNT_OR_MASK_MISMATCH"
    return "NONE"
