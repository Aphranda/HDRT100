"""Independent P0-T3 calibration action pages.

This is the host-side shell for the ladder.  Each page exposes its own action
id, C-segment defaults and run/readback controls; the actual board semantics
still belong to the calibration owner.  P0 is read-only (topology/boot).
"""

from __future__ import annotations

import threading
import tkinter as tk
from tkinter import ttk
from tkinter.scrolledtext import ScrolledText

try:
    from .calibration_workflow import GuideWorkflow
    from .obs_read import ObsReaderMixin
    from .ui import UiEvent
except ImportError:  # direct script execution
    from calibration_workflow import GuideWorkflow  # type: ignore
    from obs_read import ObsReaderMixin  # type: ignore
    from ui import UiEvent  # type: ignore

ACTION_SPECS = {
    "P0": {"action": 0, "name": "线序校准", "guide": False,
           "driver": "板端", "state": "已实现（自组 10 kHz）",
           "description": "零口自组后的拓扑/线序检查；只读 BOOT:PLAN? 与 RING:STATus?。"},
    "P1": {"action": 1, "name": "时钟标定", "guide": True,
           "driver": "板端", "state": "已接线（GUIDE）",
           "level": 23, "crc_base": 0x12340000, "window": 4000,
           "candidates": "0,4,16", "trials": 4, "expected_trials": 4,
           "threshold_low": "1", "threshold_high": "64",
           "description": "P1：reference slot + 4/16 cycle 候选。"},
    "P2": {"action": 2, "name": "编码标定", "guide": True,
           "driver": "板端", "state": "已接线（GUIDE，按阶梯）",
           "level": 7, "crc_base": 0x22000000, "window": 8000,
           "candidates": "0,1,80,120,0", "trials": 1, "expected_trials": 1,
           "threshold_low": "10", "threshold_high": "512",
           "description": "P2：ref_slot/codebook/min_lag/max_lag/min_margin；必须 5 候选、trials=1。"},
    "P3": {"action": 3, "name": "时延标定", "guide": True,
           "driver": "板端", "state": "已接线（GUIDE，按阶梯）",
           "level": 7, "crc_base": 0x33000000, "window": 20000,
           "candidates": "0,0,32,256,3", "trials": 1, "expected_trials": 12,
           "threshold_low": "10000000", "threshold_high": "50000",
           "description": "P3：start_link/signal_group/pulse/capture/repeats；trials=nodes×repeats。"},
    "T0": {"action": 4, "name": "TRN-00 marker", "guide": True,
           "driver": "板端", "state": "已接线（GUIDE，按阶梯）",
           "level": 7, "crc_base": 0x44000000, "window": 4000,
           "candidates": "0,1,0,0,1", "trials": 1, "expected_trials": 1,
           "threshold_low": "0", "threshold_high": "0",
           "description": "T0：origin/codebook/link_delay/offset/repeats；trials=repeats。"},
    "T1": {"action": 5, "name": "TRN-01 SCK 偏移", "guide": True,
           "driver": "板端", "state": "已接线（GUIDE，按阶梯）",
           "level": 7, "crc_base": 0x55000000, "window": 4000,
           "candidates": "0,100,0,32,1", "trials": 1, "expected_trials": 1,
           "threshold_low": "64", "threshold_high": "1",
           "description": "T1：link_index/link_delay/search/guard。"},
    "T2": {"action": 6, "name": "TRN-02 DATA 重复", "guide": True,
           "driver": "板端", "state": "已接线（GUIDE，按阶梯）",
           "level": 7, "crc_base": 0x66000000, "window": 4000,
           "candidates": "0,100,0,32,1", "trials": 1, "expected_trials": 1,
           "threshold_low": "64", "threshold_high": "1",
           "description": "T2：link_index/marker_to_data/search。"},
    "T3": {"action": 7, "name": "TRN-03 staging", "guide": True,
           "driver": "板端", "state": "stage/ARM（S 段）",
           "level": 7, "crc_base": 0x77000000, "window": 4000,
           "candidates": "", "trials": 1, "expected_trials": 1,
           "threshold_low": "0", "threshold_high": "0",
           "description": "T3：staging 经环上来；仅此动作的 S 允许 ARM→START 10 MHz。"},
}


