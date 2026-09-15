#!/usr/bin/env python3
"""Small Tk interface for single-board sequence-trigger SCPI debugging."""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from tkinter.scrolledtext import ScrolledText
import threading
import sys
from pathlib import Path

# When launched as ``python tools/sequence_trigger_debug_ui.py`` Python adds
# ``tools`` (the script directory) to sys.path, not the repository root.
# Add the root explicitly so the shared SCPI helpers remain importable.
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import open_serial_port
from tools.scpi_query.scpi_query import send_command


class SequenceUi(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("DHRT100 序列触发调试")
        self.geometry("760x560")
        self.port = tk.StringVar(value="COM10")
        self.backend = tk.StringVar(value="Serial")
        self.source = tk.StringVar(value="IN1")
        self.edge = tk.StringVar(value="RIS")
        self.settle = tk.StringVar(value="10")
        self.pulse = tk.StringVar(value="10")
        self.plan = tk.StringVar(value="SP8T")
        self.codes = tk.StringVar(value="0,1,2,3,4,5,6,7")
        self.status = tk.StringVar(value="未连接")
        self.mode_hint = tk.StringVar(value="外部脉冲模式：启动后等待输入脉冲")
        self.io_inputs = tk.StringVar(value="输入：—")
        self.io_outputs = tk.StringVar(value="输出：—")
        self.io_owned = tk.StringVar(value="占用：—")
        self.io_state = tk.StringVar(value="IO状态：—")
        self.input_lamps: list[tk.Label] = []
        self.output_lamps: list[tk.Label] = []
        self.step_button: ttk.Button | None = None
        self._build()

    def _build(self) -> None:
        cfg = ttk.LabelFrame(self, text="连接与序列配置")
        cfg.pack(fill="x", padx=8, pady=8)
        fields = [("资源/端口", self.port), ("计划", self.plan), ("状态编码", self.codes),
                  ("建立时间 µs", self.settle), ("完成脉冲 µs", self.pulse)]
        for col, (label, var) in enumerate(fields):
            ttk.Label(cfg, text=label).grid(row=0, column=col, padx=4, pady=3)
            ttk.Entry(cfg, textvariable=var, width=16).grid(row=1, column=col, padx=4)
        ttk.Label(cfg, text="通信后端").grid(row=2, column=4, padx=4, pady=3)
        backend_box = ttk.Combobox(cfg, textvariable=self.backend, values=["Serial", "USB TMC"], state="readonly", width=13)
        backend_box.grid(row=3, column=4, padx=4)
        ttk.Label(cfg, text="输入").grid(row=2, column=0, padx=4, pady=3)
        source_box = ttk.Combobox(cfg, textvariable=self.source, values=["BUS", "IN1", "IN2", "IN3", "IN4"], state="readonly", width=13)
        source_box.grid(row=3, column=0, padx=4)
        source_box.bind("<<ComboboxSelected>>", lambda _event: self.update_mode_hint())
        ttk.Label(cfg, text="边沿").grid(row=2, column=1, padx=4, pady=3)
        ttk.Combobox(cfg, textvariable=self.edge, values=["RIS", "FALL"], width=13).grid(row=3, column=1, padx=4)
        ttk.Button(cfg, text="连接/配置", command=self.configure).grid(row=3, column=4, padx=8)

        ctl = ttk.LabelFrame(self, text="控制")
        ctl.pack(fill="x", padx=8, pady=4)
        for text, command in [("启动/等待触发", "TRIG:START"), ("软件单步", "TRIG:SEQ:STEP"),
                              ("NEXT", "CONF:SEQ:NEXT"), ("暂停", "TRIG:PAUS"),
                              ("继续", "TRIG:CONT"), ("停止", "TRIG:STOP")]:
            button = ttk.Button(ctl, text=text, command=lambda c=command: self.command(c))
            button.pack(side="left", padx=4, pady=6)
            if command == "TRIG:SEQ:STEP":
                self.step_button = button
        ttk.Button(ctl, text="刷新状态", command=lambda: self.command("READ:SEQ:NEXT?")).pack(side="left", padx=12)
        ttk.Button(ctl, text="清空日志", command=self.clear_log).pack(side="left", padx=4)
        ttk.Label(ctl, textvariable=self.status).pack(side="right", padx=8)
        ttk.Label(self, textvariable=self.mode_hint, foreground="#155e75").pack(anchor="w", padx=12)
        io = ttk.LabelFrame(self, text="实时 IO")
        io.pack(fill="x", padx=8, pady=4)
        in_frame = ttk.LabelFrame(io, text="输入脉冲（按边沿推进）")
        in_frame.pack(side="left", padx=6, pady=3)
        out_frame = ttk.LabelFrame(io, text="输出电平（OUT1–OUT3编码，OUT4完成脉冲）")
        out_frame.pack(side="left", padx=6, pady=3)
        for index in range(4):
            lamp = tk.Label(in_frame, text=f"IN{index + 1}\n低", width=7, height=2, bg="#d1d5db", relief="groove")
            lamp.pack(side="left", padx=2, pady=2)
            self.input_lamps.append(lamp)
            lamp = tk.Label(out_frame, text=f"OUT{index + 1}\n低", width=7, height=2, bg="#d1d5db", relief="groove")
            lamp.pack(side="left", padx=2, pady=2)
            self.output_lamps.append(lamp)
        ttk.Label(io, textvariable=self.io_owned).pack(side="left", padx=10)
        ttk.Label(io, textvariable=self.io_state).pack(side="left", padx=10)
        self.update_mode_hint()

        self.output = ScrolledText(self, height=22, state="disabled", font=("Consolas", 10))
        self.output.pack(fill="both", expand=True, padx=8, pady=8)

    def log(self, text: str) -> None:
        self.output.configure(state="normal")
        self.output.insert("end", text + "\n")
        self.output.see("end")
        self.output.configure(state="disabled")

    def clear_log(self) -> None:
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def update_mode_hint(self) -> None:
        if self.source.get() == "BUS":
            self.mode_hint.set("BUS 软件触发模式：可使用“软件单步”或 NEXT")
            if self.step_button is not None:
                self.step_button.state(["!disabled"])
        else:
            self.mode_hint.set(f"{self.source.get()} 外部脉冲模式：点击“启动/等待触发”，每个{self.edge.get()}沿推进一步")
            if self.step_button is not None:
                self.step_button.state(["disabled"])

    def run_commands(self, commands: list[str]) -> None:
        try:
            if self.backend.get() == "USB TMC":
                try:
                    import pyvisa
                except ImportError as exc:
                    raise RuntimeError("USB TMC 需要安装 pyvisa（例如: uv pip install pyvisa）") from exc
                rm = pyvisa.ResourceManager()
                instrument = rm.open_resource(self.port.get())
                instrument.timeout = 2000
                try:
                    for command in commands:
                        instrument.write(command)
                        response = instrument.query(command) if "?" in command.split(maxsplit=1)[0] else "OK"
                        self.after(0, self.log, f"> {command}\n< {response.strip()}")
                        self.after(0, self.status.set, response.strip())
                        self.after(0, self.update_io, command, response.strip())
                finally:
                    instrument.close()
                    rm.close()
            else:
                with open_serial_port(self.port.get(), 115200, 2, .2, read_timeout_s=.2) as ser:
                    for command in commands:
                        response = send_command(ser, command, 2)
                        self.after(0, self.log, f"> {command}\n< {response}")
                        self.after(0, self.status.set, response)
                        self.after(0, self.update_io, command, response)
        except Exception as exc:
            self.after(0, self.log, f"错误: {exc}")
            self.after(0, self.status.set, "连接错误")

    def update_io(self, command: str, response: str) -> None:
        try:
            values = [int(x) for x in response.strip().split(",")]
        except ValueError:
            return
        if command.upper().startswith("READ:IO:STAT?") and len(values) == 5:
            self.io_inputs.set(f"输入：0x{values[0]:X}")
            self.io_outputs.set(f"输出：0x{values[1]:X}")
            self.io_owned.set(f"占用：0x{values[2]:X}")
            self.io_state.set(f"已武装={values[3]} 忙={values[4]}")
            for index, lamp in enumerate(self.input_lamps):
                high = bool(values[0] & (1 << index))
                lamp.configure(text=f"IN{index + 1}\n{'高' if high else '低'}", bg="#4ade80" if high else "#d1d5db")
            for index, lamp in enumerate(self.output_lamps):
                high = bool(values[1] & (1 << index))
                lamp.configure(text=f"OUT{index + 1}\n{'高' if high else '低'}", bg="#4ade80" if high else "#d1d5db")

    def command(self, command: str) -> None:
        if command == "TRIG:SEQ:STEP" and self.source.get() != "BUS":
            self.log("当前为外部脉冲模式，软件单步已禁用；请使用“启动/等待触发”。")
            return
        commands = [command]
        if not command.endswith("?"):
            commands.append("READ:IO:STAT?")
        threading.Thread(target=self.run_commands, args=(commands,), daemon=True).start()

    def configure(self) -> None:
        try:
            codes = [int(x.strip()) for x in self.codes.get().split(",") if x.strip()]
            commands = ["TRIG:STOP", f"CONF:TRIG {len(codes)},0,1,1",
                        f"CONF:SEQ {self.plan.get()}," + ",".join(map(str, codes)),
                        f"CONF:SEQ:ACT {self.plan.get()}",
                        f"CONF:SEQ:IO 7,OUT4,{int(self.settle.get())},{int(self.pulse.get())}"]
            commands += [f"CONF:SEQ:CODE {i},{code}" for i, code in enumerate(codes)]
            commands += [f"CONF:SEQ:SOUR {self.source.get()},{self.edge.get()}", "READ:SEQ:NEXT?", "READ:IO:STAT?"]
        except ValueError as exc:
            self.log(f"配置错误: {exc}")
            return
        threading.Thread(target=self.run_commands, args=(commands,), daemon=True).start()


if __name__ == "__main__":
    SequenceUi().mainloop()
