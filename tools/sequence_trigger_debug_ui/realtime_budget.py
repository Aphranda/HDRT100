"""Core1 schedule editor: applying changes RAM; only SAVE writes device Flash."""

from __future__ import annotations

import csv
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
import time
import tkinter as tk
from tkinter import ttk


PREFIX = "SYST:TDMA:SCHEDULE"
PHASES = ("TDMA", "VDC", "DPLL", "CALIBRATION", "SYNC_CAPTURE", "REFMEM",
          "MODEL", "SYNC_TRIGGER", "TRIGGER_MEASURE", "GUARD")


@dataclass(frozen=True)
class BudgetTable:
    cycle: int
    phases: tuple[tuple[int, int, int], ...]


@dataclass(frozen=True)
class BudgetSnapshot:
    clock_hz: int
    table: BudgetTable
    quarantined_mask: int = 0
    schedule_misses: int = 0


def _fields(response):
    return next(csv.reader([response.strip()], strict=True))


def validate_table(table):
    if not isinstance(table, BudgetTable) or len(table.phases) != len(PHASES):
        raise ValueError("预算阶段数量不匹配")
    if not isinstance(table.cycle, int) or not 0 < table.cycle <= 0xffffff:
        raise ValueError("总周期超出设备计数范围")
    previous = 0
    for name, (start, end, wcet) in zip(PHASES, table.phases, strict=True):
        if (any(not isinstance(value, int) for value in (start, end, wcet)) or
                start != previous or not start < end <= table.cycle or
                not 0 <= wcet <= end - start):
            raise ValueError(f"{name}：时窗必须连续且为正，WCET 不得超出时窗")
        previous = end
    if previous != table.cycle or table.phases[-1][2] != 0:
        raise ValueError("GUARD 必须结束于总周期，且 WCET 为 0")


def parse_snapshot(response):
    values = [int(value) for value in _fields(response)]
    if (len(values) != 8 + len(PHASES) * 11 or values[0] != 2 or
            values[1] <= 0 or values[3] != len(PHASES)):
        raise ValueError("设备调度快照格式不支持")
    table = BudgetTable(values[2], tuple(tuple(values[8 + i * 11:11 + i * 11])
                                       for i in range(len(PHASES))))
    validate_table(table)
    return BudgetSnapshot(values[1], table, values[5], values[7])


def microseconds(cycles, clock_hz):
    text = format(Decimal(cycles) * 1000000 / Decimal(clock_hz), "f")
    return text.rstrip("0").rstrip(".") if "." in text else text


def _cycles(value, clock_hz):
    try:
        number = Decimal(str(value))
        if not number.is_finite() or not 0 <= number <= 0xffffff * Decimal(1000000) / clock_hz:
            raise ValueError("预算必须是计数范围内的非负数")
        result = number * clock_hz / 1000000
        if result != result.to_integral_value():
            raise ValueError("预算必须对应整数个设备时钟周期")
        return int(result)
    except (InvalidOperation, OverflowError) as exc:
        raise ValueError("预算必须是有效数字") from exc


def table_from_microseconds(clock_hz, cycle_us, windows_us, wcets_us):
    """Keep every entered phase budget; only GUARD consumes the remainder."""
    if clock_hz <= 0 or len(windows_us) != len(PHASES) - 1 or len(wcets_us) != len(windows_us):
        raise ValueError("请先读取设备时钟和完整预算")
    cycle = _cycles(cycle_us, clock_hz)
    phases = []
    start = 0
    for window, wcet in zip(windows_us, wcets_us, strict=True):
        end = start + _cycles(window, clock_hz)
        phases.append((start, end, _cycles(wcet, clock_hz)))
        start = end
    phases.append((start, cycle, 0))
    table = BudgetTable(cycle, tuple(phases))
    validate_table(table)
    return table


