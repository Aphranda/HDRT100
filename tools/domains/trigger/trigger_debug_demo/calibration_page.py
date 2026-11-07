"""Single-port calibration guide page (10 MHz / 10 kHz).

The C/T/PLAN flow is identical for both rates; only the operating-profile level
and the delivery budget change.  This page deliberately does not touch the
bench: it only builds SCPI strings and displays readbacks.
"""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from tkinter.scrolledtext import ScrolledText

try:
    from .calibration_workflow import GuideWorkflow
    from .ui import UiEvent
except ImportError:  # direct script execution
    from calibration_workflow import GuideWorkflow  # type: ignore
    from ui import UiEvent  # type: ignore

PLAN_FIELDS = (
    "valid", "action", "crc", "gen", "level", "window", "trials", "pending",
    "epoch", "ctrqs", "progress_valid", "progress_status", "trials_done",
    "deepest", "reason", "reconcile", "resume_trial", "commit",
    "commit_applicable", "commit_ok", "ladder_index", "ladder_step",
    "passed_mask", "failed_mask",
)


class CalibrationPage(ttk.Frame):
    """10 MHz / 10 kHz guide C/T/PLAN console."""

    def __init__(self, master, app) -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.mode = tk.StringVar(value="10 MHz")
        self.action = tk.StringVar(value="1")
        self.crc = tk.StringVar(value="305397760")  # 0x12340000
        self.auto_crc = tk.BooleanVar(value=True)
        self.generation = tk.StringVar(value="1")
        self.window_ms = tk.StringVar(value="4000")
        self.trials = tk.StringVar(value="4")
        self.candidate_count = tk.StringVar(value="3")
        self.candidates = tk.StringVar(value="0,4,16")
        self.threshold_low = tk.StringVar(value="1")
        self.threshold_high = tk.StringVar(value="64")
        self.epoch = tk.StringVar(value="1")
        self.status = tk.StringVar(value="IDLE")
        self.busy = False
        self.workflow_runner = None

        top = ttk.LabelFrame(self, text="引导档位", style="Card.TLabelframe",
                             padding=(12, 8))
        top.pack(fill="x", padx=10, pady=10)
        ttk.Radiobutton(top, text="10 MHz（level 7）", value="10 MHz",
                        variable=self.mode, command=self._mode_changed).pack(side="left")
        ttk.Radiobutton(top, text="10 kHz（level 23）", value="10 kHz",
                        variable=self.mode, command=self._mode_changed).pack(
            side="left", padx=(16, 0))
        ttk.Label(top, textvariable=self.status, style="Value.TLabel").pack(
            side="right")

        info = ttk.Label(
            self, style="Warn.TLabel", justify="left", wraplength=1100,
            text="10 kHz 的流程与 10 MHz 相同，只是 profile level 不同；"
                 "10 kHz 启环已确认可用。CONFig 约 50 s、TRIGger 约 78 s，"
                 "窗口结束前不要重发。四板一致性仍需外部读回；"
                 "本页只保证单口命令与协调板 PLAN? 读回。")
        info.pack(fill="x", padx=12, pady=(0, 6))

        flow_bar = ttk.Frame(self, style="Panel.TFrame", padding=(12, 6))
        flow_bar.pack(fill="x", padx=10, pady=(0, 6))
        self.flow_button = ttk.Button(
            flow_bar, text="运行单口流程", style="Accent.TButton",
            command=self.run_flow)
        self.flow_button.pack(side="left")
        self.cancel_button = ttk.Button(
            flow_bar, text="取消流程", command=self.cancel)
        self.cancel_button.pack(side="left", padx=6)
        ttk.Label(
            flow_bar, style="Muted.TLabel",
            text="precheck → CONFig → 等 gen → TRIGger → 等 epoch → 等 COMPLETE → 报告").pack(
            side="left", padx=12)

        form = ttk.LabelFrame(self, text="C 段：GUIDE:CONFig", style="Card.TLabelframe",
                              padding=(12, 8))
        form.pack(fill="x", padx=10, pady=6)
        fields = (
            ("action", self.action), ("plan_crc", self.crc),
            ("generation", self.generation), ("window_ms", self.window_ms),
            ("trials", self.trials), ("candidate_count", self.candidate_count),
            ("candidates", self.candidates),
            ("threshold_low", self.threshold_low),
            ("threshold_high", self.threshold_high),
        )
        for index, (label, variable) in enumerate(fields):
            row, column = divmod(index, 3)
            ttk.Label(form, text=label, style="Field.TLabel").grid(
                row=row, column=column * 2, sticky="e", padx=(0, 4), pady=2)
            ttk.Entry(form, textvariable=variable, width=16).grid(
                row=row, column=column * 2 + 1, sticky="w", padx=(0, 12), pady=2)
        self.config_button = ttk.Button(
            form, text="下发 CONFig", style="Accent.TButton",
            command=self.send_config)
        self.config_button.grid(row=3, column=0, columnspan=2, sticky="w", pady=(8, 0))
        ttk.Checkbutton(
            form, text="CRC=0x12340000+gen", variable=self.auto_crc).grid(
            row=4, column=0, columnspan=2, sticky="w", pady=(4, 0))
        self.plan_button = ttk.Button(form, text="读 PLAN?", command=self.read_plan)
        self.plan_button.grid(row=3, column=2, sticky="w", pady=(8, 0))
        self.scan_button = ttk.Button(form, text="读 SCAN?", command=self.read_scan)
        self.scan_button.grid(row=3, column=4, sticky="w", pady=(8, 0))
        self.pair_button = ttk.Button(form, text="读 PAIR?", command=self.read_pair)
        self.pair_button.grid(row=3, column=6, sticky="w", pady=(8, 0))

        trigger = ttk.LabelFrame(self, text="T 段：GUIDE:TRIGger",
                                 style="Card.TLabelframe", padding=(12, 8))
        trigger.pack(fill="x", padx=10, pady=6)
        ttk.Label(trigger, text="epoch", style="Field.TLabel").grid(
            row=0, column=0, sticky="e")
        ttk.Entry(trigger, textvariable=self.epoch, width=16).grid(
            row=0, column=1, sticky="w", padx=(4, 12))
        self.trigger_button = ttk.Button(
            trigger, text="触发 TRIGger", style="Accent.TButton",
            command=self.send_trigger)
        self.trigger_button.grid(row=0, column=2, padx=(8, 0))
        self.ring_button = ttk.Button(
            trigger, text="读 RING:STATus?", command=self.read_ring)
        self.ring_button.grid(row=0, column=3, padx=(8, 0))

        ttk.Label(self, text="读回", style="Hint.TLabel").pack(
            anchor="w", padx=12, pady=(4, 2))
        self.output = ScrolledText(self, height=14, font=("Consolas", 9),
                                   background="#f8fafc", relief="flat")
        self.output.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        self._mode_changed()

    def _mode_changed(self) -> None:
        if self.mode.get() == "10 kHz":
            self.status.set("10 kHz · level 23 · 长预算")
        else:
            self.status.set("10 MHz · level 7")
        self.flow_button.configure(state="normal")

    def _level(self) -> int:
        return 23 if self.mode.get() == "10 kHz" else 7

    def _u32(self, variable: tk.StringVar, name: str) -> int:
        try:
            return int(variable.get(), 0)
        except ValueError as exc:
            raise ValueError(f"{name} 必须是 u32") from exc

    def _record(self, text: str) -> None:
        self.output.insert("end", text + "\n")
        self.output.see("end")

    def render(self, vector) -> None:
        state = "normal" if vector.connection.connected else "disabled"
        for button in (self.config_button, self.plan_button, self.scan_button,
                       self.pair_button, self.trigger_button, self.ring_button):
            button.configure(state=state)
        flow_state = ("normal" if vector.connection.connected and not self.busy
                      else "disabled")
        self.flow_button.configure(state=flow_state)
        self.cancel_button.configure(state="normal" if self.busy else "disabled")

    def _emit(self, text: str) -> None:
        self.app.pump.post(UiEvent("calflow", text))

    def note(self, text: str) -> None:
        self._record(text)

    def _request(self, command: str, timeout: float) -> str:
        if self.app.dispatcher is None:
            raise RuntimeError("未连接")
        result = self.app.dispatcher.request(command, "cal", timeout)
        if result.error:
            raise RuntimeError(result.error)
        return result.response

    def _flow_inputs(self) -> dict:
        command_values = self._config_command()
        # Parse the command back into the 16-word inputs used by the workflow.
        parts = command_values.split(maxsplit=1)[1].split(",")
        if len(parts) < 9:
            raise ValueError("CONFig 参数不完整")
        count = int(parts[6], 0)
        candidates = [int(item, 0) for item in parts[7:7 + count]]
        if len(candidates) != count:
            raise ValueError("candidates 数量不匹配")
        return {
            "action": int(parts[0], 0),
            "crc": int(parts[1], 0),
            "generation": int(parts[2], 0),
            "window_ms": int(parts[4], 0),
            "trials": int(parts[5], 0),
            "candidate_count": count,
            "candidates": candidates,
            "threshold_low": int(parts[7 + count], 0),
            "threshold_high": int(parts[8 + count], 0),
            "epoch": self._u32(self.epoch, "epoch"),
        }

    def run_flow(self) -> None:
        if self.busy:
            return
        try:
            inputs = self._flow_inputs()
        except ValueError as exc:
            self.app.dialogs.error("参数错误", str(exc))
            return
        level = 23 if self.mode.get() == "10 kHz" else 7
        label = "10k" if level == 23 else "10m"
        self.busy = True
        self.status.set("RUNNING")
        self.render(self.app.store.snapshot())
        self._record(f"启动 {self.mode.get()} 单口流程")
        import threading
        threading.Thread(target=self._run_flow, args=(inputs, level, label),
                         name="cal-guide-flow", daemon=True).start()

    def _run_flow(self, inputs: dict, level: int, label: str) -> None:
        workflow = GuideWorkflow(self, level=level, label=label)
        self.workflow_runner = workflow.runner
        try:
            state = workflow.start(inputs)
            self.app.pump.post(UiEvent(
                "calflow", f"流程完成：{state.status} run={state.run_id}"))
        except Exception as exc:
            self.app.pump.post(UiEvent("calflow", f"流程失败：{exc}"))
        finally:
            self.app.pump.post(UiEvent("calflow-done", ""))

    def flow_done(self) -> None:
        self.busy = False
        self.status.set("DONE")
        self.render(self.app.store.snapshot())

    def cancel(self) -> None:
        if self.workflow_runner is not None:
            self.workflow_runner.cancel()
            self._emit("已请求取消流程")
        self.busy = False
        self.status.set("CANCELLED")
        self.render(self.app.store.snapshot())

    def _send(self, command: str, source: str) -> None:
        self._record(f"> {command}")
        if not self.app.send_command(command, source):
            self._record("! 未发送（未连接或 OTA 独占）")

    def send_config(self) -> None:
        try:
            command = self._config_command()
        except ValueError as exc:
            self.app.dialogs.error("参数错误", str(exc))
            return
        self._send(command, "cal:config")

    def _config_command(self) -> str:
        action = self._u32(self.action, "action")
        generation = self._u32(self.generation, "generation")
        crc = (0x12340000 + generation) & 0xFFFFFFFF \
            if self.auto_crc.get() else self._u32(self.crc, "plan_crc")
        window = self._u32(self.window_ms, "window_ms")
        trials = self._u32(self.trials, "trials")
        count = self._u32(self.candidate_count, "candidate_count")
        if count > 5:
            raise ValueError("candidate_count 不能超过 5")
        values = [int(item, 0) for item in self.candidates.get().split(",")
                  if item.strip()]
        if len(values) != count:
            raise ValueError(f"candidates 需要 {count} 个值")
        low = self._u32(self.threshold_low, "threshold_low")
        high = self._u32(self.threshold_high, "threshold_high")
        parts = [action, crc, generation, self._level(), window, trials, count]
        parts.extend(values)
        parts.extend((low, high))
        return "SYST:TDMA:GUIDE:CONFig " + ",".join(str(item) for item in parts)

    def send_trigger(self) -> None:
        try:
            action = self._u32(self.action, "action")
            epoch = self._u32(self.epoch, "epoch")
        except ValueError as exc:
            self.app.dialogs.error("参数错误", str(exc))
            return
        self._send(f"SYST:TDMA:GUIDE:TRIGger {action},{epoch}", "cal:trigger")

    def read_plan(self) -> None:
        self._send("SYST:TDMA:GUIDE:PLAN?", "cal:plan")

    def read_scan(self) -> None:
        self._send("SYST:TDMA:GUIDE:SCAN?", "cal:scan")

    def read_pair(self) -> None:
        self._send("SYST:TDMA:GUIDE:PAIR?", "cal:pair")

    def read_ring(self) -> None:
        self._send("SYST:TDMA:RING:STATus?", "cal:ring")

    def show_result(self, source: str, response: str, error: str) -> None:
        if error:
            self._record(f"! {source} → {error}")
            return
        self._record(f"< {response}")
        if source == "cal:plan":
            values = [item.strip() for item in response.split(",")]
            self._record("  PLAN? fields:")
            for index, name in enumerate(PLAN_FIELDS):
                if index < len(values):
                    self._record(f"    {index:02d} {name} = {values[index]}")
