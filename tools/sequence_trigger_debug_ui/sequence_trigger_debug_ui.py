#!/usr/bin/env python3
"""Small Tk interface for single-board sequence-trigger SCPI debugging."""

from __future__ import annotations

import csv
from datetime import datetime
import queue
import re
import subprocess
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText
import threading
import sys
from pathlib import Path

from serial.tools import list_ports

# When launched from its tool directory Python adds only that directory to
# sys.path, not the repository root.
# Add the root explicitly so the shared SCPI helpers remain importable.
ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import open_serial_port
from tools.scpi_query.scpi_query import send_command


MAX_LOG_LINES = 3000
TIME_MAX_US = 0xffffffff // 10
ROLE_SEQUENCE = "序列编码"
ROLE_STATUS = "状态输出"


def discover_serial_ports(candidates=None) -> list[str]:
    ports = list(list_ports.comports() if candidates is None else candidates)

    def sort_key(info) -> tuple[int, int, str]:
        device = str(info.device)
        match = re.fullmatch(r"COM(\d+)", device, re.IGNORECASE)
        number = int(match.group(1)) if match else 0x7fffffff
        return (0 if getattr(info, "vid", None) == 0x2e8a else 1,
                number, device.casefold())

    return [str(info.device) for info in sorted(ports, key=sort_key)]


def discover_visa_resources(resource_manager=None) -> list[str]:
    owned = resource_manager is None
    if owned:
        import pyvisa
        resource_manager = pyvisa.ResourceManager()
    try:
        resources = resource_manager.list_resources("USB?*::INSTR")
        return sorted(str(resource) for resource in resources)
    finally:
        if owned:
            resource_manager.close()


def build_ota_command(image: Path, resource: str, out_dir: Path,
                      backend: str = "Serial") -> list[str]:
    if backend == "USB TMC":
        return [
            sys.executable,
            str(ROOT / "tools" / "visa_ota_send" / "visa_ota_send.py"),
            resource, str(image), "--boot",
        ]
    return [
        sys.executable,
        str(ROOT / "tools" / "ota_multi_update" / "ota_multi_update.py"),
        str(image),
        "--ports", resource,
        "--expected-board-count", "1",
        "--max-workers", "1",
        "--out-dir", str(out_dir),
    ]


def build_configuration_commands(plan: str, codes: list[int], source: str,
                                 edge: str, settle_us: int,
                                 pulse_us: int,
                                 sequence_output_mask: int = 7,
                                 status_output_mask: int = 8,
                                 status_mode: str = "PULSE") -> list[str]:
    mode = {"电平": "LEVEL", "脉冲": "PULSE"}.get(
        status_mode, status_mode.upper())
    if (not sequence_output_mask or not status_output_mask or
            (sequence_output_mask | status_output_mask) & ~15 or
            sequence_output_mask & status_output_mask):
        raise ValueError("序列编码与状态输出必须各占至少一个且互不重叠的 OUT")
    if mode not in {"LEVEL", "PULSE"}:
        raise ValueError("状态输出形式必须是电平或脉冲")
    if not 0 <= settle_us <= TIME_MAX_US:
        raise ValueError("建立时间超出固件范围")
    if mode == "PULSE" and not 0 < pulse_us <= TIME_MAX_US:
        raise ValueError("脉冲宽度必须大于 0 且不超出固件范围")
    configured_pulse = pulse_us if mode == "PULSE" else 0
    if not codes:
        raise ValueError("至少需要一个输出编码")
    if any(code < 0 or code & ~sequence_output_mask for code in codes):
        raise ValueError(
            f"输出编码只能使用序列编码 OUT（掩码 0x{sequence_output_mask:X}）")
    states = list(range(len(codes)))
    commands = [
        "TRIG:STOP",
        f"CONF:TRIG {len(states)},0,1,1",
        f"CONF:SEQ {plan}," + ",".join(map(str, states)),
        f"CONF:SEQ:ACT {plan}",
        f"CONF:SEQ:OUTPUT {sequence_output_mask},{status_output_mask},"
        f"{mode},{settle_us},{configured_pulse}",
    ]
    commands += [f"CONF:SEQ:CODE {state},{code}"
                 for state, code in zip(states, codes)]
    commands += [f"CONF:SEQ:SOUR {source},{edge}",
                 "READ:SEQ:NEXT?", "READ:IO:STAT?"]
    return commands