def run_budget_operation(action, payload, exchange, emit, stop, *,
                         monotonic=time.monotonic, sleep=time.sleep):
    """Run in the serial worker. ``stop`` must stop trigger/ring and verify ACKs.

    Firmware capability 1,1 guarantees APPLY/CLEAR are volatile and SAVE is
    explicit. Never issue a setter before this preflight, including to old FW.
    """
    if action not in ("read", "validate", "apply", "save"):
        raise ValueError("未知预算操作")

    def query(command):
        response = exchange(command)
        emit(command, response)
        if not response or response == "<timeout>":
            raise RuntimeError(f"{command} 超时或无响应")
        return response

    def write(command):
        response = query(command)
        error = _fields(query("SYST:ERR?"))
        if not error or error[0] != "0":
            raise RuntimeError(f"{command} 被拒绝：{','.join(error)}")
        if _fields(response)[0] != "OK":
            raise RuntimeError(f"{command} 未确认执行：{response}")

    def settled():
        deadline = monotonic() + 5.0
        while True:
            fields = [int(value) for value in _fields(query("SYST:TDMA:PERIOD?"))]
            if len(fields) != 5:
                raise RuntimeError("周期状态字段不匹配")
            # STOP may cancel a legacy PERIOD request without advancing applied
            # generation. Completion is absence of pending/applying work; own
            # APPLY is additionally checked against the entire actual table.
            if fields[2] == 0 and fields[4] == 0:
                return
            if monotonic() >= deadline:
                raise RuntimeError("等待预算实际生效超时；请重新读取设备")
            sleep(.05)

    if action != "read":
        capabilities = _fields(query(PREFIX + ":CAPABILITIES?"))
        if capabilities != ["1", "1"]:
            raise RuntimeError("固件未确认临时应用/显式 Flash 保存能力，请更新固件")
        baseline = payload.get("baseline")
        table = payload.get("table")
        if not isinstance(baseline, BudgetSnapshot):
            raise ValueError("请先读取当前设备预算")
        validate_table(table)
        if action == "save" and table != baseline.table:
            raise ValueError("编辑内容尚未临时应用；请先应用并读回，再保存到 Flash")
        if action in ("apply", "save"):
            stop()
        settled()
        current = parse_snapshot(query(PREFIX + "?"))
        if current.clock_hz != baseline.clock_hz or current.table != baseline.table:
            raise RuntimeError("设备预算已变化，请重新读取后编辑")
        if action == "save":
            write(PREFIX + ":SAVE")
        else:
            write(PREFIX + ":BEGIN")
            for index, phase in enumerate(table.phases):
                write(PREFIX + f":PHASE {index}," + ",".join(map(str, phase)))
            write(PREFIX + f":CYCLE {table.cycle}")
            validation = _fields(query(PREFIX + ":VALIDATE?"))
            if len(validation) != 4 or validation[0] != "1" or int(validation[3]) != table.cycle:
                raise RuntimeError("设备预算校验拒绝：" + ",".join(validation))
            if action == "apply":
                write(PREFIX + ":APPLY")
                settled()
            else:
                write(PREFIX + ":DISCARD")
    settled()
    snapshot = parse_snapshot(query(PREFIX + "?"))
    if action in ("apply", "save") and (snapshot.table != table or snapshot.clock_hz != baseline.clock_hz):
        raise RuntimeError("实际预算与提交值不一致，请重新读取设备")
    status = query(PREFIX + ":STATUS?")
    status_fields = _fields(status)
    if len(status_fields) != 12 or status_fields[0] != "1":
        raise RuntimeError("预算保存状态格式不支持")
    if action == "save" and status_fields[5] != "1":
        raise RuntimeError("设备未确认当前预算已保存到 Flash")
    return {"action": action, "snapshot": snapshot, "status": status,
            "persisted": status_fields[5] == "1"}