def action_inputs(spec: dict, generation: int, epoch: int, level=None) -> dict:
    candidates = [int(item, 0) for item in str(spec.get("candidates", "")).split(",")
                  if item.strip()]
    return {
        "action": int(spec["action"]),
        "level": int(spec.get("level", 23) if level is None else level),
        "crc": (int(spec.get("crc_base", 0x12340000)) + generation) & 0xFFFFFFFF,
        "generation": int(generation),
        "window_ms": int(spec.get("window", 4000)),
        "trials": int(spec.get("trials", 1)),
        "candidate_count": len(candidates),
        "candidates": candidates,
        "threshold_low": int(spec.get("threshold_low", 1)),
        "threshold_high": int(spec.get("threshold_high", 64)),
        "epoch": int(epoch),
        "expected_trials": int(spec.get("expected_trials", spec.get("trials", 1))),
    }


class CalibrationActionPage(ObsReaderMixin, ttk.Frame):
    def __init__(self, master, app, key: str) -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.key = key
        self.spec = ACTION_SPECS[key]
        self.action = tk.StringVar(value=str(self.spec["action"]))
        self.level = tk.StringVar(value=str(self.spec.get("level", 23)))
        self.generation = tk.StringVar(value="1")
        self.epoch = tk.StringVar(value="1")
        self.window_ms = tk.StringVar(
            value=str(self.spec.get("window", 4000)))
        self.trials = tk.StringVar(value=str(self.spec.get("trials", 1)))
        self.candidates = tk.StringVar(value=self.spec.get("candidates", ""))
        self.threshold_low = tk.StringVar(
            value=str(self.spec.get("threshold_low", "1")))
        self.threshold_high = tk.StringVar(
            value=str(self.spec.get("threshold_high", "64")))
        self.status = tk.StringVar(value="IDLE")
        self.busy = False
        self.workflow = None

        head = ttk.LabelFrame(self, text=f"{key} · {self.spec['name']}",
                              style="Card.TLabelframe", padding=(12, 8))
        head.pack(fill="x", padx=10, pady=10)
        description = self.spec["description"]
        if not self.spec["guide"]:
            description += " 当前固件只接 P1；本页仅保留参数与只读入口。"
        ttk.Label(head, text=description, style="Hint.TLabel",
                  wraplength=1080, justify="left").pack(anchor="w")
        ttk.Label(
            head,
            text=f"驱动方：{self.spec.get('driver', '-')}｜状态：{self.spec.get('state', '-')}",
            style="Value.TLabel").pack(anchor="w", pady=(4, 0))
        ttk.Label(head, textvariable=self.status, style="Value.TLabel").pack(
            anchor="w", pady=(6, 0))

        form = ttk.LabelFrame(self, text="C/T 参数", style="Card.TLabelframe",
                              padding=(12, 8))
        form.pack(fill="x", padx=10, pady=6)
        fields = (
            ("action", self.action), ("level", self.level),
            ("generation", self.generation), ("epoch", self.epoch),
            ("window_ms", self.window_ms),
            ("trials", self.trials), ("candidates", self.candidates),
            ("threshold_low", self.threshold_low),
            ("threshold_high", self.threshold_high),
        )
        for index, (label, variable) in enumerate(fields):
            row, column = divmod(index, 4)
            ttk.Label(form, text=label, style="Field.TLabel").grid(
                row=row, column=column * 2, sticky="e", padx=(0, 4), pady=2)
            ttk.Entry(form, textvariable=variable, width=18).grid(
                row=row, column=column * 2 + 1, sticky="w", padx=(0, 12), pady=2)

        bar = ttk.Frame(self, style="Panel.TFrame", padding=(12, 6))
        bar.pack(fill="x", padx=10, pady=6)
        self.run_button = ttk.Button(
            bar, text="运行当前动作", style="Accent.TButton",
            command=self.run_action)
        self.run_button.pack(side="left")
        self.plan_button = ttk.Button(bar, text="读 PLAN?（本地）", command=self.read_plan)
        self.plan_button.pack(side="left", padx=6)
        self.boot_button = ttk.Button(bar, text="读 BOOT:PLAN?（本地）", command=self.read_boot)
        self.boot_button.pack(side="left", padx=6)
        self.cancel_button = ttk.Button(bar, text="取消", command=self.cancel)
        self.cancel_button.pack(side="left", padx=6)
        if not self.spec["guide"]:
            self.run_button.configure(state="disabled")

        ttk.Label(self, text="本动作读回", style="Hint.TLabel").pack(
            anchor="w", padx=12, pady=(4, 2))
        self.output = ScrolledText(self, height=16, font=("Consolas", 9),
                                   background="#f8fafc", relief="flat")
        self.output.pack(fill="both", expand=True, padx=10, pady=(0, 10))

    def _u32(self, var, name: str) -> int:
        try:
            return int(var.get(), 0)
        except ValueError as exc:
            raise ValueError(f"{name} 必须是 u32") from exc

    def _inputs(self) -> dict:
        return action_inputs(
            self.spec, self._u32(self.generation, "generation"),
            self._u32(self.epoch, "epoch"),
            level=self._u32(self.level, "level"))

    def _record(self, text: str) -> None:
        self.output.insert("end", text + "\n")
        self.output.see("end")

    def _emit(self, text: str) -> None:
        self.app.pump.post(UiEvent("calflow", f"[{self.key}] {text}"))

    def _request(self, command: str, timeout: float) -> str:
        if self.app.dispatcher is None:
            raise RuntimeError("未连接")
        result = self.app.dispatcher.request(command, f"cal-{self.key}", timeout)
        if result.error:
            raise RuntimeError(result.error)
        return result.response

    def render(self, vector) -> None:
        enabled = vector.connection.connected and not self.busy
        state = "normal" if enabled else "disabled"
        for button in (self.plan_button, self.boot_button):
            button.configure(state=state)
        if self.spec["guide"]:
            self.run_button.configure(state=state)
        self.cancel_button.configure(state="normal" if self.busy else "disabled")

    def run_action(self) -> None:
        if not self.spec["guide"] or self.busy:
            return
        if self.key == "T3" and not self.app.dialogs.confirm(
                "T3 引导", "T3 的 S 段允许 ARM → START 10 MHz；确认继续？",
                danger=True):
            return
        try:
            inputs = self._inputs()
        except ValueError as exc:
            self.app.dialogs.error("参数错误", str(exc))
            return
        self.busy = True
        self.status.set("RUNNING")
        self.render(self.app.store.snapshot())
        threading.Thread(target=self._run, args=(inputs,), daemon=True,
                         name=f"cal-{self.key}").start()

    def _run(self, inputs: dict) -> None:
        workflow = GuideWorkflow(
            self, level=int(self._u32(self.level, "level")), label="10k",
            budget_level=23,
            report_root=("out/calibration/guide-10k/" f"action-{self.key}"),
            log_label=f"10k-{self.key}")
        self.workflow = workflow
        try:
            state = workflow.start(inputs)
            self._emit(f"完成：{state.status} run={state.run_id}")
        except Exception as exc:  # noqa: BLE001
            self._emit(f"失败：{exc}")
        finally:
            self.app.pump.post(UiEvent("calflow-done", ""))

    def cancel(self) -> None:
        if self.workflow is not None:
            self.workflow.cancel()
        self.busy = False
        self.status.set("CANCELLED")
        self.render(self.app.store.snapshot())

    def dispose(self) -> None:
        self.cancel()

    def _send(self, command: str, source: str) -> None:
        self._record(f"> {command}")
        self.app.send_command(command, source)

    def read_plan(self) -> None:
        self._send("SYST:TDMA:GUIDE:PLAN?", f"cal:a{self.key}:plan")

    def read_boot(self) -> None:
        self._send("SYST:TDMA:BOOT:PLAN?", f"cal:a{self.key}:boot")

    def show_result(self, source: str, response: str, error: str) -> None:
        if error:
            self._record(f"! {source} → {error}")
            return
        self._record(f"< {source} {response}")