def format_switch_position(response: str) -> str:
    fields = next(csv.reader([response], strict=True))
    if len(fields) < 7:
        raise ValueError("序列状态字段不足")
    runtime_state = fields[0]
    count = int(fields[3])
    current_index = int(fields[4])
    current_state = int(fields[5])
    if runtime_state == "IDLE" or count == 0 or current_index == 0xffffffff:
        return "当前开关：未运行（输出已释放）"
    if current_index >= count:
        raise ValueError("当前开关位置越界")
    return (f"当前开关：第 {current_index + 1}/{count} 位 "
            f"（状态 {current_state}，运行态 {runtime_state}）")


def parse_usb_mode(response: str) -> str:
    mode = response.strip().strip('"').upper()
    if mode not in {"CDC", "USBTMC"}:
        raise ValueError("当前固件未启用 PROJECT_ENABLE_USB_RUNTIME_SWITCH")
    return mode


class SequenceUi(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("DHRT100 序列触发调试")
        self.geometry("1380x900")
        self.minsize(1220, 800)
        super().configure(bg="#f3f4f6")
        self._configure_style()
        self.port = tk.StringVar()
        self.backend = tk.StringVar(value="Serial")
        self.source = tk.StringVar(value="BUS")
        self.edge = tk.StringVar(value="RIS")
        self.settle = tk.StringVar(value="10")
        self.pulse = tk.StringVar(value="10")
        self.status_mode = tk.StringVar(value="脉冲")
        self.out_enabled = [tk.BooleanVar(value=True) for _ in range(4)]
        self.out_roles = [tk.StringVar(value=ROLE_SEQUENCE) for _ in range(3)] + [
            tk.StringVar(value=ROLE_STATUS)]
        self.plan = tk.StringVar(value="SP8T")
        self.codes = tk.StringVar(value="0,1,2,3,4,5,6,7")
        self.status = tk.StringVar(value="未连接")
        self.mode_hint = tk.StringVar(value="外部脉冲模式：启动后等待输入脉冲")
        self.io_inputs = tk.StringVar(value="输入：—")
        self.io_outputs = tk.StringVar(value="输出：—")
        self.io_owned = tk.StringVar(value="占用：—")
        self.io_state = tk.StringVar(value="IO状态：—")
        self.switch_position = tk.StringVar(value="当前开关：未运行")
        self.ota_file = tk.StringVar()
        self.ota_status = tk.StringVar(value="未选择固件")
        self.manual_command = tk.StringVar()
        self.auto_scroll = tk.BooleanVar(value=True)
        self.input_lamps: list[tk.Label] = []
        self.output_lamps: list[tk.Label] = []
        self.port_box: ttk.Combobox | None = None
        self.step_button: ttk.Button | None = None
        self.pulse_entry: ttk.Entry | None = None
        self.ota_progress: ttk.Progressbar | None = None
        self._ota_running = False
        self._transport_switching = False
        self._operations: queue.Queue[tuple[str, object] | None] = queue.Queue()
        self._ui_events: queue.Queue[tuple[str, tuple]] = queue.Queue()
        self._closing = False
        self._build()
        self.refresh_ports(log_result=False)
        self._worker = threading.Thread(target=self._operation_loop, daemon=True)
        self._worker.start()
        self.after(50, self._poll_ui_events)
        self.protocol("WM_DELETE_WINDOW", self.close)

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure(".", font=("Microsoft YaHei UI", 9),
                        background="#f3f4f6", foreground="#111827")
        style.configure("App.TFrame", background="#f3f4f6")
        style.configure("Panel.TFrame", background="#ffffff")
        style.configure("Section.TLabel", background="#ffffff",
                        foreground="#111827",
                        font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("Field.TLabel", background="#ffffff",
                        foreground="#4b5563")
        style.configure("Hint.TLabel", background="#f3f4f6",
                        foreground="#0f766e")
        style.configure("Position.TLabel", background="#ffffff",
                        foreground="#111827",
                        font=("Microsoft YaHei UI", 11, "bold"))
        style.configure("Status.TLabel", background="#ffffff",
                        foreground="#4b5563")
        style.configure("TEntry", fieldbackground="#ffffff",
                        foreground="#111827", borderwidth=1,
                        relief="flat", padding=6)
        style.configure("TCombobox", fieldbackground="#ffffff",
                        foreground="#111827", borderwidth=1,
                        relief="flat", padding=5)
        style.configure("TButton", background="#e5e7eb",
                        foreground="#111827", borderwidth=0,
                        relief="flat", padding=(11, 7))
        style.map("TButton",
                  background=[("active", "#d1d5db"),
                              ("disabled", "#f3f4f6")],
                  foreground=[("disabled", "#9ca3af")])
        style.configure("Primary.TButton", background="#0f766e",
                        foreground="#ffffff")
        style.map("Primary.TButton",
                  background=[("active", "#115e59"),
                              ("disabled", "#99f6e4")])
        style.configure("Danger.TButton", background="#fee2e2",
                        foreground="#991b1b")
        style.map("Danger.TButton", background=[("active", "#fecaca")])
        style.configure("TNotebook", background="#f3f4f6", borderwidth=0)
        style.configure("TNotebook.Tab", background="#e5e7eb",
                        foreground="#4b5563", borderwidth=0,
                        padding=(16, 7))
        style.map("TNotebook.Tab",
                  background=[("selected", "#ffffff")],
                  foreground=[("selected", "#0f766e")])
        style.configure("TProgressbar", background="#0f766e",
                        troughcolor="#e5e7eb", borderwidth=0,
                        relief="flat")
        style.configure("TCheckbutton", background="#f3f4f6",
                        foreground="#4b5563")

    def _build(self) -> None:
        cfg = ttk.Frame(self, style="Panel.TFrame", padding=(14, 10))
        cfg.pack(fill="x", padx=12, pady=(12, 6))
        ttk.Label(cfg, text="连接与序列配置", style="Section.TLabel").grid(
            row=0, column=0, columnspan=8, sticky="w", pady=(0, 8))
        fields = [
            ("资源/端口", self.port, "port", 11),
            ("通信后端", self.backend, "backend", 12),
            ("计划", self.plan, "entry", 12),
            ("输出编码（首项为 START）", self.codes, "entry", 24),
            ("建立时间 µs", self.settle, "entry", 11),
            ("完成脉冲 µs", self.pulse, "entry", 11),
            ("输入", self.source, "source", 10),
            ("边沿", self.edge, "edge", 10),
        ]
        source_box = None
        for col, (label, var, kind, width) in enumerate(fields):
            cfg.columnconfigure(col, weight=2 if col == 3 else 1)
            ttk.Label(cfg, text=label, style="Field.TLabel").grid(
                row=1, column=col, sticky="w", padx=(0, 8), pady=(0, 4))
            if kind == "backend":
                widget = ttk.Combobox(cfg, textvariable=var,
                                      values=["Serial", "USB TMC"],
                                      state="readonly", width=width)
                widget.bind("<<ComboboxSelected>>",
                            lambda _event: self.refresh_ports())
            elif kind == "port":
                widget = ttk.Combobox(cfg, textvariable=var, width=width)
                self.port_box = widget
            elif kind == "source":
                widget = ttk.Combobox(cfg, textvariable=var,
                                      values=["BUS", "IN1", "IN2", "IN3", "IN4"],
                                      state="readonly", width=width)
                source_box = widget
            elif kind == "edge":
                widget = ttk.Combobox(cfg, textvariable=var,
                                      values=["RIS", "FALL"],
                                      state="readonly", width=width)
            else:
                widget = ttk.Entry(cfg, textvariable=var, width=width)
                if var is self.pulse:
                    self.pulse_entry = widget
            widget.grid(row=2, column=col, sticky="ew", padx=(0, 8))
        assert source_box is not None
        source_box.bind("<<ComboboxSelected>>", lambda _event: self.update_mode_hint())
        roles = ttk.Frame(cfg, style="Panel.TFrame")
        roles.grid(row=3, column=0, columnspan=6, sticky="w", pady=(12, 0))
        ttk.Label(roles, text="OUT 角色", style="Field.TLabel").pack(
            side="left", padx=(0, 10))
        for index in range(4):
            ttk.Checkbutton(roles, text=f"OUT{index + 1}",
                            variable=self.out_enabled[index]).pack(side="left")
            ttk.Combobox(roles, textvariable=self.out_roles[index],
                         values=[ROLE_SEQUENCE, ROLE_STATUS], state="readonly",
                         width=9).pack(side="left", padx=(2, 10))
        ttk.Label(roles, text="状态形式", style="Field.TLabel").pack(
            side="left", padx=(4, 4))
        status_mode = ttk.Combobox(
            roles, textvariable=self.status_mode, values=["电平", "脉冲"],
            state="readonly", width=7)
        status_mode.pack(side="left")
        status_mode.bind("<<ComboboxSelected>>",
                         lambda _event: self._update_status_mode())
        self._update_status_mode()
        ttk.Button(cfg, text="扫描串口", command=self.refresh_ports).grid(
            row=4, column=5, sticky="e", pady=(10, 0), padx=(0, 8))
        ttk.Button(cfg, text="连接并配置", command=self.configure,
                   style="Primary.TButton").grid(
                       row=4, column=6, columnspan=2, sticky="e",
                       pady=(10, 0), padx=(0, 8))

        notebook = ttk.Notebook(self)
        notebook.pack(fill="x", padx=12, pady=6)
        sequence_page = ttk.Frame(notebook, style="App.TFrame")
        maintenance_page = ttk.Frame(notebook, style="App.TFrame")
        notebook.add(sequence_page, text="序列控制")
        notebook.add(maintenance_page, text="设备维护")

        ctl = ttk.Frame(sequence_page, style="Panel.TFrame", padding=(14, 10))
        ctl.pack(fill="x", pady=(0, 6))
        ttk.Label(ctl, text="运行控制", style="Section.TLabel").pack(
            side="left", padx=(0, 14))
        for text, command in [("启动/等待触发", "TRIG:START"), ("软件单步", "TRIG:SEQ:STEP"),
                              ("NEXT", "CONF:SEQ:NEXT"), ("暂停", "TRIG:PAUS"),
                              ("继续", "TRIG:CONT"), ("停止", "TRIG:STOP")]:
            button_style = "Primary.TButton" if command == "TRIG:START" else \
                ("Danger.TButton" if command == "TRIG:STOP" else "TButton")
            button = ttk.Button(ctl, text=text, style=button_style,
                                command=lambda c=command: self.command(c))
            button.pack(side="left", padx=(0, 6))
            if command == "TRIG:SEQ:STEP":
                self.step_button = button
        ttk.Button(ctl, text="刷新", command=lambda: self.command("READ:SEQ:NEXT?")).pack(
            side="left", padx=(8, 6))
        ttk.Label(ctl, textvariable=self.status, style="Status.TLabel").pack(
            side="right", padx=(12, 0))
        ttk.Label(sequence_page, textvariable=self.mode_hint,
                  style="Hint.TLabel").pack(
                      anchor="w", padx=6, pady=(0, 4))
        io = ttk.Frame(sequence_page, style="Panel.TFrame", padding=(14, 10))
        io.pack(fill="x")
        ttk.Label(io, text="实时 IO", style="Section.TLabel").pack(
            side="top", anchor="w")
        ttk.Label(io, textvariable=self.switch_position,
                  style="Position.TLabel").pack(
                      side="top", anchor="w", pady=(5, 8))
        io_levels = ttk.Frame(io, style="Panel.TFrame")
        io_levels.pack(fill="x")
        in_frame = ttk.Frame(io_levels, style="Panel.TFrame")
        in_frame.pack(side="left", padx=(0, 18))
        ttk.Label(in_frame, text="输入脉冲", style="Field.TLabel").pack(
            anchor="w", pady=(0, 4))
        input_lamps = ttk.Frame(in_frame, style="Panel.TFrame")
        input_lamps.pack()
        out_frame = ttk.Frame(io_levels, style="Panel.TFrame")
        out_frame.pack(side="left", padx=(0, 18))
        ttk.Label(out_frame, text="输出电平", style="Field.TLabel").pack(
            anchor="w", pady=(0, 4))
        output_lamps = ttk.Frame(out_frame, style="Panel.TFrame")
        output_lamps.pack()
        for index in range(4):
            lamp = tk.Label(input_lamps, text=f"IN{index + 1}\n低",
                            width=7, height=2, bg="#e5e7eb", fg="#374151",
                            relief="flat", highlightthickness=1,
                            highlightbackground="#d1d5db")
            lamp.pack(side="left", padx=(0, 4))
            self.input_lamps.append(lamp)
            lamp = tk.Label(output_lamps, text=f"OUT{index + 1}\n低",
                            width=7, height=2, bg="#e5e7eb", fg="#374151",
                            relief="flat", highlightthickness=1,
                            highlightbackground="#d1d5db")
            lamp.pack(side="left", padx=(0, 4))
            self.output_lamps.append(lamp)
        metrics = ttk.Frame(io_levels, style="Panel.TFrame")
        metrics.pack(side="left", fill="x", expand=True)
        ttk.Label(metrics, textvariable=self.io_owned, style="Status.TLabel").pack(anchor="w")
        ttk.Label(metrics, textvariable=self.io_state, style="Status.TLabel").pack(anchor="w", pady=(5, 0))
        self.update_mode_hint()

        self._build_maintenance(maintenance_page)

        log_bar = ttk.Frame(self, style="App.TFrame")
        log_bar.pack(fill="x", padx=12, pady=(6, 3))
        ttk.Label(log_bar, text="命令日志 / 响应输出").pack(side="left")
        ttk.Checkbutton(log_bar, text="自动滚动",
                        variable=self.auto_scroll).pack(side="right")
        ttk.Button(log_bar, text="导出日志", command=self.export_log).pack(
            side="right", padx=(6, 0))
        ttk.Button(log_bar, text="清空", command=self.clear_log).pack(side="right")
        self.output = ScrolledText(
            self, height=20, state="disabled", font=("Consolas", 10),
            bg="#ffffff", fg="#1f2937", insertbackground="#111827",
            selectbackground="#99f6e4", selectforeground="#134e4a",
            relief="flat", borderwidth=0, highlightthickness=1,
            highlightbackground="#d1d5db", highlightcolor="#0f766e",
            padx=10, pady=8)
        self.output.pack(fill="both", expand=True, padx=12, pady=(0, 12))
        self.output.tag_configure("TIME", foreground="#6b7280")
        self.output.tag_configure("CMD", foreground="#0369a1")
        self.output.tag_configure("OK", foreground="#15803d")
        self.output.tag_configure("INFO", foreground="#374151")
        self.output.tag_configure("WARN", foreground="#a16207")
        self.output.tag_configure("ERROR", foreground="#b91c1c")
        self.output.tag_configure("OTA", foreground="#7c3aed")

    def _build_maintenance(self, parent: ttk.Frame) -> None:
        device = ttk.Frame(parent, style="Panel.TFrame", padding=(14, 10))
        device.pack(fill="x", pady=(0, 6))
        ttk.Label(device, text="设备信息", style="Section.TLabel").pack(
            side="left", padx=(0, 14))
        ttk.Button(device, text="读取身份",
                   command=lambda: self.enqueue_commands(
                       ["*IDN?", "SYST:FW:BUILD?", "SYST:OTA:STAT?"])).pack(
                           side="left", padx=(0, 6))
        ttk.Button(device, text="查询 USB 模式",
                   command=lambda: self.enqueue_commands(
                       ["SYST:USB:MODE?"])).pack(side="left", padx=(0, 6))
        ttk.Button(device, text="切到 USB TMC",
                   command=lambda: self.switch_usb_mode("USBTMC")).pack(
                       side="left", padx=(0, 6))
        ttk.Button(device, text="切回 CDC",
                   command=lambda: self.switch_usb_mode("CDC")).pack(side="left")

        ota = ttk.Frame(parent, style="Panel.TFrame", padding=(14, 10))
        ota.pack(fill="x", pady=(0, 6))
        ttk.Label(ota, text="单板 OTA", style="Section.TLabel").grid(
            row=0, column=0, columnspan=4, sticky="w", pady=(0, 8))
        ota.columnconfigure(1, weight=1)
        ttk.Label(ota, text="固件包", style="Field.TLabel").grid(
            row=1, column=0, sticky="w", padx=(0, 8))
        ttk.Entry(ota, textvariable=self.ota_file, state="readonly").grid(
            row=1, column=1, sticky="ew", padx=(0, 8))
        ttk.Button(ota, text="选择文件", command=self.select_ota_file).grid(
            row=1, column=2, padx=(0, 8))
        ttk.Button(ota, text="开始升级", command=self.start_ota,
                   style="Primary.TButton").grid(row=1, column=3)
        self.ota_progress = ttk.Progressbar(ota, mode="indeterminate")
        self.ota_progress.grid(row=2, column=0, columnspan=3,
                               sticky="ew", pady=(10, 0), padx=(0, 8))
        ttk.Label(ota, textvariable=self.ota_status,
                  style="Status.TLabel").grid(row=2, column=3, sticky="e",
                                               pady=(10, 0))

        custom = ttk.Frame(parent, style="Panel.TFrame", padding=(14, 10))
        custom.pack(fill="x")
        ttk.Label(custom, text="自定义 SCPI", style="Section.TLabel").pack(
            side="left", padx=(0, 14))
        entry = ttk.Entry(custom, textvariable=self.manual_command)
        entry.pack(side="left", fill="x", expand=True, padx=(0, 8))
        entry.bind("<Return>", lambda _event: self.send_manual_command())
        ttk.Button(custom, text="发送", command=self.send_manual_command).pack(
            side="right")

    def refresh_ports(self, log_result: bool = True) -> None:
        backend = self.backend.get()
        try:
            ports = (discover_serial_ports() if backend == "Serial" else
                     discover_visa_resources())
        except Exception as exc:
            ports = []
            if log_result:
                self.log(f"{backend} 资源扫描失败：{exc}", "ERROR")
        if self.port_box is not None:
            self.port_box.configure(values=ports)
        current = self.port.get().strip()
        if ports and current not in ports:
            self.port.set(ports[0])
        elif not ports:
            self.port.set("")
        kind = "串口" if backend == "Serial" else "USBTMC 资源"
        self.status.set(f"发现 {len(ports)} 个{kind}" if ports else
                        f"未发现{kind}")
        if log_result:
            self.log(f"{kind}扫描：" +
                     (", ".join(ports) if ports else "未发现设备"))

    def log(self, text: str, level: str = "INFO") -> None:
        self.output.configure(state="normal")
        self.output.insert("end", f"[{datetime.now():%H:%M:%S}] ", "TIME")
        self.output.insert("end", text + "\n", level)
        line_count = int(self.output.index("end-1c").split(".")[0])
        if line_count > MAX_LOG_LINES:
            self.output.delete("1.0", f"{line_count - MAX_LOG_LINES}.0")
        if self.auto_scroll.get():
            self.output.see("end")
        self.output.configure(state="disabled")

    def clear_log(self) -> None:
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def export_log(self) -> None:
        filename = filedialog.asksaveasfilename(
            defaultextension=".log",
            filetypes=[("日志文件", "*.log"), ("文本文件", "*.txt"),
                       ("所有文件", "*.*")],
            initialfile=f"dhrt100_sequence_{datetime.now():%Y%m%d_%H%M%S}.log")
        if not filename:
            return
        Path(filename).write_text(self.output.get("1.0", "end-1c") + "\n",
                                  encoding="utf-8")
        self.log(f"日志已导出：{filename}")

    def enqueue_commands(self, commands: list[str]) -> None:
        if self._ota_running or self._transport_switching:
            owner = "OTA" if self._ota_running else "USB 模式切换"
            self.log(f"{owner} 正在独占通信资源，当前命令未加入队列。", "WARN")
            return
        self._operations.put((
            "commands",
            (self.backend.get(), self.port.get().strip(), list(commands))))

    def _operation_loop(self) -> None:
        while True:
            operation = self._operations.get()
            if operation is None:
                return
            kind, payload = operation
            if kind == "commands":
                backend, resource, commands = payload
                self.run_commands(backend, resource, commands)
            elif kind == "ota":
                image, port, backend = payload
                self.run_ota(image, port, backend)
            elif kind == "usb-switch":
                backend, resource, mode = payload
                self.run_usb_switch(backend, resource, mode)

    def _poll_ui_events(self) -> None:
        while True:
            try:
                kind, args = self._ui_events.get_nowait()
            except queue.Empty:
                break
            if kind == "log":
                self.log(*args)
            elif kind == "exchange":
                self.log_exchange(*args)
            elif kind == "status":
                self.status.set(*args)
            elif kind == "ota-finish":
                self._finish_ota(*args)
            elif kind == "usb-switch-finish":
                self._finish_usb_mode_switch(*args)
        if not self._closing:
            self.after(50, self._poll_ui_events)

    def close(self) -> None:
        if self._ota_running:
            messagebox.showwarning("OTA 正在运行",
                                   "请等待升级完成后再关闭调试工具。")
            return
        self._closing = True
        self._operations.put(None)
        self.destroy()

    def switch_usb_mode(self, mode: str) -> None:
        target = "USB TMC" if mode == "USBTMC" else "Serial"
        if not messagebox.askyesno(
                "确认切换 USB 模式",
                f"设备将保存 {mode} 模式并重启 USB。当前连接会断开，是否继续？"):
            return
        self.log(f"准备切换 USB 模式：{mode}", "WARN")
        resource = self.port.get().strip()
        if not resource:
            self.log("未选择当前通信资源，无法切换 USB 模式。", "ERROR")
            return
        self._transport_switching = True
        self.status.set(f"正在切换到 {mode}")
        self._operations.put((
            "usb-switch", (self.backend.get(), resource, mode)))

    def _finish_usb_mode_switch(self, passed: bool, backend: str,
                                message: str) -> None:
        self._transport_switching = False
        self.log(message, "OK" if passed else "ERROR")
        if passed:
            self.backend.set(backend)
            self.status.set("等待 USB 重新枚举")
            self.after(3500, self.refresh_ports)
        else:
            self.status.set("USB 模式切换失败")

    def update_mode_hint(self) -> None:
        if self.source.get() == "BUS":
            self.mode_hint.set("BUS 软件触发模式：可使用“软件单步”或 NEXT")
            if self.step_button is not None:
                self.step_button.state(["!disabled"])
        else:
            self.mode_hint.set(f"{self.source.get()} 外部脉冲模式：点击“启动/等待触发”，每个{self.edge.get()}沿推进一步")
            if self.step_button is not None:
                self.step_button.state(["disabled"])

    def _update_status_mode(self) -> None:
        if self.pulse_entry is None:
            return
        if self.status_mode.get() == "电平":
            self.pulse_entry.state(["disabled"])
        else:
            self.pulse_entry.state(["!disabled"])

    def _output_role_masks(self) -> tuple[int, int]:
        sequence_mask = 0
        status_mask = 0
        for index, (enabled, role) in enumerate(
                zip(self.out_enabled, self.out_roles)):
            if not enabled.get():
                continue
            if role.get() == ROLE_SEQUENCE:
                sequence_mask |= 1 << index
            elif role.get() == ROLE_STATUS:
                status_mask |= 1 << index
            else:
                raise ValueError(f"OUT{index + 1} 角色无效")
        return sequence_mask, status_mask

    def run_commands(self, backend: str, resource: str,
                     commands: list[str]) -> None:
        try:
            if backend == "USB TMC":
                try:
                    import pyvisa
                except ImportError as exc:
                    raise RuntimeError("USB TMC 需要安装 pyvisa（例如: uv pip install pyvisa）") from exc
                rm = pyvisa.ResourceManager()
                instrument = rm.open_resource(resource)
                instrument.timeout = 2000
                try:
                    for command in commands:
                        if "?" in command.split(maxsplit=1)[0]:
                            response = instrument.query(command).strip()
                        else:
                            instrument.write(command)
                            response = "OK"
                        self._ui_events.put(("exchange", (command, response)))
                finally:
                    instrument.close()
                    rm.close()
            else:
                with open_serial_port(resource, 115200, 2, .2,
                                      read_timeout_s=.2) as ser:
                    for command in commands:
                        response = send_command(ser, command, 2)
                        self._ui_events.put(("exchange", (command, response)))
        except Exception as exc:
            self._ui_events.put(("log", (f"串口操作失败：{exc}", "ERROR")))
            self._ui_events.put(("status", ("连接错误",)))

    def run_usb_switch(self, backend: str, resource: str, mode: str) -> None:
        target_backend = "USB TMC" if mode == "USBTMC" else "Serial"

        def emit(command: str, response: str) -> None:
            self._ui_events.put(("exchange", (command, response)))

        try:
            if backend == "USB TMC":
                try:
                    import pyvisa
                except ImportError as exc:
                    raise RuntimeError("USB TMC 需要安装 pyvisa") from exc
                rm = pyvisa.ResourceManager()
                instrument = rm.open_resource(resource)
                instrument.timeout = 2500
                instrument.write_termination = "\n"
                instrument.read_termination = "\n"
                try:
                    current_response = instrument.query("SYST:USB:MODE?").strip()
                    emit("SYST:USB:MODE?", current_response)
                    current = parse_usb_mode(current_response)
                    if current != mode:
                        set_response = instrument.query(
                            f"SYST:USB:MODE {mode}").strip()
                        emit(f"SYST:USB:MODE {mode}", set_response)
                        verify_response = instrument.query(
                            "SYST:USB:MODE?").strip()
                        emit("SYST:USB:MODE?", verify_response)
                        if parse_usb_mode(verify_response) != mode:
                            raise RuntimeError("USB 模式写入后读回不一致")
                        instrument.write("SYST:USB:BOOT")
                        emit("SYST:USB:BOOT", "已发送，等待重新枚举")
                finally:
                    instrument.close()
                    rm.close()
            else:
                with open_serial_port(resource, 115200, 2, .2,
                                      read_timeout_s=.2) as ser:
                    current_response = send_command(
                        ser, "SYST:USB:MODE?", 2)
                    emit("SYST:USB:MODE?", current_response)
                    current = parse_usb_mode(current_response)
                    if current != mode:
                        set_response = send_command(
                            ser, f"SYST:USB:MODE {mode}", 3)
                        emit(f"SYST:USB:MODE {mode}", set_response)
                        verify_response = send_command(
                            ser, "SYST:USB:MODE?", 2)
                        emit("SYST:USB:MODE?", verify_response)
                        if parse_usb_mode(verify_response) != mode:
                            raise RuntimeError("USB 模式写入后读回不一致")
                        boot_response = send_command(
                            ser, "SYST:USB:BOOT", 2)
                        emit("SYST:USB:BOOT", boot_response)
            message = (f"USB 已处于 {mode} 模式。" if current == mode else
                       f"已写入 {mode} 并请求重启，等待设备重新枚举。")
            self._ui_events.put((
                "usb-switch-finish", (True, target_backend, message)))
        except Exception as exc:
            self._ui_events.put((
                "usb-switch-finish", (False, backend, str(exc))))

    def log_exchange(self, command: str, response: str) -> None:
        self.log(f"> {command}", "CMD")
        level = "ERROR" if response == "<timeout>" else "OK"
        self.log(f"< {response}", level)
        self.status.set(response)
        self.update_io(command, response)

    def select_ota_file(self) -> None:
        filename = filedialog.askopenfilename(
            title="选择 DHRT100 固件包",
            filetypes=[("DHRT100 OTA 包", "*.pkg"),
                       ("固件镜像", "*.bin"), ("所有文件", "*.*")])
        if filename:
            self.ota_file.set(filename)
            self.ota_status.set("固件已选择")
            self.log(f"已选择 OTA 固件：{filename}", "OTA")

    def start_ota(self) -> None:
        if self._ota_running:
            self.log("OTA 已在运行。", "WARN")
            return
        image = Path(self.ota_file.get())
        port = self.port.get().strip()
        if not image.is_file():
            messagebox.showwarning("固件不可用", "请选择有效的 .pkg 或 .bin 固件文件。")
            return
        if not port:
            messagebox.showwarning("串口不可用", "请先扫描并选择目标串口。")
            return
        if not messagebox.askyesno(
                "确认单板 OTA",
                f"将通过 {port} 更新当前识别到的一块 DHRT100，是否继续？"):
            return
        self._ota_running = True
        self.ota_status.set("升级中")
        if self.ota_progress is not None:
            self.ota_progress.start(12)
        self.log(f"OTA 开始：{image.name} -> {port}", "OTA")
        self._operations.put(("ota", (image, port, self.backend.get())))

    def run_ota(self, image: Path, port: str, backend: str) -> None:
        out_dir = (ROOT / "out" / "sequence-debug-ui" /
                   f"ota-{datetime.now():%Y%m%d-%H%M%S}")
        out_dir.mkdir(parents=True, exist_ok=True)
        command = build_ota_command(image, port, out_dir, backend)
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        try:
            output_lines: list[str] = []
            process = subprocess.Popen(
                command, cwd=ROOT, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                errors="replace", creationflags=creationflags)
            assert process.stdout is not None
            for line in process.stdout:
                message = line.rstrip()
                if message:
                    output_lines.append(message)
                    self._ui_events.put(("log", (message, "OTA")))
            returncode = process.wait()
            (out_dir / "ota.log").write_text(
                "\n".join(output_lines) + "\n", encoding="utf-8")
            if returncode != 0:
                raise RuntimeError(f"OTA 工具退出码 {returncode}")
            self._ui_events.put((
                "ota-finish", (True, f"升级完成，证据：{out_dir}")))
        except Exception as exc:
            self._ui_events.put((
                "ota-finish", (False, f"升级失败：{exc}")))

    def _finish_ota(self, passed: bool, message: str) -> None:
        self._ota_running = False
        if self.ota_progress is not None:
            self.ota_progress.stop()
        self.ota_status.set("升级完成" if passed else "升级失败")
        self.log(message, "OK" if passed else "ERROR")
        self.after(1200, self.refresh_ports)

    def send_manual_command(self) -> None:
        command = self.manual_command.get().strip()
        if command:
            self.enqueue_commands([command])

    def update_io(self, command: str, response: str) -> None:
        if command.upper().startswith("READ:SEQ:NEXT?"):
            try:
                self.switch_position.set(format_switch_position(response))
            except (ValueError, csv.Error):
                self.switch_position.set("当前开关：状态解析失败")
            return
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
                lamp.configure(text=f"IN{index + 1}\n{'高' if high else '低'}",
                               bg="#86efac" if high else "#e5e7eb")
            for index, lamp in enumerate(self.output_lamps):
                high = bool(values[1] & (1 << index))
                lamp.configure(text=f"OUT{index + 1}\n{'高' if high else '低'}",
                               bg="#86efac" if high else "#e5e7eb")

    def command(self, command: str) -> None:
        if command == "TRIG:SEQ:STEP" and self.source.get() != "BUS":
            self.log("当前为外部脉冲模式，软件单步已禁用；请使用“启动/等待触发”。")
            return
        commands = [command]
        if not command.endswith("?"):
            commands += ["READ:SEQ:NEXT?", "READ:IO:STAT?"]
        self.enqueue_commands(commands)

    def configure(self) -> None:
        try:
            if not self.port.get().strip():
                raise ValueError("请先扫描或输入串口")
            codes = [int(x.strip()) for x in self.codes.get().split(",") if x.strip()]
            sequence_mask, status_mask = self._output_role_masks()
            commands = build_configuration_commands(
                self.plan.get(), codes, self.source.get(), self.edge.get(),
                int(self.settle.get()), int(self.pulse.get()),
                sequence_mask, status_mask, self.status_mode.get())
        except ValueError as exc:
            self.log(f"配置错误: {exc}")
            return
        self.log(
            f"配置 {len(codes)} 个位置；TRIG:START 将直接输出首项编码 "
            f"{codes[0]}；序列掩码 0x{sequence_mask:X}，"
            f"状态掩码 0x{status_mask:X}（{self.status_mode.get()}）。")
        self.enqueue_commands(commands)


if __name__ == "__main__":
    SequenceUi().mainloop()
