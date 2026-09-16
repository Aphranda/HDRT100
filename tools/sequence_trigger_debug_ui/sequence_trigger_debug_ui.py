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
import time
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
from tools.tdma_ring_monitor.tdma_field_parse import RUNTIME_FIELDS


MAX_LOG_LINES = 3000
TIME_MAX_US = 0xffffffff // 10
ROLE_SEQUENCE = "编码"
ROLE_STATUS = "状态"
ROLE_GATEWAY = "触发"
MODE_INDEPENDENT = "独立 SP8T 序列"
MODE_RJ45 = "RJ45 物理回环 · SP8T + VNA 网关"
RING_STATUS_QUERY = "SYST:TDMA:RING:STAT?"
RING_ACK_ONLY = {"SYST:TDMA:RING:STOP", "SYST:TDMA:RING:ARM", "SYST:TDMA:RING:START"}
LINK_PHASES = {0: "未启用", 1: "等待启动", 2: "等待链路通知回环", 3: "等待 READY",
               4: "等待 READY 回环", 5: "等待序列切换", 6: "暂停", 7: "异常", 8: "已完成"}


def build_mode_configuration(mode: str, plan: str, codes: list[int], source: str,
                             edge: str, settle_us: int, pulse_us: int,
                             sequence_mask: int, status_mask: int, status_mode: str,
                             ready_input: str = "MANUAL", timeout_ms: int = 5000,
                             repeat_count: int = 1,
                             gateway_output: str = "OUT4") -> list[str]:
    if mode not in {MODE_INDEPENDENT, MODE_RJ45}:
        raise ValueError("请选择运行模式")
    if not 0 <= repeat_count <= 0xffffffff:
        raise ValueError("循环次数必须为非负整数；0 表示持续运行")
    if repeat_count and len(codes) * repeat_count > 0xffffffff:
        raise ValueError("循环次数与序列长度的乘积超出固件计数范围")
    combined = mode == MODE_RJ45
    normalized_status_mode = {"无": "NONE", "电平": "LEVEL", "脉冲": "PULSE"}.get(
        status_mode, status_mode.upper())
    gateway_masks = {f"OUT{index}": 1 << (index - 1) for index in range(1, 5)}
    if combined and (ready_input not in {"MANUAL", "IN1", "IN2", "IN3", "IN4"} or
                     edge not in {"RIS", "FALL"} or not 1 <= timeout_ms <= 0x7fffffff or
                     not 0 < pulse_us <= TIME_MAX_US or gateway_output not in gateway_masks or
                     status_mask != 0 or normalized_status_mode != "NONE" or
                     sequence_mask & gateway_masks.get(gateway_output, 0)):
        raise ValueError("请检查 READY、OUT 属性、触发脉宽和等待超时")
    base = build_configuration_commands(plan, codes, "MANUAL" if combined else source,
        edge, settle_us, 0 if combined else pulse_us,
        sequence_mask, 0 if combined else status_mask,
        "NONE" if combined else normalized_status_mode)
    commands = ["TRIG:STOP", "SYST:TDMA:RING:STOP", "CONF:SEQ:LINK OFF", *base[1:-2],
                f"CONF:SEQ:REPEAT {repeat_count}"]
    if combined:
        commands += ["CONF:SEQ:NODE:ROLE 2,5,DUT", "CONF:SEQ:NODE:ROLE 3,7,VNA",
                     "CONF:SEQ:NODE:ACT",
                     "SYST:TDMA:OPMODE:STAGE 7", "SYST:TDMA:OPMODE:APPLY",
                     "SYST:TDMA:RING:TOPOLOGY 2,0,0", "CAL:TOPOLOGY:PROBE 1,10",
                     "SYST:TDMA:FLIGHT:MODE 1",
                     f"CONF:SEQ:LINK LOOPBACK,2,3,{ready_input},{gateway_output},"
                     f"{pulse_us},{timeout_ms},{edge}",
                     "READ:SEQ:LINK?"]
    return commands + ["READ:SEQ:REPEAT?", "TRIG:SEQ:NEXT?", "READ:IO:STAT?"]


def build_start_commands(mode: str) -> list[str]:
    if mode == MODE_RJ45:
        return ["SYST:TDMA:RING:STOP", "SYST:TDMA:RING:ARM", "SYST:TDMA:RING:TRAIN 4096",
                "SYST:TDMA:RING:START", "TRIG:START"]
    return ["TRIG:START"]


def format_link_status(response: str, plan_count: int) -> str:
    values = [int(field) for field in next(csv.reader([response], strict=True))]
    if len(values) != 23 or any(value < 0 or value > 0xffffffff for value in values):
        raise ValueError("RJ45 网关状态字段不匹配")
    enabled, phase, error = values[:3]
    target = "持续" if values[21] == 0 else str(values[21])
    rounds = values[12] // plan_count if plan_count > 0 else 0
    return (f"RJ45 物理回环：{LINK_PHASES.get(phase, f'阶段 {phase}')} · 启用={enabled} · 错误={error} · "
            f"测量触发 {values[11]} / READY {values[12]} / 切换完成 {values[13]} · "
            f"轮次 {rounds}/{target} · TDMA 发送 {values[8]} / 接收 {values[9]} / 拒绝 {values[10]} · "
            f"交换 {values[22]}")