class BudgetEditor(tk.Toplevel):
    """Tk view only; submit/action completion are owned by the main GUI worker."""

    def __init__(self, parent, submit):
        super().__init__(parent)
        self.title("TDMA / VDC 实时预算")
        self.transient(parent)
        self.resizable(False, False)
        self._submit = submit
        self._snapshot = None
        self._busy = False
        self._inputs = []
        self._buttons = []
        self.cycle_us = tk.StringVar()
        self.windows_us = [tk.StringVar() for _ in PHASES[:-1]]
        self.wcets_us = [tk.StringVar() for _ in PHASES[:-1]]
        self.guard_us = tk.StringVar(value="—")
        self.message = tk.StringVar(value="先读取当前设备预算。编辑和临时应用均不写 Flash。")
        self.draft_message = tk.StringVar()
        body = ttk.Frame(self, padding=12)
        body.grid(sticky="nsew")
        ttk.Label(body, text="Core1 软件周期与预算（µs），与链路 TDMA 挡位独立。\n"
                  "修改周期不会缩放其他阶段；GUARD 使用剩余时间。500 / 1000 µs 需完整预算通过校验。",
                  wraplength=630).grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 8))
        ttk.Label(body, text="总周期（µs）").grid(row=1, column=0, sticky="w")
        self._entry(body, self.cycle_us, 1, 1)
        ttk.Label(body, text="时窗（µs）").grid(row=2, column=1)
        ttk.Label(body, text="WCET 预算（µs）").grid(row=2, column=2)
        for i, name in enumerate(PHASES[:-1]):
            ttk.Label(body, text=name, font=("TkDefaultFont", 10, "bold") if i < 2 else None).grid(
                row=i + 3, column=0, sticky="w", padx=(0, 15))
            self._entry(body, self.windows_us[i], i + 3, 1)
            self._entry(body, self.wcets_us[i], i + 3, 2)
        ttk.Label(body, text="GUARD（剩余）").grid(row=12, column=0, sticky="w")
        ttk.Label(body, textvariable=self.guard_us).grid(row=12, column=1)
        ttk.Label(body, text="0").grid(row=12, column=2)
        actions = ttk.Frame(body)
        actions.grid(row=13, column=0, columnspan=3, sticky="w", pady=10)
        for action, label in (("read", "读取设备"), ("validate", "校验草稿"),
                              ("apply", "临时应用"), ("save", "保存到 Flash")):
            button = ttk.Button(actions, text=label, command=lambda a=action: self.request(a))
            button.pack(side="left", padx=(0, 6))
            self._buttons.append((action, button))
        ttk.Label(body, text="应用/保存会停止触发与 TDMA 环路。保存仅固化已应用预算；未保存的修改重启丢失。",
                  wraplength=630).grid(row=14, column=0, columnspan=3, sticky="w")
        ttk.Label(body, textvariable=self.message, wraplength=630).grid(
            row=15, column=0, columnspan=3, sticky="w", pady=(8, 0))
        ttk.Label(body, textvariable=self.draft_message).grid(
            row=16, column=0, columnspan=3, sticky="w")
        for variable in [self.cycle_us, *self.windows_us, *self.wcets_us]:
            variable.trace_add("write", self._edited)
        self._refresh_controls()

    def _entry(self, parent, variable, row, column):
        entry = ttk.Entry(parent, textvariable=variable, width=18)
        entry.grid(row=row, column=column, padx=5, pady=2)
        self._inputs.append(entry)

    def _table(self):
        if self._snapshot is None:
            raise ValueError("请先读取当前设备预算")
        return table_from_microseconds(self._snapshot.clock_hz, self.cycle_us.get(),
                                       [v.get() for v in self.windows_us],
                                       [v.get() for v in self.wcets_us])

    def _edited(self, *_):
        try:
            table = self._table()
            self.guard_us.set(microseconds(table.cycle - table.phases[-1][0], self._snapshot.clock_hz))
            self.draft_message.set("草稿尚未应用；保存到 Flash 前请先临时应用。"
                                   if table != self._snapshot.table else "")
        except ValueError:
            self.guard_us.set("预算超限或输入无效")
            self.draft_message.set("草稿无效，请检查总周期、各阶段时窗与 WCET。"
                                   if self._snapshot is not None else "")
        self._refresh_controls()

    def _refresh_controls(self):
        valid = False
        unchanged = False
        try:
            table = self._table()
            valid = True
            unchanged = table == self._snapshot.table
        except ValueError:
            pass
        for entry in self._inputs:
            entry.configure(state="disabled" if self._busy or self._snapshot is None else "normal")
        for action, button in self._buttons:
            enabled = not self._busy and (action == "read" or
                      (valid and (action != "save" or unchanged)))
            button.configure(state="normal" if enabled else "disabled")

    def request(self, action):
        if self._busy:
            return
        try:
            payload = {} if action == "read" else {"baseline": self._snapshot, "table": self._table()}
            if action == "save" and payload["table"] != self._snapshot.table:
                raise ValueError("请先临时应用编辑内容，再保存到 Flash")
            self._busy = True
            self._refresh_controls()
            self.message.set("正在处理；请等待设备读回…")
            self._submit(action, payload)
        except Exception as exc:
            self.receive_error(str(exc))

    def receive_result(self, action, result):
        self._busy = False
        self._snapshot = result["snapshot"]
        if action != "validate":
            self.cycle_us.set(microseconds(self._snapshot.table.cycle, self._snapshot.clock_hz))
            for i, (start, end, wcet) in enumerate(self._snapshot.table.phases[:-1]):
                self.windows_us[i].set(microseconds(end - start, self._snapshot.clock_hz))
                self.wcets_us[i].set(microseconds(wcet, self._snapshot.clock_hz))
        outcome = {"read": "已读取实际预算", "validate": "草稿校验通过，尚未应用",
                   "apply": "临时预算已生效", "save": "当前预算已保存到 Flash"}[action]
        saved = "当前实际预算已固化" if result["persisted"] else "当前实际预算未固化"
        self.message.set(f"{outcome}；{saved}。\n"
                         f"隔离位图=0x{self._snapshot.quarantined_mask:X}，"
                         f"调度错失={self._snapshot.schedule_misses}（非实时稳定性验收）。")
        self._edited()

    def receive_error(self, message):
        self.invalidate()
        self.message.set(f"操作未确认：{message}。请重新读取设备预算。")

    def invalidate(self):
        self._busy = False
        self._snapshot = None
        self.draft_message.set("")
        self.message.set("设备连接或状态已变化，请重新读取预算。")
        self._refresh_controls()