def execute_command_batch(commands, exchange, emit, *, monotonic=time.monotonic, sleep=time.sleep):
    """Verify asynchronous boundaries before issuing dependent configuration."""
    def query(command):
        response = exchange(command)
        emit(command, response)
        if response == "<timeout>":
            raise RuntimeError(f"{command} 超时")
        return response

    def wait_until(command, ready):
        deadline = monotonic() + 5.0
        while True:
            if ready(query(command)):
                return
            if monotonic() >= deadline:
                raise RuntimeError(f"等待设备状态超时：{command}")
            sleep(.05)

    for command in commands:
        header = command.split(maxsplit=1)[0].upper()
        if header == "TRIG:SEQ:NEXT":
            fields = [int(field) for field in next(
                csv.reader([query("READ:SEQ:LINK?")], strict=True))]
            if len(fields) != 23:
                raise RuntimeError("RJ45 状态字段不匹配，未发送 NEXT")
            enabled, phase, error, ready_input = fields[0], fields[1], fields[2], fields[16]
            if enabled and ready_input != 0:
                raise RuntimeError("RJ45 READY 来源不是 MANUAL，未发送 NEXT")
            if enabled and (phase != 3 or error != 0):
                raise RuntimeError(
                    f"RJ45 尚不能接受 NEXT：phase={phase}, error={error}")
        response = exchange(command)
        emit(command, response)
        if response == "<timeout>" and header not in RING_ACK_ONLY:
            raise RuntimeError(f"{command} 超时，已停止后续命令")
        if "?" in header:
            continue
        error = next(csv.reader([query("SYST:ERR?")], strict=True))
        if not error or error[0] != "0":
            raise RuntimeError(f"{command} 被设备拒绝：{','.join(error)}")
        if header.startswith(("CONF:SEQ", "CONF:TRIG", "TRIG:")):
            if header == "CONF:SEQ:NODE:ROLE":
                if next(csv.reader([response])) != ["STAGED"]:
                    raise RuntimeError(f"角色配置未暂存：{response}")
            elif header == "CONF:SEQ:NODE:ACT":
                if next(csv.reader([response]))[0] != "ACTIVE":
                    raise RuntimeError(f"角色配置未激活：{response}")
            elif response != "1":
                raise RuntimeError(f"{command} 未确认执行：{response}")
        if header == "TRIG:STOP":
            wait_until("TRIG:SEQ:NEXT?", lambda value: next(csv.reader([value]))[0] == "IDLE")
        elif header in RING_ACK_ONLY:
            def ready(value):
                fields = [int(field) for field in next(csv.reader([value], strict=True))]
                if len(fields) != len(RUNTIME_FIELDS):
                    raise ValueError("TDMA 运行状态字段不匹配")
                row = dict(zip(RUNTIME_FIELDS, fields, strict=True))
                if header == "SYST:TDMA:RING:STOP":
                    return (row["ring_enabled"] == row["ring_adapter_started"] == 0 and
                            row["ring_config_seq"] == row["ring_applied_config_seq"])
                armed = row["ring_enabled"] == row["ring_adapter_started"] == 1
                return armed and (header.endswith(":ARM") or row["ring_up_running"] == 1)
            wait_until(RING_STATUS_QUERY, ready)
        elif header == "SYST:TDMA:RING:TRAIN":
            sleep(.2)


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
    mode = {"无": "NONE", "电平": "LEVEL", "脉冲": "PULSE"}.get(
        status_mode, status_mode.upper())
    if mode not in {"NONE", "LEVEL", "PULSE"}:
        raise ValueError("状态输出形式必须是无、电平或脉冲")
    if mode == "NONE" and status_output_mask != 0:
        raise ValueError("无状态输出模式的状态掩码必须为 0")
    if mode != "NONE" and not status_output_mask:
        raise ValueError("电平或脉冲模式必须分配至少一个状态输出 OUT")
    if (not sequence_output_mask or
            (sequence_output_mask | status_output_mask) & ~15 or
            sequence_output_mask & status_output_mask):
        raise ValueError("序列编码必须占至少一个 OUT，且与状态输出互不重叠、均在 OUT1–OUT4 范围内")
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
                 "TRIG:SEQ:NEXT?", "READ:IO:STAT?"]
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
        self.geometry("1180x860")
        self.minsize(1120, 820)
        super().configure(bg="#f3f4f6")
        self._configure_style()
        self.port = tk.StringVar()
        self.backend = tk.StringVar(value="Serial")
        self.run_mode = tk.StringVar(value=MODE_INDEPENDENT)
        self.ready_input = tk.StringVar(value="MANUAL")
        self.ready_timeout = tk.StringVar(value="5000")
        self.repeat_count = tk.StringVar(value="1")
        self.link_status = tk.StringVar(value="独立模式：输入脉冲 → 编码切换 → 状态反馈")
        self.repeat_status = tk.StringVar(value="循环次数：1；0 表示持续运行")
        self._configured_mode = None
        self._configured_plan_count = 0
        self._configuration_generation = 0
        self._configured_resource = None
        self._device_configurations = {}
        self._device_fact_generation_floor = {}
        self._device_mode = None
        self._device_plan_count = 0
        self.source = tk.StringVar(value="IN1")
        self.edge = tk.StringVar(value="RIS")
        self.settle = tk.StringVar(value="10")
        self.pulse = tk.StringVar(value="10")
        self.status_mode = tk.StringVar(value="脉冲")
        self.output_hint = tk.StringVar()
        self.out_enabled = [tk.BooleanVar(value=True) for _ in range(4)]
        self.out_roles = [tk.StringVar(value=ROLE_SEQUENCE) for _ in range(3)] + [
            tk.StringVar(value=ROLE_STATUS)]
        self.plan = tk.StringVar(value="SP8T")
        self.codes = tk.StringVar(value="0,1,2,3,4,5,6,7")
        self.gateway_plan = tk.StringVar(value="SP8T")
        self.gateway_codes = tk.StringVar(value="0,1,2,3,4,5,6,7")
        self.gateway_settle = tk.StringVar(value="10")
        self.gateway_repeat_count = tk.StringVar(value="1")
        self.gateway_edge = tk.StringVar(value="RIS")
        self.gateway_pulse = tk.StringVar(value="10")
        self.gateway_out_enabled = [tk.BooleanVar(value=True) for _ in range(4)]
        self.gateway_out_roles = [tk.StringVar(value=ROLE_SEQUENCE) for _ in range(3)] + [
            tk.StringVar(value=ROLE_GATEWAY)]
        self.gateway_ready_input = self.ready_input
        self.gateway_timeout = self.ready_timeout
        self.status = tk.StringVar(value="未连接")
        self.sequence_state = tk.StringVar(value="UNKNOWN")
        self.mode_hint = tk.StringVar(value="外部脉冲模式：启动后等待输入脉冲")
        self.io_inputs = tk.StringVar(value="输入：—")
        self.io_outputs = tk.StringVar(value="输出：—")
        self.io_owned = tk.StringVar(value="占用：—")
        self.io_state = tk.StringVar(value="IO状态：—")
        self.switch_position = tk.StringVar(value="当前开关：未运行")
        self.independent_switch = tk.StringVar(value="1")
        self.independent_switch_status = tk.StringVar(value="独立 SP8T：未读取")
        self.ota_file = tk.StringVar()
        self.ota_status = tk.StringVar(value="未选择固件")
        self.manual_command = tk.StringVar()
        self.auto_scroll = tk.BooleanVar(value=True)
        self.input_lamps: list[tk.Label] = []
        self.output_lamps: list[tk.Label] = []
        self.port_box: ttk.Combobox | None = None
        self.next_button: ttk.Button | None = None
        self.pulse_entry: ttk.Entry | None = None
        self.out_checkbuttons: list[ttk.Checkbutton] = []
        self.out_role_boxes: list[ttk.Combobox] = []
        self.gateway_out_checkbuttons: list[ttk.Checkbutton] = []
        self.gateway_out_role_boxes: list[ttk.Combobox] = []
        self.status_mode_box: ttk.Combobox | None = None
        self.source_box: ttk.Combobox | None = None
        self.ota_progress: ttk.Progressbar | None = None
        self._ota_running = False
        self._transport_switching = False
        self._operations: queue.Queue[tuple[str, object] | None] = queue.Queue()
        self._ui_events: queue.Queue[tuple[str, tuple]] = queue.Queue()
        self._closing = False
        self._build()
        self._watch_configuration_changes()
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
        self.connection_panel = ttk.Frame(self, style="Panel.TFrame", padding=(12, 10))
        self.connection_panel.pack(fill="x", padx=12, pady=(10, 6))
        ttk.Label(self.connection_panel, text="设备连接", style="Section.TLabel").pack(side="left", padx=(0, 14))
        backend_box = ttk.Combobox(self.connection_panel, textvariable=self.backend,
            values=["Serial", "USB TMC"], state="readonly", width=10)
        backend_box.pack(side="left", padx=(0, 8))
        backend_box.bind("<<ComboboxSelected>>", lambda _event: self.refresh_ports())
        self.port_box = ttk.Combobox(self.connection_panel, textvariable=self.port, width=44)
        self.port_box.pack(side="left", fill="x", expand=True, padx=(0, 8))
        ttk.Button(self.connection_panel, text="扫描设备", command=self.refresh_ports).pack(side="left")
        ttk.Label(self.connection_panel, textvariable=self.status, style="Status.TLabel",
                  width=24, anchor="e").pack(side="right", padx=(14, 0))

        self.main_panes = ttk.Panedwindow(self, orient="vertical")
        self.main_panes.pack(fill="both", expand=True, padx=12, pady=(0, 10))
        main_panel = ttk.Frame(self.main_panes, style="App.TFrame")
        log_panel = ttk.Frame(self.main_panes, style="App.TFrame")
        self.main_panes.add(main_panel, weight=0)
        self.main_panes.add(log_panel, weight=1)

        self.mode_notebook = ttk.Notebook(main_panel)
        self.mode_notebook.pack(fill="x", pady=(0, 6))
        self.independent_page = ttk.Frame(self.mode_notebook, style="Panel.TFrame", padding=10)
        self.loopback_page = ttk.Frame(self.mode_notebook, style="Panel.TFrame", padding=10)
        self.manual_switch_page = ttk.Frame(self.mode_notebook, style="Panel.TFrame", padding=12)
        self.maintenance_page = ttk.Frame(self.mode_notebook, style="App.TFrame", padding=8)
        for page, title in ((self.independent_page, "独立 SP8T 序列"),
                            (self.loopback_page, "RJ45 物理回环"),
                            (self.manual_switch_page, "手动 SP8T"),
                            (self.maintenance_page, "设备维护")):
            self.mode_notebook.add(page, text=title)
        self._build_sequence_page(self.independent_page, MODE_INDEPENDENT)
        self._build_sequence_page(self.loopback_page, MODE_RJ45)
        self._build_manual_switch(self.manual_switch_page)
        self._build_maintenance(self.maintenance_page)
        self.mode_notebook.bind("<<NotebookTabChanged>>", self._on_mode_tab_changed)

        self.io_panel = ttk.LabelFrame(main_panel, text="公共 IO 读回", padding=(12, 8))
        self.io_panel.pack(fill="x")
        headline = ttk.Frame(self.io_panel)
        headline.pack(fill="x")
        ttk.Label(headline, textvariable=self.switch_position, style="Position.TLabel").pack(side="left")
        ttk.Button(headline, text="刷新状态", command=lambda: self.command("TRIG:SEQ:NEXT?")).pack(side="right")
        levels = ttk.Frame(self.io_panel)
        levels.pack(fill="x", pady=(6, 0))
        for heading, prefix, lamps in (("输入", "IN", self.input_lamps), ("输出", "OUT", self.output_lamps)):
            bank = ttk.Frame(levels)
            bank.pack(side="left", padx=(0, 16))
            ttk.Label(bank, text=heading).pack(side="left", padx=(0, 5))
            for index in range(4):
                lamp = tk.Label(bank, text=f"{prefix}{index + 1}\n低", width=6, height=2,
                    bg="#e5e7eb", fg="#374151", relief="flat", highlightthickness=1,
                    highlightbackground="#d1d5db")
                lamp.pack(side="left", padx=(0, 4))
                lamps.append(lamp)
        metrics = ttk.Frame(levels)
        metrics.pack(side="left", fill="x", expand=True)
        for variable in (self.io_owned, self.io_state):
            ttk.Label(metrics, textvariable=variable).pack(anchor="w")
        ttk.Label(self.io_panel, textvariable=self.repeat_status).pack(anchor="w", pady=(5, 0))

        log_bar = ttk.Frame(log_panel)
        log_bar.pack(fill="x", pady=(5, 3))
        ttk.Label(log_bar, text="命令日志").pack(side="left")
        ttk.Checkbutton(log_bar, text="自动滚动", variable=self.auto_scroll).pack(side="right")
        ttk.Button(log_bar, text="导出日志", command=self.export_log).pack(side="right", padx=6)
        ttk.Button(log_bar, text="清空", command=self.clear_log).pack(side="right")
        self.output = ScrolledText(log_panel, height=10, state="disabled", font=("Consolas", 10),
            bg="#ffffff", fg="#1f2937", insertbackground="#111827", selectbackground="#99f6e4",
            selectforeground="#134e4a", relief="flat", borderwidth=0, highlightthickness=1,
            highlightbackground="#d1d5db", highlightcolor="#0f766e", padx=10, pady=8)
        self.output.pack(fill="both", expand=True)
        for tag, color in (("TIME", "#6b7280"), ("CMD", "#0369a1"), ("OK", "#15803d"),
                           ("INFO", "#374151"), ("WARN", "#a16207"), ("ERROR", "#b91c1c"), ("OTA", "#7c3aed")):
            self.output.tag_configure(tag, foreground=color)
        self._update_status_mode()
        self.update_mode_hint()

    @staticmethod
    def _field(parent, column, label, variable, *, values=None, width=12):
        ttk.Label(parent, text=label).grid(row=0, column=column, sticky="w", padx=(0, 10), pady=(0, 4))
        widget = (ttk.Combobox(parent, textvariable=variable, values=values, state="readonly", width=width)
                  if values is not None else ttk.Entry(parent, textvariable=variable, width=width))
        widget.grid(row=1, column=column, sticky="ew", padx=(0, 10))
        parent.columnconfigure(column, weight=1, uniform="fields")
        return widget

    def _build_output_assignment(self, parent, enabled_vars, role_vars, role_values,
                                 checkbuttons, role_boxes, on_select=None):
        for index in range(4):
            cell = ttk.Frame(parent)
            cell.pack(side="left", padx=(0, 8 if index < 3 else 0))
            check = ttk.Checkbutton(
                cell, text=f"OUT{index + 1}", variable=enabled_vars[index])
            check.pack(side="left")
            checkbuttons.append(check)
            box = ttk.Combobox(
                cell, textvariable=role_vars[index], values=role_values,
                state="readonly", width=7)
            box.pack(side="left", padx=(4, 0))
            if on_select is not None:
                box.bind("<<ComboboxSelected>>", on_select)
            role_boxes.append(box)

    def _build_sequence_page(self, page, mode):
        combined = mode == MODE_RJ45
        plan, codes, settle, repeat = ((self.gateway_plan, self.gateway_codes, self.gateway_settle,
            self.gateway_repeat_count) if combined else (self.plan, self.codes, self.settle, self.repeat_count))
        plan_group = ttk.LabelFrame(page, text="序列配置", padding=10)
        plan_group.pack(fill="x", pady=(0, 8))
        self._field(plan_group, 0, "计划", plan, width=10)
        self._field(plan_group, 1, "编码（首项为启动状态）", codes, width=22)
        self._field(plan_group, 2, "建立时间 µs", settle, width=10)
        self._field(plan_group, 3, "循环次数（0=持续）", repeat, width=10)

        if combined:
            self.gateway_output_group = ttk.LabelFrame(page, text="OUT 属性", padding=8)
            self.gateway_output_group.pack(fill="x", pady=(0, 8))
            self._build_output_assignment(
                self.gateway_output_group, self.gateway_out_enabled,
                self.gateway_out_roles, [ROLE_SEQUENCE, ROLE_GATEWAY],
                self.gateway_out_checkbuttons, self.gateway_out_role_boxes,
                lambda _event: self.update_mode_hint())
            self.gateway_group = ttk.LabelFrame(page, text="VNA 网关", padding=10)
            self.gateway_group.pack(fill="x", pady=(0, 8))
            self.gateway_ready_box = self._field(
                self.gateway_group, 0, "READY 输入", self.gateway_ready_input,
                values=["MANUAL", "IN1", "IN2", "IN3", "IN4"], width=9)
            self.gateway_ready_box.bind(
                "<<ComboboxSelected>>", lambda _event: self.update_mode_hint())
            self._field(self.gateway_group, 1, "READY 边沿", self.gateway_edge, values=["RIS", "FALL"], width=9)
            self._field(self.gateway_group, 2, "触发脉宽 µs", self.gateway_pulse, width=12)
            self._field(self.gateway_group, 3, "READY 超时 ms", self.gateway_timeout, width=12)
            ttk.Label(page, text="流程：首编码 → RJ45 TDMA → VNA 触发 → READY → RJ45 TDMA → 下一编码。").pack(anchor="w", pady=(0, 5))
        else:
            self.independent_output_group = ttk.LabelFrame(page, text="OUT 属性", padding=8)
            self.independent_output_group.pack(fill="x", pady=(0, 8))
            self._build_output_assignment(
                self.independent_output_group, self.out_enabled, self.out_roles,
                [ROLE_SEQUENCE, ROLE_STATUS], self.out_checkbuttons,
                self.out_role_boxes, lambda _event: self._update_status_mode())

            self.independent_input_group = ttk.LabelFrame(page, text="推进事件", padding=10)
            self.independent_input_group.pack(fill="x", pady=(0, 8))
            self.source_box = self._field(self.independent_input_group, 0, "输入", self.source,
                values=["MANUAL", "IN1", "IN2", "IN3", "IN4"], width=8)
            self.source_box.bind("<<ComboboxSelected>>", lambda _event: self.update_mode_hint())
            self._field(self.independent_input_group, 1, "边沿", self.edge, values=["RIS", "FALL"], width=7)
            self.status_mode_box = self._field(
                self.independent_input_group, 2, "状态形式", self.status_mode,
                values=["无", "电平", "脉冲"], width=7)
            self.status_mode_box.bind(
                "<<ComboboxSelected>>", lambda _event: self._update_status_mode())
            self.pulse_entry = self._field(
                self.independent_input_group, 3, "状态脉宽 µs", self.pulse, width=9)

        controls = ttk.Frame(page, style="Panel.TFrame")
        controls.pack(fill="x", pady=(2, 6))
        ttk.Button(controls, text="配置此模式", style="Primary.TButton",
            command=lambda m=mode: self.configure_mode(m)).pack(side="left", padx=(0, 8))
        actions = [("启动", "TRIG:START"), ("下一步", "TRIG:SEQ:NEXT")]
        actions += [("暂停", "TRIG:PAUS"), ("继续", "TRIG:CONT"), ("停止", "TRIG:STOP")]
        for label, command in actions:
            button = ttk.Button(controls, text=label,
                style="Danger.TButton" if command == "TRIG:STOP" else "TButton",
                command=lambda c=command, m=mode: self.command_mode(m, c))
            button.pack(side="left", padx=(0, 6))
            if command == "TRIG:SEQ:NEXT":
                self.next_button = button
        if combined:
            ttk.Label(page, textvariable=self.link_status, wraplength=1040).pack(anchor="w")
        else:
            ttk.Label(page, textvariable=self.mode_hint).pack(anchor="w")
            ttk.Label(page, textvariable=self.output_hint).pack(anchor="w", pady=(3, 0))

    def _build_manual_switch(self, page):
        group = ttk.LabelFrame(page, text="独立开关控制", padding=16)
        group.pack(fill="x")
        ttk.Label(group, text="直接控制 OUT1–OUT3 的 SP8T 位置。请先停止序列并释放输出。").pack(anchor="w", pady=(0, 12))
        row = ttk.Frame(group)
        row.pack(fill="x")
        ttk.Label(row, text="位置").pack(side="left")
        self.manual_position_box = ttk.Combobox(row, textvariable=self.independent_switch,
            values=[str(i) for i in range(1, 9)], state="readonly", width=7)
        self.manual_position_box.pack(side="left", padx=8)
        ttk.Button(row, text="切换", command=self.set_independent_switch).pack(side="left", padx=(0, 6))
        ttk.Button(row, text="读取", command=self.read_independent_switch).pack(side="left")
        ttk.Label(group, textvariable=self.independent_switch_status).pack(anchor="w", pady=(12, 0))

    def _on_mode_tab_changed(self, _event=None):
        selected = self.mode_notebook.select()
        mode = {str(self.independent_page): MODE_INDEPENDENT, str(self.loopback_page): MODE_RJ45}.get(selected)
        if mode is not None and mode != self.run_mode.get():
            self.run_mode.set(mode)
            self._update_run_mode()

    def configure_mode(self, mode):
        if mode != self.run_mode.get():
            self.run_mode.set(mode)
            self._update_run_mode()
        self.configure()

    def command_mode(self, mode, command):
        if mode != self.run_mode.get():
            self.run_mode.set(mode)
            self._update_run_mode()
        self.command(command)

    def _resource_key(self):
        return self.backend.get(), self.port.get().strip()

    def _watch_configuration_changes(self):
        independent = [self.plan, self.codes, self.settle, self.repeat_count, self.source,
                       self.edge, self.pulse, self.status_mode, *self.out_enabled, *self.out_roles]
        gateway = [self.gateway_plan, self.gateway_codes, self.gateway_settle, self.gateway_repeat_count,
                   self.gateway_ready_input, self.gateway_edge, self.gateway_pulse,
                   self.gateway_timeout, *self.gateway_out_enabled, *self.gateway_out_roles]
        for mode, variables in ((MODE_INDEPENDENT, independent), (MODE_RJ45, gateway)):
            for variable in variables:
                variable.trace_add("write", lambda *_args, m=mode: self._draft_changed(m))
        for variable in (self.port, self.backend):
            variable.trace_add("write", self._connection_changed)

    def _draft_changed(self, mode):
        if mode == self.run_mode.get():
            self._configured_mode = None
            self._configuration_generation += 1
            self.status.set("参数已修改，请配置此模式")

    def _connection_changed(self, *_args):
        self._configured_mode = None
        self._configured_resource = None
        self._configuration_generation += 1
        self._device_mode, self._device_plan_count = self._device_configurations.get(self._resource_key(), (None, 0))
        self._clear_device_readback()
        self.status.set("资源已改变，请配置此模式")

    def _clear_device_readback(self):
        self.sequence_state.set("UNKNOWN")
        self.switch_position.set("当前开关：未读取")
        self.independent_switch_status.set("独立 SP8T：未读取")
        self.io_inputs.set("输入：未读取")
        self.io_outputs.set("输出：未读取")
        self.io_owned.set("占用：未读取")
        self.io_state.set("IO 状态：未读取")
        self.repeat_status.set("循环次数：未读取")
        self.link_status.set("RJ45 状态：未读取")
        for prefix, lamps in (("IN", self.input_lamps), ("OUT", self.output_lamps)):
            for index, lamp in enumerate(lamps):
                lamp.configure(text=f"{prefix}{index + 1}\n未知", bg="#e5e7eb")

    def _invalidate_device_configuration(self):
        self._configured_mode = None
        self._configured_resource = None
        self._configured_plan_count = 0
        self._configuration_generation += 1
        resource_key = self._resource_key()
        # A queued configuration predating a reset must not restore device
        # facts. Draft edits alone intentionally retain completed device facts.
        self._device_fact_generation_floor[resource_key] = self._configuration_generation
        self._device_configurations[resource_key] = (None, 0)
        self._device_mode, self._device_plan_count = None, 0
        self._clear_device_readback()
        self.status.set("设备配置未知，请重新配置此模式")

    def _configuration_finished(self, mode, count, generation, resource_key):
        # Device facts outlive a draft edit or a tab switch. An old resource's
        # completed transaction cannot authorize START on another connection.
        if generation < self._device_fact_generation_floor.get(resource_key, 0):
            return
        self._device_configurations[resource_key] = (mode, count)
        if resource_key != self._resource_key():
            return
        self._device_mode, self._device_plan_count = mode, count
        if generation == self._configuration_generation:
            self._configured_mode, self._configured_plan_count = mode, count
            self._configured_resource = resource_key if mode is not None else None
            self.status.set("配置完成，可以启动" if mode else "配置失败，请检查日志")
        else:
            self.status.set("设备配置已结束；当前草稿需重新配置")

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

    def set_independent_switch(self) -> None:
        if self.sequence_state.get() not in {"UNKNOWN", "IDLE"}:
            self.log(f"独立 SP8T 只能在序列未运行且资源空闲时切换（当前状态：{self.sequence_state.get()}）。", "WARN")
            return
        value = self.independent_switch.get()
        self.enqueue_commands([f"CONF:SWITCH1 {value}", "READ:SWITCH1?", "READ:IO:STAT?"])

    def read_independent_switch(self) -> None:
        self.enqueue_commands(["READ:SWITCH1?", "READ:IO:STAT?"])

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

    def enqueue_commands(self, commands: list[str]) -> bool:
        if self._ota_running or self._transport_switching:
            owner = "OTA" if self._ota_running else "USB 模式切换"
            self.log(f"{owner} 正在独占通信资源，当前命令未加入队列。", "WARN")
            return False
        self._operations.put((
            "commands",
            (self.backend.get(), self.port.get().strip(), list(commands))))
        return True

    def _operation_loop(self) -> None:
        while True:
            operation = self._operations.get()
            if operation is None:
                return
            kind, payload = operation
            if kind == "commands":
                backend, resource, commands = payload
                self.run_commands(backend, resource, commands)
            elif kind == "configuration":
                backend, resource, commands, mode, count, generation = payload
                passed = self.run_commands(backend, resource, commands)
                self._ui_events.put(("configured", (mode if passed else None, count if passed else 0,
                    generation, (backend, resource))))
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
            elif kind == "configured":
                self._configuration_finished(*args)
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
        if self._ota_running or self._transport_switching:
            self.log("设备维护正在占用通信资源，请稍后切换 USB 模式。", "WARN")
            return
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
        self._invalidate_device_configuration()
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
        combined = self.run_mode.get() == MODE_RJ45
        can_step = (combined and self.gateway_ready_input.get() == "MANUAL") or (
            not combined and self.source.get() == "MANUAL")
        if self.next_button is not None:
            self.next_button.state(["!disabled"] if can_step else ["disabled"])
        if combined:
            ready = ("SCPI NEXT" if self.gateway_ready_input.get() == "MANUAL" else
                     f"{self.gateway_ready_input.get()} READY")
            try:
                _, gateway_output = self._gateway_output_assignment()
            except ValueError:
                gateway_output = "所选 OUT"
            self.mode_hint.set(
                f"RJ45 物理回环：启动首编码 → TDMA → {gateway_output} 触发 → "
                f"{ready} → TDMA → 下一编码；有限次数完成后停止。")
            return
        if self.source.get() == "MANUAL":
            self.mode_hint.set("MANUAL 软件触发模式：使用“下一步”推进")
        else:
            self.mode_hint.set(f"启动先输出首项编码；随后 {self.source.get()} 每个 {self.edge.get()} 沿推进一步。")

    def _update_run_mode(self) -> None:
        self._configured_mode = None
        self._configuration_generation += 1
        self.status.set("页面已切换；启动前请配置此模式")
        self.update_mode_hint()

    def _update_status_mode(self) -> None:
        mode = self.status_mode.get()
        if self.pulse_entry is not None:
            self.pulse_entry.state(
                ["!disabled"] if mode == "脉冲" else ["disabled"])
        for enabled, role, widget in zip(
                self.out_enabled, self.out_roles, self.out_checkbuttons):
            disabled = mode == "无" and role.get() == ROLE_STATUS
            if disabled:
                enabled.set(False)
            widget.state(["disabled"] if disabled else ["!disabled"])
        self.output_hint.set({
            "无": "DUT 仅输出编码电平；不占用状态 OUT，不输出完成脉冲。",
            "电平": "兼容状态电平输出；请显式勾选状态 OUT。",
            "脉冲": "兼容状态脉冲输出；请显式勾选状态 OUT，此模式不配置 VNA 网关。",
        }[mode])

    @staticmethod
    def _role_masks(enabled_vars, role_vars, secondary_role: str) -> tuple[int, int]:
        sequence_mask = 0
        secondary_mask = 0
        for index, (enabled, role) in enumerate(
                zip(enabled_vars, role_vars)):
            if not enabled.get():
                continue
            if role.get() == ROLE_SEQUENCE:
                sequence_mask |= 1 << index
            elif role.get() == secondary_role:
                secondary_mask |= 1 << index
            else:
                raise ValueError(f"OUT{index + 1} 角色无效")
        return sequence_mask, secondary_mask

    def _output_role_masks(self) -> tuple[int, int]:
        return SequenceUi._role_masks(
            self.out_enabled, self.out_roles, ROLE_STATUS)

    def _gateway_output_assignment(self) -> tuple[int, str]:
        sequence_mask, gateway_mask = SequenceUi._role_masks(
            self.gateway_out_enabled, self.gateway_out_roles, ROLE_GATEWAY)
        if gateway_mask == 0 or gateway_mask & (gateway_mask - 1):
            raise ValueError("RJ45 模式必须启用且仅启用一路 VNA 触发 OUT")
        return sequence_mask, f"OUT{gateway_mask.bit_length()}"

    def run_commands(self, backend: str, resource: str,
                     commands: list[str]) -> bool:
        def emit(command, response):
            self._ui_events.put(("exchange", (command, response, (backend, resource))))
        try:
            if backend == "USB TMC":
                try:
                    import pyvisa
                except ImportError as exc:
                    raise RuntimeError("USB TMC 需要安装 pyvisa（例如: uv pip install pyvisa）") from exc
                rm = pyvisa.ResourceManager()
                instrument = rm.open_resource(resource)
                instrument.timeout = 2000
                instrument.write_termination = "\n"
                instrument.read_termination = "\n"
                try:
                    def exchange(command):
                        header = command.split(maxsplit=1)[0].upper()
                        if header in {"*CLS", "*RST"}:
                            instrument.write(command)
                            return "已发送，等待读回"
                        # Sequence setters return a numeric result even though
                        # their SCPI headers do not contain a question mark.
                        try:
                            return instrument.query(command).strip()
                        except pyvisa.errors.VisaIOError as exc:
                            if header in RING_ACK_ONLY and exc.error_code == pyvisa.constants.StatusCode.error_timeout:
                                return "<timeout>"
                            raise
                    execute_command_batch(commands, exchange, emit)
                finally:
                    instrument.close()
                    rm.close()
            else:
                with open_serial_port(resource, 115200, 2, .2,
                                      read_timeout_s=.2) as ser:
                    execute_command_batch(commands, lambda command: send_command(ser, command, 2), emit)
            return True
        except Exception as exc:
            self._ui_events.put(("log", (f"命令执行失败：{exc}", "ERROR")))
            self._ui_events.put(("status", ("执行失败，请检查日志",)))
            return False

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

    def log_exchange(self, command: str, response: str, resource_key=None) -> None:
        self.log(f"> {command}", "CMD")
        level = "ERROR" if response == "<timeout>" else "OK"
        self.log(f"< {response}", level)
        if resource_key is not None and resource_key != self._resource_key():
            return
        summary = response.replace("\n", " ")
        self.status.set(summary if len(summary) <= 24 else summary[:21] + "…")
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
        if self._ota_running or self._transport_switching:
            self.log("设备维护正在占用通信资源，请稍后升级。", "WARN")
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
        self._invalidate_device_configuration()
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
        if command and self.enqueue_commands([command]):
            # Inspect every header, rather than a '?' anywhere in parameters.
            # Conservatively invalidate on a nonquery in a compound message.
            headers = [part.strip().split()[0] for part in re.split(r"[;\r\n]", command)
                       if part.strip()]
            if any(not header.endswith("?") for header in headers):
                self._invalidate_device_configuration()

    def update_io(self, command: str, response: str) -> None:
        if command.upper().startswith("READ:SEQ:LINK?"):
            try:
                self.link_status.set(format_link_status(response, self._device_plan_count))
            except (ValueError, csv.Error):
                self.link_status.set("RJ45 状态解析失败，请检查固件版本和命令日志")
            return
        if command.upper().startswith("READ:SEQ:REPEAT?"):
            try:
                configured, run, finished = map(int, response.split(","))
                self.repeat_status.set(f"循环次数：{'持续' if configured == 0 else configured} · "
                    f"本次：{'持续' if run == 0 else run} · {'已完成并停止' if finished else '未完成'}")
            except ValueError:
                self.repeat_status.set("循环次数读回失败")
            return
        if command.upper().startswith("READ:SWITCH1?"):
            try:
                fields = [int(value) for value in response.split(",")]
                self.independent_switch.set(str(fields[1]))
                self.independent_switch_status.set(f"独立 SP8T：第 {fields[1]} 位")
            except (ValueError, IndexError):
                self.independent_switch_status.set("独立 SP8T：读回失败")
            return
        if command.upper().startswith("TRIG:SEQ:NEXT?"):
            try:
                self.sequence_state.set(next(csv.reader([response]))[0].strip('"'))
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
        combined = self.run_mode.get() == MODE_RJ45
        device_combined = self._device_mode == MODE_RJ45
        if command == "TRIG:SEQ:NEXT" and (((combined or device_combined) and
                self.gateway_ready_input.get() != "MANUAL") or
                (not combined and not device_combined and self.source.get() != "MANUAL")):
            self.log("当前模式由外部输入推进，SCPI NEXT 已禁用。")
            return
        if command == "TRIG:START" and (self._configured_mode != self.run_mode.get() or
                                        self._configured_resource != self._resource_key()):
            self.log("请先点击“配置此模式”，确认当前参数和通信资源配置成功后再启动。", "WARN")
            return
        commands = build_start_commands(self.run_mode.get()) if command == "TRIG:START" else [command]
        if command == "TRIG:STOP" and (device_combined or self._device_mode is None):
            commands.append("SYST:TDMA:RING:STOP")
        commands += ["READ:SEQ:REPEAT?", "READ:IO:STAT?"]
        if command != "TRIG:SEQ:NEXT?":
            commands.append("TRIG:SEQ:NEXT?")
        if device_combined or self._device_mode is None or (command == "TRIG:START" and combined):
            commands.append("READ:SEQ:LINK?")
        self.enqueue_commands(commands)

    def configure(self) -> None:
        try:
            if not self.port.get().strip():
                raise ValueError("请先扫描或输入通信资源")
            mode = self.run_mode.get()
            combined = mode == MODE_RJ45
            if combined:
                plan, codes_text = self.gateway_plan.get(), self.gateway_codes.get()
                source, edge = "MANUAL", self.gateway_edge.get()
                settle, pulse = int(self.gateway_settle.get()), int(self.gateway_pulse.get())
                repeat = int(self.gateway_repeat_count.get())
                ready, timeout = self.gateway_ready_input.get(), int(self.gateway_timeout.get())
                sequence_mask, gateway_output = self._gateway_output_assignment()
                status_mask, status_mode = 0, "NONE"
            else:
                plan, codes_text = self.plan.get(), self.codes.get()
                source, edge = self.source.get(), self.edge.get()
                settle = int(self.settle.get())
                status_mode = self.status_mode.get()
                pulse = int(self.pulse.get()) if status_mode == "脉冲" else 0
                repeat = int(self.repeat_count.get())
                sequence_mask, status_mask = self._output_role_masks()
                ready, timeout = "IN1", 5000
                gateway_output = "OUT4"
            codes = [int(x.strip()) for x in codes_text.split(",") if x.strip()]
            commands = build_mode_configuration(
                mode, plan, codes, source, edge, settle, pulse,
                sequence_mask, status_mask, status_mode, ready, timeout, repeat,
                gateway_output)
        except ValueError as exc:
            self.log(f"配置错误: {exc}")
            return
        self.log(
            f"{mode}：配置 {len(codes)} 个位置；循环次数 {repeat}（0=持续）；启动直接输出首项编码 "
            f"{codes[0]}；序列掩码 0x{sequence_mask:X}，"
            f"状态掩码 0x{status_mask:X}（{status_mode}）。")
        if combined:
            self.log(
                f"VNA 网关：{ready} {edge} READY，{gateway_output} 触发 "
                f"{pulse} µs，等待超时 {timeout} ms。")
        else:
            self.log(self.output_hint.get())
        if self._ota_running or self._transport_switching:
            self.log("设备维护正在占用通信资源，请稍后配置。", "WARN")
            return
        self._configured_mode = None
        self._configuration_generation += 1
        self._operations.put(("configuration", (self.backend.get(), self.port.get().strip(), commands,
            mode, len(codes), self._configuration_generation)))


if __name__ == "__main__":
    SequenceUi().mainloop()
