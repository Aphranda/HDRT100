#!/usr/bin/env python3
"""Single-port, REFMEM unified-control-plane trigger debug demo.

Tabs are functional modules.  Every write can be issued either through the
REFMEM unified control plane (``SYSTem:CTRL:EXECute``, all boards addressed by
one target/required mask) or through the node-sequence application SCPI layer.
Queries and per-board ACK evidence use the same single coordinator port.
"""

from __future__ import annotations

from pathlib import Path
import hashlib
import struct
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, ttk
from tkinter.scrolledtext import ScrolledText
import zlib

try:
    from . import catalog, device, ota
    from .ota_workflow import OtaWorkflow
    from .ui import (
        ConsoleStore,
        DEFAULT_THEME,
        ErrorBanner,
        ErrorBoundary,
        LogView,
        PageHost,
        PageRouter,
        PumpSink,
        TkDialogService,
        TkScheduler,
        UiEvent,
        UiPump,
        WorkflowNotResumable,
        apply_tk_scaling,
        load_settings,
        save_settings,
    )
except ImportError:  # direct script execution
    import catalog  # type: ignore
    import device  # type: ignore
    import ota  # type: ignore
    from ota_workflow import OtaWorkflow  # type: ignore
    from ui import (  # type: ignore
        ConsoleStore,
        DEFAULT_THEME,
        ErrorBanner,
        ErrorBoundary,
        LogView,
        PageHost,
        PageRouter,
        PumpSink,
        TkDialogService,
        TkScheduler,
        UiEvent,
        UiPump,
        WorkflowNotResumable,
        apply_tk_scaling,
        load_settings,
        save_settings,
    )


APP_TITLE = "DHRT100 触发调试 Demo"
SETTINGS_PATH = Path.home() / ".trigger_debug_demo.json"
LOG_LIMIT = 2500

BG = DEFAULT_THEME.bg
PANEL = DEFAULT_THEME.panel
INK = DEFAULT_THEME.ink
MUTED = DEFAULT_THEME.muted
ACCENT = DEFAULT_THEME.accent
ACCENT_SOFT = DEFAULT_THEME.accent_soft
OK = DEFAULT_THEME.ok
WARN = DEFAULT_THEME.warn
ERR = DEFAULT_THEME.err

STATE_NAMES = {
    0: "IDLE", 1: "POSTED", 2: "TAKEN", 3: "BUSY", 4: "ACKED",
    5: "NACKED", 6: "TIMED_OUT",
}
REASON_NAMES = {
    0: "NONE", 1: "CONFIG_CRC_MISMATCH", 2: "HW_PROFILE_MISMATCH",
    3: "NODE_STALE", 4: "NODE_FAULT", 5: "FLASH_LOCKOUT_UNREADY",
    6: "RESOURCE_BUSY", 7: "RUN_STATE_DENIED", 8: "PAYLOAD_CRC_MISMATCH",
    9: "EPOCH_MISMATCH", 10: "DUP_SEQ_CRC_MISMATCH", 11: "TIMEOUT",
    12: "PERMISSION_DENIED", 13: "CONFIG_VALIDATION_FAILED",
    14: "NOT_ONLINE_SAFE",
}


def state_name(value: int) -> str:
    return STATE_NAMES.get(value, f"STATE_{value}")


def reason_name(value: int) -> str:
    return REASON_NAMES.get(value, f"REASON_{value}")


def mask_slots(mask: int) -> str:
    slots = [str(slot) for slot in range(32) if mask & (1 << slot)]
    return ",".join(slots) if slots else "-"


def _mark(value: int) -> str:
    return "✓" if value else "-"


class ScrollFrame(ttk.Frame):
    """A vertical scrolling frame usable inside a notebook tab."""

    def __init__(self, master, **kwargs) -> None:
        super().__init__(master, **kwargs)
        self.canvas = tk.Canvas(self, bg=BG, highlightthickness=0)
        self.scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.inner = ttk.Frame(self.canvas, style="Page.TFrame")
        self.window = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")
        self.inner.bind("<Configure>", self._on_inner)
        self.canvas.bind("<Configure>", self._on_canvas)
        self.canvas.bind("<Enter>", self._bind_wheel)
        self.canvas.bind("<Leave>", self._unbind_wheel)

    def _on_inner(self, _event=None) -> None:
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_canvas(self, event) -> None:
        self.canvas.itemconfigure(self.window, width=event.width)

    def _bind_wheel(self, _event=None) -> None:
        self.canvas.bind_all("<MouseWheel>", self._wheel)

    def _unbind_wheel(self, _event=None) -> None:
        self.canvas.unbind_all("<MouseWheel>")

    def _wheel(self, event) -> None:
        self.canvas.yview_scroll(int(-event.delta / 120), "units")


class FunctionPage(ScrollFrame):
    """Scrollable module page that owns its cards and renders connection state."""

    def __init__(self, master) -> None:
        super().__init__(master)
        self.cards: list[FunctionCard] = []

    def add_card(self, card: "FunctionCard") -> None:
        self.cards.append(card)

    def render(self, vector) -> None:
        for card in self.cards:
            card.render(vector)


class FunctionCard(ttk.LabelFrame):
    """One command function with semantic fields and a live wire preview."""

    def __init__(self, master, app: "DemoApp", function: catalog.Function) -> None:
        super().__init__(master, text=function.title, style="Card.TLabelframe",
                         padding=(12, 8))
        self.app = app
        self.function = function
        self.vars = {field.name: tk.StringVar(value=field.default)
                     for field in function.fields}
        self.channel = tk.StringVar(value="ctl" if function.ctl_name else "app")
        self.preview = tk.StringVar(value="")

        desc = ttk.Label(self, text=function.description, style="Muted.TLabel",
                         wraplength=980, justify="left")
        desc.grid(row=0, column=0, columnspan=6, sticky="w", pady=(0, 6))

        row = 1
        for index, field in enumerate(function.fields):
            column = (index % 3) * 2
            if index and index % 3 == 0:
                row += 1
            ttk.Label(self, text=field.label, style="Field.TLabel").grid(
                row=row, column=column, sticky="e", padx=(0, 4), pady=2)
            if field.kind == "enum":
                widget = ttk.Combobox(self, textvariable=self.vars[field.name],
                                      values=list(field.choices), width=12,
                                      state="readonly")
            else:
                widget = ttk.Entry(self, textvariable=self.vars[field.name], width=14)
            widget.grid(row=row, column=column + 1, sticky="w", padx=(0, 14), pady=2)
            if field.hint:
                ttk.Label(self, text=field.hint, style="Hint.TLabel").grid(
                    row=row + 1, column=column, columnspan=2, sticky="w", pady=(0, 4))
        row += 2

        controls = ttk.Frame(self, style="Card.TFrame")
        controls.grid(row=row, column=0, columnspan=6, sticky="ew", pady=(4, 2))
        if function.ctl_name:
            ttk.Radiobutton(controls, text="统一控制面", value="ctl",
                            variable=self.channel, command=self._refresh,
                            style="Card.TRadiobutton").pack(side="left")
            ttk.Radiobutton(controls, text="应用层 SCPI", value="app",
                            variable=self.channel, command=self._refresh,
                            style="Card.TRadiobutton").pack(side="left", padx=(6, 12))
        else:
            ttk.Label(controls, text="应用层专用", style="Muted.TLabel").pack(
                side="left", padx=(0, 12))
        self.send_button = ttk.Button(controls, text="发送", style="Accent.TButton",
                                      command=self.send)
        self.send_button.pack(side="left")
        ttk.Button(controls, text="填入示例", command=self.fill_example).pack(
            side="left", padx=6)
        ttk.Button(controls, text="复制命令", command=self.copy).pack(side="left")
        self.result = ttk.Label(controls, text="", style="Result.TLabel")
        self.result.pack(side="left", padx=12)

        ttk.Label(self, text="命令预览", style="Hint.TLabel").grid(
            row=row + 1, column=0, columnspan=6, sticky="w")
        preview = tk.Entry(self, textvariable=self.preview, font=("Consolas", 9),
                           readonlybackground="#f8fafc", state="readonly")
        preview.grid(row=row + 2, column=0, columnspan=6, sticky="ew", pady=(2, 0))
        self.columnconfigure(1, weight=1)
        self.columnconfigure(3, weight=1)
        self.columnconfigure(5, weight=1)

        if function.warning:
            ttk.Label(self, text="⚠ " + function.warning, style="Warn.TLabel",
                      wraplength=980, justify="left").grid(
                row=row + 3, column=0, columnspan=6, sticky="w", pady=(4, 0))

        for variable in self.vars.values():
            variable.trace_add("write", lambda *_args: self._refresh())
        self._refresh()

    def _values(self) -> dict[str, str]:
        return {name: variable.get() for name, variable in self.vars.items()}

    def render(self, vector) -> None:
        self.send_button.configure(
            state="normal" if vector.connection.connected else "disabled")

    def _build(self) -> str:
        if self.channel.get() == "ctl" and self.function.ctl_name:
            return catalog.build_ctl(self.function, self._values(),
                                     self.app.target_mask(), self.app.required_mask())
        return catalog.build_app(self.function, self._values())

    def _refresh(self) -> None:
        try:
            self.preview.set(self._build())
            self.result.configure(text="", foreground=OK)
        except ValueError as exc:
            self.preview.set(f"参数无效：{exc}")
            self.result.configure(text="", foreground=ERR)

    def fill_example(self) -> None:
        for field in self.function.fields:
            self.vars[field.name].set(field.default)

    def copy(self) -> None:
        text = self.preview.get()
        if text.startswith("参数无效"):
            return
        self.clipboard_clear()
        self.clipboard_append(text)
        self.result.configure(text="已复制", foreground=MUTED)

    def send(self) -> None:
        try:
            command = self._build()
        except ValueError as exc:
            self.app.dialogs.error("参数错误", str(exc))
            return
        source = f"function:{self.function.key}"
        if self.app.send_command(command, source):
            self.result.configure(text="已排队", foreground=ACCENT)

    def show_result(self, response: str, error: str) -> None:
        if error:
            self.result.configure(text=f"失败：{error}", foreground=ERR)
            return
        status = response.strip()
        if status.startswith("QUEUED"):
            self.result.configure(text=status, foreground=ACCENT)
        elif status in ("1", "OK"):
            self.result.configure(text="ACCEPTED", foreground=OK)
        elif status == "<timeout>":
            self.result.configure(text="超时", foreground=WARN)
        else:
            self.result.configure(text=status[:48], foreground=INK)


class QueryPanel(ttk.Frame):
    """List of read-only queries with argument form and parsed response."""

    def __init__(self, master, app: "DemoApp", queries: tuple[catalog.Query, ...]) -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.queries = queries
        self.by_key = {query.key: query for query in queries}
        self.vars: dict[str, tk.StringVar] = {}

        paned = ttk.Panedwindow(self, orient="horizontal")
        paned.pack(fill="both", expand=True, padx=10, pady=10)

        left = ttk.Frame(paned, style="Panel.TFrame", padding=8)
        paned.add(left, weight=1)
        ttk.Label(left, text="查询项", style="Section.TLabel").pack(anchor="w")
        self.listbox = ttk.Treeview(left, columns=("title", "scpi"), show="headings",
                                    height=14, selectmode="browse")
        self.listbox.heading("title", text="功能")
        self.listbox.heading("scpi", text="命令")
        self.listbox.column("title", width=120, anchor="w")
        self.listbox.column("scpi", width=230, anchor="w")
        for query in queries:
            self.listbox.insert("", "end", iid=query.key, values=(query.title, query.scpi))
        self.listbox.pack(fill="both", expand=True, pady=(6, 6))
        self.listbox.bind("<<TreeviewSelect>>", self._select)
        self.read_one_button = ttk.Button(
            left, text="读取选中", style="Accent.TButton",
            command=self.read_selected)
        self.read_one_button.pack(fill="x")
        self.read_all_button = ttk.Button(
            left, text="读取全部", command=self.read_all)
        self.read_all_button.pack(fill="x", pady=4)
        right = ttk.Frame(paned, style="Panel.TFrame", padding=10)
        paned.add(right, weight=3)
        self.title = ttk.Label(right, text="选择一项查询", style="Section.TLabel")
        self.title.pack(anchor="w")
        self.description = ttk.Label(right, text="", style="Muted.TLabel",
                                     wraplength=700, justify="left")
        self.description.pack(anchor="w", pady=(2, 8))
        self.form = ttk.Frame(right, style="Panel.TFrame")
        self.form.pack(fill="x")
        ttk.Label(right, text="响应", style="Hint.TLabel").pack(anchor="w", pady=(10, 2))
        self.raw = tk.Text(right, height=4, font=("Consolas", 9), wrap="none",
                           background="#f8fafc", relief="flat", borderwidth=1)
        self.raw.pack(fill="x")
        ttk.Label(right, text="解析", style="Hint.TLabel").pack(anchor="w", pady=(8, 2))
        self.table = ttk.Treeview(right, columns=("index", "field", "value"),
                                  show="headings", height=10)
        self.table.heading("index", text="#")
        self.table.heading("field", text="字段")
        self.table.heading("value", text="值")
        self.table.column("index", width=40, anchor="e")
        self.table.column("field", width=220, anchor="w")
        self.table.column("value", width=380, anchor="w")
        self.table.pack(fill="both", expand=True)
        self._forms: list[ttk.Widget] = []
        self.current: catalog.Query | None = None
        if queries:
            self.listbox.selection_set(queries[0].key)
            self._select()

    def render(self, vector) -> None:
        state = "normal" if vector.connection.connected else "disabled"
        self.read_one_button.configure(state=state)
        self.read_all_button.configure(state=state)

    def cancel(self) -> None:
        return None

    def dispose(self) -> None:
        return None

    def _select(self, _event=None) -> None:
        selection = self.listbox.selection()
        if not selection:
            return
        query = self.by_key[selection[0]]
        self.current = query
        self.title.configure(text=f"{query.title}  ·  {query.scpi}")
        self.description.configure(text=query.description)
        for widget in self._forms:
            widget.destroy()
        self._forms.clear()
        self.vars = {}
        for index, field in enumerate(query.args):
            variable = tk.StringVar(value=field.default)
            self.vars[field.name] = variable
            label = ttk.Label(self.form, text=field.label, style="Field.TLabel")
            label.grid(row=index // 2, column=(index % 2) * 2, sticky="e",
                       padx=(0, 4), pady=2)
            entry = ttk.Entry(self.form, textvariable=variable, width=14)
            entry.grid(row=index // 2, column=(index % 2) * 2 + 1, sticky="w",
                       padx=(0, 16), pady=2)
            self._forms.extend([label, entry])
        if not query.args:
            ttk.Label(self.form, text="无参数", style="Muted.TLabel").grid(
                row=0, column=0, sticky="w")

    def read_selected(self) -> None:
        if self.current is None or not self.app.connected:
            return
        try:
            command = catalog.build_query(self.current, self._values())
        except ValueError as exc:
            self.app.dialogs.error("参数错误", str(exc))
            return
        self.app.send_command(command, f"query:{self.current.key}")

    def _values(self) -> dict[str, str]:
        return {name: variable.get() for name, variable in self.vars.items()}

    def read_all(self) -> None:
        if not self.app.connected:
            return
        for query in self.queries:
            values = {field.name: field.default for field in query.args}
            try:
                command = catalog.build_query(query, values)
            except ValueError:
                continue
            self.app.send_command(command, f"query:{query.key}")

    def show_result(self, query_key: str, response: str, error: str) -> None:
        query = self.by_key.get(query_key)
        if query is None:
            return
        if self.current is not None and query.key == self.current.key:
            self._show(response, error)
        else:
            # Update only when the user is looking at this query.
            pass

    def _show(self, response: str, error: str) -> None:
        self.raw.delete("1.0", "end")
        if error:
            self.raw.insert("1.0", f"ERROR: {error}")
            self.table.delete(*self.table.get_children())
            return
        self.raw.insert("1.0", response)
        self.table.delete(*self.table.get_children())
        prefix, values = catalog.split_response(response)
        for name, value in prefix:
            self.table.insert("", "end", values=(name, name, value))
        for index, (_, value) in enumerate(values):
            if self.current is not None:
                label = catalog.response_label(self.current, index, value)
            else:
                label = f"value[{index}]"
            self.table.insert("", "end", values=(index, label, value))


class AckPage(ttk.Frame):
    """Board-target mask plus aggregate and per-board ACK evidence."""

    def __init__(self, master, app: "DemoApp") -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.slot_vars = [tk.BooleanVar(value=True) for _ in range(4)]
        self.required_var = tk.StringVar(value="15")
        self.required_var.trace_add(
            "write", lambda *_args: self.app.refresh_previews())
        self.auto_var = tk.BooleanVar(value=True)
        self.seq_var = tk.StringVar(value="-")
        self.state_var = tk.StringVar(value="-")
        self.ack_var = tk.StringVar(value="-")
        self.nack_var = tk.StringVar(value="-")
        self.timeout_var = tk.StringVar(value="-")
        self.reason_var = tk.StringVar(value="-")
        self._auto_token = None
        self._disposed = False

        top = ttk.Frame(self, style="Panel.TFrame", padding=12)
        top.pack(fill="x", padx=10, pady=(10, 6))
        ttk.Label(top, text="目标板卡", style="Section.TLabel").grid(
            row=0, column=0, sticky="w")
        for slot in range(4):
            ttk.Checkbutton(top, text=f"槽{slot} · {device.BOARD_ROLES[slot]}",
                            variable=self.slot_vars[slot],
                            command=self._sync_masks).grid(
                row=0, column=slot + 1, sticky="w", padx=(10, 0))
        ttk.Label(top, text="目标掩码", style="Field.TLabel").grid(
            row=1, column=0, sticky="e", pady=(8, 0))
        self.target_label = ttk.Label(top, text="15", style="Value.TLabel")
        self.target_label.grid(row=1, column=1, sticky="w", pady=(8, 0))
        ttk.Label(top, text="必需掩码", style="Field.TLabel").grid(
            row=1, column=2, sticky="e", pady=(8, 0))
        ttk.Entry(top, textvariable=self.required_var, width=8).grid(
            row=1, column=3, sticky="w", pady=(8, 0))
        self.read_aggregate_button = ttk.Button(
            top, text="读取聚合", command=self.read_aggregate)
        self.read_aggregate_button.grid(
            row=1, column=4, padx=(16, 4), pady=(8, 0))
        self.read_targets_button = ttk.Button(
            top, text="读取逐板", command=self.read_targets)
        self.read_targets_button.grid(row=1, column=5, padx=4, pady=(8, 0))
        self.read_reply_button = ttk.Button(
            top, text="读取回复", command=self.read_reply)
        self.read_reply_button.grid(row=1, column=6, padx=4, pady=(8, 0))
        ttk.Checkbutton(top, text="自动刷新", variable=self.auto_var).grid(
            row=1, column=7, padx=(16, 0), pady=(8, 0))
        top.columnconfigure(8, weight=1)

        summary = ttk.LabelFrame(self, text="聚合命令状态（SYSTem:CTRL:STATus?）",
                                 style="Card.TLabelframe", padding=(12, 8))
        summary.pack(fill="x", padx=10, pady=6)
        fields = (("Seq", self.seq_var), ("State", self.state_var),
                  ("ACK", self.ack_var), ("NACK", self.nack_var),
                  ("Timeout", self.timeout_var), ("Reason", self.reason_var))
        for index, (label, variable) in enumerate(fields):
            ttk.Label(summary, text=label, style="Field.TLabel").grid(
                row=0, column=index * 2, sticky="e", padx=(0, 4))
            ttk.Label(summary, textvariable=variable, style="Value.TLabel").grid(
                row=0, column=index * 2 + 1, sticky="w", padx=(0, 18))

        board_frame = ttk.LabelFrame(self, text="逐板 ACK（SYSTem:COMMand:TARget?）",
                                     style="Card.TLabelframe", padding=8)
        board_frame.pack(fill="both", expand=True, padx=10, pady=6)
        self.board_table = ttk.Treeview(
            board_frame,
            columns=("slot", "role", "seq", "state", "type", "ack", "nack", "reason"),
            show="headings", height=4)
        for column, text, width in (
                ("slot", "槽位", 50), ("role", "角色", 150), ("seq", "命令 Seq", 90),
                ("state", "状态", 100), ("type", "Type", 70), ("ack", "ACK", 60),
                ("nack", "NACK", 60), ("reason", "Reason", 180)):
            self.board_table.heading(column, text=text)
            self.board_table.column(column, width=width, anchor="w")
        self.board_table.pack(fill="both", expand=True)
        for slot in range(4):
            self.board_table.insert("", "end", iid=str(slot), values=(
                slot, device.BOARD_ROLES[slot], "-", "IDLE", "-", "-", "-", "-"))

        hint = ttk.Label(
            self, style="Muted.TLabel", justify="left", wraplength=1100,
            text="统一控制面只提交一个命令事实：目标掩码决定哪些板执行，必需掩码决定聚合完成条件。"
                 "本页只读回证据，不改变任何板卡状态。")
        hint.pack(fill="x", padx=12, pady=(0, 10))
        self._auto_token = self.app.scheduler.after(1000, self._auto_tick)

    def render(self, vector) -> None:
        state = "normal" if vector.connection.connected else "disabled"
        self.read_aggregate_button.configure(state=state)
        self.read_targets_button.configure(state=state)
        self.read_reply_button.configure(state=state)
        self.seq_var.set(vector.ack.seq)
        self.state_var.set(vector.ack.state)
        self.ack_var.set(vector.ack.ack)
        self.nack_var.set(vector.ack.nack)
        self.timeout_var.set(vector.ack.timeout)
        self.reason_var.set(vector.ack.reason)

    def cancel(self) -> None:
        self._disposed = True
        if self._auto_token is not None:
            self.app.scheduler.cancel(self._auto_token)
            self._auto_token = None

    def dispose(self) -> None:
        self.cancel()

    def _auto_tick(self) -> None:
        if self._disposed:
            return
        try:
            if (self.auto_var.get() and self.app.connected
                    and not self.app.pending_ack
                    and self.app.notebook.select() == str(self)):
                self.read_aggregate()
        except tk.TclError:
            return
        self._auto_token = self.app.scheduler.after(1000, self._auto_tick)

    def _sync_masks(self) -> None:
        mask = self.app.target_mask()
        self.target_label.configure(text=str(mask))
        self.required_var.set(str(mask))
        self.app.refresh_previews()

    def target_mask(self) -> int:
        return sum(1 << slot for slot, variable in enumerate(self.slot_vars)
                   if variable.get())

    def selected_slots(self) -> list[int]:
        return [slot for slot, variable in enumerate(self.slot_vars) if variable.get()]

    def read_aggregate(self) -> None:
        self.app.send_command("SYSTem:CTRL:STATus?", "ack:status")

    def read_targets(self) -> None:
        for slot in self.selected_slots():
            self.app.send_command(f"SYSTem:COMMand:TARget? {slot}", f"ack:target:{slot}")

    def read_reply(self) -> None:
        self.app.send_command("SYSTem:REFMEM:CMD:REPLy?", "ack:reply")

    def show_status(self, response: str) -> None:
        values = _csv(response)
        if len(values) < 13:
            return
        self.app.store.update_ack(
            seq=values[0],
            state=f"{values[2]} {state_name(int(values[2]))}",
            ack=f"{values[6]} ({mask_slots(int(values[6]))})",
            nack=f"{values[7]} ({mask_slots(int(values[7]))})",
            timeout=f"{values[9]} ({mask_slots(int(values[9]))})",
            reason=f"{values[10]} {reason_name(int(values[10]))}",
        )

    def show_target(self, slot: int, response: str) -> None:
        values = _csv(response)
        if len(values) < 9:
            return
        self.board_table.item(str(slot), values=(
            values[0], device.BOARD_ROLES[slot], values[1],
            f"{values[2]} {state_name(int(values[2]))}", values[3],
            _mark(int(values[6])), _mark(int(values[7])),
            f"{values[8]} {reason_name(int(values[8]))}"))

    def show_reply(self, response: str) -> None:
        values = _csv(response)
        if len(values) < 15:
            return
        self.app.store.update_ack(
            ack=f"{values[10]} ({mask_slots(int(values[10]))})",
            nack=f"{values[11]} ({mask_slots(int(values[11]))})",
            state=f"{values[9]} {state_name(int(values[9]))}",
        )


class OverviewPage(ttk.Frame):
    def __init__(self, master, app: "DemoApp") -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.vars = {name: tk.StringVar(value="-") for name in (
            "idn", "build", "board", "owner", "local", "is_owner",
            "mode", "trigger_state", "plan", "seq", "command_state",
            "ack", "nack", "reason", "nodes", "tcp")}
        grid = ttk.Frame(self, style="Page.TFrame")
        grid.pack(fill="both", expand=True, padx=10, pady=10)
        grid.columnconfigure(0, weight=1)
        grid.columnconfigure(1, weight=1)

        card = self._card(grid, "设备与端口", 0, 0)
        self._row(card, "设备", self.vars["idn"])
        self._row(card, "固件 Build", self.vars["build"])
        self._row(card, "本板编号", self.vars["board"])
        self._row(card, "TCP 透传", self.vars["tcp"])

        card = self._card(grid, "REFMEM 控制权", 0, 1)
        self._row(card, "Owner 槽位", self.vars["owner"])
        self._row(card, "本板槽位", self.vars["local"])
        self._row(card, "本板是否 Owner", self.vars["is_owner"])

        card = self._card(grid, "触发业务", 1, 0)
        self._row(card, "模式", self.vars["mode"])
        self._row(card, "运行状态", self.vars["trigger_state"])
        self._row(card, "Active Plan", self.vars["plan"])

        card = self._card(grid, "最近统一命令", 1, 1)
        self._row(card, "命令 Seq", self.vars["seq"])
        self._row(card, "状态", self.vars["command_state"])
        self._row(card, "ACK / NACK", self.vars["ack"])
        self._row(card, "拒绝原因", self.vars["reason"])

        card = self._card(grid, "板卡在线（REFMEM:NODES?）", 2, 0, columnspan=2)
        nodes = ttk.Label(card, textvariable=self.vars["nodes"], style="Value.TLabel",
                          justify="left")
        nodes.pack(anchor="w")

        actions = ttk.Frame(self, style="Page.TFrame")
        actions.pack(fill="x", padx=10, pady=(0, 10))
        self.refresh_button = ttk.Button(
            actions, text="刷新全部", style="Accent.TButton",
            command=self.refresh)
        self.refresh_button.pack(side="left")
        ttk.Button(actions, text="读取控制面目录",
                   command=lambda: self.app.control_page.read_catalog()).pack(
            side="left", padx=6)
        ttk.Button(actions, text="读取板卡 ACK",
                   command=lambda: self.app.ack_page.read_targets()).pack(side="left")

    def _card(self, parent, title: str, row: int, column: int, columnspan: int = 1):
        card = ttk.LabelFrame(parent, text=title, style="Card.TLabelframe",
                              padding=(12, 8))
        card.grid(row=row, column=column, columnspan=columnspan, sticky="nsew",
                  padx=6, pady=6)
        return card

    def _row(self, parent, label: str, variable: tk.StringVar) -> None:
        line = ttk.Frame(parent, style="Card.TFrame")
        line.pack(fill="x", pady=1)
        ttk.Label(line, text=label, style="Field.TLabel", width=14).pack(side="left")
        ttk.Label(line, textvariable=variable, style="Value.TLabel").pack(side="left")

    def refresh(self) -> None:
        if not self.app.connected:
            return
        bridge = getattr(self.app, "tcp_bridge", None)
        self.vars["tcp"].set(
            f"监听 {bridge.host}:{bridge.actual_port} · 客户端 {bridge.clients}"
            if bridge is not None and bridge.running else "未启动")
        for command, key in (
                ("*IDN?", "overview:idn"),
                ("SYSTem:FW:BUILD?", "overview:build"),
                ("SYSTem:BOARD:NO?", "overview:board"),
                ("SYSTem:CTRL:OWNer?", "overview:owner"),
                ("READ:TRIGger:STATe?", "overview:trigger"),
                ("SYSTem:CTRL:STATus?", "overview:status"),
                ("SYSTem:REFMEM:NODES? 0,4", "overview:nodes")):
            self.app.send_command(command, key)

    def show(self, key: str, response: str) -> None:
        values = _csv(response)
        try:
            if key == "overview:idn":
                self.vars["idn"].set(response.strip('"'))
            elif key == "overview:build":
                self.vars["build"].set(response.strip('"'))
            elif key == "overview:board":
                self.vars["board"].set(values[0])
            elif key == "overview:owner" and len(values) >= 4:
                self.vars["owner"].set(values[0])
                self.vars["local"].set(values[2])
                self.vars["is_owner"].set("是" if values[3] == "1" else "否")
            elif key == "overview:trigger" and len(values) >= 14:
                self.vars["mode"].set(values[0])
                self.vars["trigger_state"].set(values[1])
                self.vars["plan"].set(values[3])
            elif key == "overview:status" and len(values) >= 13:
                self.vars["seq"].set(values[0])
                self.vars["command_state"].set(f"{values[2]} {state_name(int(values[2]))}")
                self.vars["ack"].set(f"{values[6]} / {values[7]}")
                self.vars["reason"].set(f"{values[10]} {reason_name(int(values[10]))}")
            elif key == "overview:nodes" and len(values) >= 15:
                lines = []
                for index in range(0, min(len(values), 60), 15):
                    chunk = values[index:index + 15]
                    lines.append(f"槽{chunk[0]}  state={chunk[1]}  heartbeat={chunk[2]}  "
                                 f"age={chunk[5]}ms  stale={chunk[6]}  fault={chunk[8]}")
                self.vars["nodes"].set("\n".join(lines))
        except (ValueError, IndexError):
            pass


    def render(self, vector) -> None:
        self.refresh_button.configure(
            state="normal" if vector.connection.connected else "disabled")
        if vector.connection.tcp_listening:
            self.vars["tcp"].set("TCP 已启动")
        elif self.vars["tcp"].get() not in ("-", "未启动"):
            self.vars["tcp"].set("未启动")


class ControlPage(ttk.Frame):
    def __init__(self, master, app: "DemoApp") -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.name_var = tk.StringVar(value="TRIGGER_MODE")
        self.params_var = tk.StringVar(value="1")
        self.preview_var = tk.StringVar(value="")
        self.frame_hex = tk.StringVar(value="")

        top = ttk.LabelFrame(self, text="CTRL:EXECute（目录命令）",
                             style="Card.TLabelframe", padding=(12, 8))
        top.pack(fill="x", padx=10, pady=10)
        ttk.Label(top, text="命令", style="Field.TLabel").grid(row=0, column=0, sticky="e")
        names = [entry[1] for entry in device.CATALOG]
        ttk.Combobox(top, textvariable=self.name_var, values=names, width=30,
                     state="readonly").grid(row=0, column=1, sticky="w", padx=(0, 12))
        ttk.Label(top, text="参数 words", style="Field.TLabel").grid(
            row=0, column=2, sticky="e")
        ttk.Entry(top, textvariable=self.params_var, width=34).grid(
            row=0, column=3, sticky="w")
        ttk.Label(top, text="逗号分隔的 u32；浮点请用 IEEE-754 位模式",
                  style="Hint.TLabel").grid(row=1, column=3, sticky="w")
        ttk.Button(top, text="生成", command=self.preview).grid(
            row=0, column=4, padx=(12, 4))
        self.send_button = ttk.Button(top, text="发送", style="Accent.TButton",
                                      command=self.send)
        self.send_button.grid(row=0, column=5)
        ttk.Label(top, text="目标 / 必需掩码来自「板卡与 ACK」页；当前：",
                  style="Muted.TLabel").grid(row=2, column=0, columnspan=4,
                                             sticky="w", pady=(8, 0))
        self.mask_label = ttk.Label(top, text="", style="Value.TLabel")
        self.mask_label.grid(row=2, column=4, columnspan=2, sticky="w", pady=(8, 0))
        ttk.Entry(top, textvariable=self.preview_var, width=96,
                  font=("Consolas", 9), state="readonly").grid(
            row=3, column=0, columnspan=6, sticky="ew", pady=(6, 0))
        top.columnconfigure(5, weight=1)

        catalog_frame = ttk.LabelFrame(self, text="控制面目录（SYSTem:CTRL:CATalog?）",
                                        style="Card.TLabelframe", padding=8)
        catalog_frame.pack(fill="both", expand=True, padx=10, pady=6)
        self.catalog_table = ttk.Treeview(
            catalog_frame, columns=("id", "name", "type", "owner", "words", "kind"),
            show="headings", height=4)
        for column, text, width in (("id", "ID", 60), ("name", "名称", 260),
                                    ("type", "Type", 70), ("owner", "Owner", 70),
                                    ("words", "Words", 70), ("kind", "Kind", 60)):
            self.catalog_table.heading(column, text=text)
            self.catalog_table.column(column, width=width, anchor="w")
        self.catalog_table.pack(fill="both", expand=True)
        buttons = ttk.Frame(catalog_frame, style="Card.TFrame")
        buttons.pack(fill="x", pady=(6, 0))
        self.read_catalog_button = ttk.Button(
            buttons, text="读取目录", command=self.read_catalog)
        self.read_catalog_button.pack(side="left")
        self.read_owner_button = ttk.Button(
            buttons, text="读取 Owner",
            command=lambda: self.app.send_command("SYSTem:CTRL:OWNer?",
                                                  "control:owner"))
        self.read_owner_button.pack(side="left", padx=6)

        frame_box = ttk.LabelFrame(self, text="REFMEM 原始命令帧（56 B）",
                                    style="Card.TLabelframe", padding=(12, 8))
        frame_box.pack(fill="x", padx=10, pady=(6, 10))
        self.frame_fields = {
            "seq": tk.StringVar(value="1"), "target": tk.StringVar(value="15"),
            "required": tk.StringVar(value="15"), "epoch": tk.StringVar(value="1"),
            "run": tk.StringVar(value="1"), "ref": tk.StringVar(value="0"),
            "payload_crc": tk.StringVar(value="0"), "timeout": tk.StringVar(value="100000"),
            "source": tk.StringVar(value="0"), "instance": tk.StringVar(value="0"),
            "type": tk.StringVar(value="35"), "class": tk.StringVar(value="5"),
            "kind": tk.StringVar(value="0"), "size": tk.StringVar(value="4"),
        }
        labels = (("seq", "seq"), ("target", "target"), ("required", "required"),
                  ("epoch", "epoch"), ("run", "run_id"), ("ref", "payload_ref"),
                  ("payload_crc", "payload_crc"), ("timeout", "timeout_us"),
                  ("source", "source"), ("instance", "instance"),
                  ("type", "command_type"), ("class", "class"),
                  ("kind", "payload_kind"), ("size", "payload_size"))
        for index, (key, text) in enumerate(labels):
            ttk.Label(frame_box, text=text, style="Field.TLabel").grid(
                row=index // 4, column=(index % 4) * 2, sticky="e", padx=(0, 4), pady=2)
            ttk.Entry(frame_box, textvariable=self.frame_fields[key], width=12).grid(
                row=index // 4, column=(index % 4) * 2 + 1, sticky="w",
                padx=(0, 12), pady=2)
        self.build_frame_button = ttk.Button(
            frame_box, text="生成帧", command=self.build_frame)
        self.build_frame_button.grid(row=4, column=0, pady=(6, 0))
        self.send_frame_button = ttk.Button(
            frame_box, text="发送 CMD:SUBmit", style="Accent.TButton",
            command=self.send_frame)
        self.send_frame_button.grid(row=4, column=1, pady=(6, 0))
        ttk.Entry(frame_box, textvariable=self.frame_hex, width=100,
                  font=("Consolas", 8), state="readonly").grid(
            row=5, column=0, columnspan=8, sticky="ew", pady=(6, 0))

    def preview(self) -> None:
        mask = self.app.target_mask()
        required = self.app.required_mask()
        self.mask_label.configure(text=f"target={mask} required={required}")
        try:
            params = [int(item, 10) for item in self.params_var.get().split(",") if item.strip()]
            entry = device.CATALOG_BY_NAME[self.name_var.get()]
            if len(params) != entry[4]:
                raise ValueError(f"{self.name_var.get()} 需要 {entry[4]} 个参数")
            command = (f"SYSTem:CTRL:EXECute {self.name_var.get()},{mask},{required}"
                       + ("," + ",".join(map(str, params)) if params else ""))
            self.preview_var.set(command)
        except (ValueError, KeyError) as exc:
            self.preview_var.set(f"参数无效：{exc}")

    def _selected_key(self) -> str:
        for function in catalog.FUNCTIONS:
            if function.ctl_name == self.name_var.get():
                return function.key
        return ""

    def send(self) -> None:
        try:
            params = [int(item, 10) for item in self.params_var.get().split(",") if item.strip()]
        except ValueError:
            self.app.dialogs.error("参数错误", "参数必须是十进制 u32 word")
            return
        mask = self.app.target_mask()
        required = self.app.required_mask()
        if mask == 0:
            self.app.dialogs.error("目标为空", "至少选择一块目标板")
            return
        entry = device.CATALOG_BY_NAME.get(self.name_var.get())
        if entry is None:
            self.app.dialogs.error("参数错误", "目录中没有该命令")
            return
        if len(params) != entry[4]:
            self.app.dialogs.error(
                "参数错误",
                f"{self.name_var.get()} 需要 {entry[4]} 个参数，实际 {len(params)}")
            return
        command = (f"SYSTem:CTRL:EXECute {self.name_var.get()},{mask},{required}"
                   + ("," + ",".join(map(str, params)) if params else ""))
        self.app.send_command(command, f"ctl:{self.name_var.get()}")

    def read_catalog(self) -> None:
        self.catalog_table.delete(*self.catalog_table.get_children())
        self.app.send_command("SYSTem:CTRL:CATalog? 0,16", "catalog:0")

    def show_catalog(self, offset: int, response: str) -> None:
        values = _csv(response)
        for index in range(0, len(values) - 6, 7):
            row = values[index:index + 7]
            self.catalog_table.insert("", "end", values=(
                row[0], row[1], row[2], row[3], row[4], row[5]))
        if len(values) >= 16 * 7:
            next_offset = offset + 16
            if next_offset < 64:
                self.app.send_command(f"SYSTem:CTRL:CATalog? {next_offset},16",
                                      f"catalog:{next_offset}")

    def build_frame(self) -> None:
        try:
            values = {key: int(variable.get(), 10)
                      for key, variable in self.frame_fields.items()}
        except ValueError:
            self.app.dialogs.error("参数错误", "帧字段必须是十进制整数")
            return
        wire = bytearray(56)
        struct.pack_into("<IHH", wire, 0, 0x52434D44, 1, 0)
        struct.pack_into("<IIIIIIII", wire, 8, values["seq"], values["target"],
                         values["required"], values["epoch"], values["run"],
                         values["ref"], values["payload_crc"], values["timeout"])
        struct.pack_into("<HHHBBHH", wire, 40, values["source"], values["instance"],
                         values["type"], values["class"], values["kind"],
                         values["size"], 0)
        struct.pack_into("<I", wire, 52, zlib.crc32(bytes(wire[:52])) & 0xFFFFFFFF)
        self.frame_hex.set(bytes(wire).hex())

    def send_frame(self) -> None:
        if not self.frame_hex.get():
            self.build_frame()
        if not self.frame_hex.get():
            return
        self.app.send_command(f"SYSTem:REFMEM:CMD:SUBmit {self.frame_hex.get()}",
                              "ctl:frame")

    def render(self, vector) -> None:
        state = "normal" if vector.connection.connected else "disabled"
        for button in (self.send_button, self.read_catalog_button,
                       self.read_owner_button, self.build_frame_button,
                       self.send_frame_button):
            button.configure(state=state)


class RawPage(ttk.Frame):
    def __init__(self, master, app: "DemoApp") -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.command = tk.StringVar(value="*IDN?")
        top = ttk.Frame(self, style="Panel.TFrame", padding=12)
        top.pack(fill="x", padx=10, pady=10)
        ttk.Label(top, text="SCPI", style="Field.TLabel").pack(side="left")
        entry = ttk.Entry(top, textvariable=self.command, font=("Consolas", 10))
        entry.pack(side="left", fill="x", expand=True, padx=8)
        entry.bind("<Return>", lambda _event: self.send())
        self.send_button = ttk.Button(top, text="发送", style="Accent.TButton",
                                      command=self.send)
        self.send_button.pack(side="left")
        ttk.Label(self, text="所有命令都从同一个协调板端口发出；需要控制远端板卡时请用"
                             "「统一控制面」页或各功能页的控制面通道。",
                  style="Muted.TLabel", wraplength=1100, justify="left").pack(
            anchor="w", padx=12)
        self.history = ttk.Treeview(self, columns=("command", "response"),
                                    show="headings")
        self.history.heading("command", text="命令")
        self.history.heading("response", text="响应")
        self.history.column("command", width=520, anchor="w")
        self.history.column("response", width=520, anchor="w")
        self.history.pack(fill="both", expand=True, padx=10, pady=10)
        self.history.bind("<<TreeviewSelect>>", self._copy_selected)

    def send(self) -> None:
        command = self.command.get().strip()
        if command:
            self.app.send_command(command, "raw")

    def render(self, vector) -> None:
        self.send_button.configure(
            state="normal" if vector.connection.connected else "disabled")

    def cancel(self) -> None:
        return None

    def dispose(self) -> None:
        return None

    def _copy_selected(self, _event=None) -> None:
        selection = self.history.selection()
        if selection:
            values = self.history.item(selection[0], "values")
            if len(values) == 2:
                self.clipboard_clear()
                self.clipboard_append(values[1])

    def add(self, command: str, response: str) -> None:
        self.history.insert("", 0, values=(command, response))


class OtaPage(ttk.Frame):
    """Conventional single-board OTA plus the reserved TDMA stream shell."""

    def __init__(self, master, app: "DemoApp") -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.mode = tk.StringVar(value="conventional")
        self.path_var = tk.StringVar(value="")
        self.size_var = tk.StringVar(value="-")
        self.crc_var = tk.StringVar(value="-")
        self.sha_var = tk.StringVar(value="-")
        self.block_var = tk.StringVar(value="1024")
        self.package_var = tk.StringVar(value="自动")
        self.auto_receipt = tk.BooleanVar(value=True)
        self.progress_var = tk.IntVar(value=0)
        self.status_var = tk.StringVar(value="IDLE")
        self.slot_var = tk.StringVar(value="-")
        self.result_var = tk.StringVar(value="-")
        self.receipt_var = tk.StringVar(value="尚未签发")
        self.stream_var = tk.StringVar(value="-")
        self.busy = False
        self._cancel_event = threading.Event()
        self.image: ota.OtaImage | None = None
        self._log_history: list[str] = []
        self._receipt_path: Path | None = None
        self.workflow = OtaWorkflow(self)
        self.workflow_runner = self.workflow.runner

        mode_card = ttk.LabelFrame(self, text="OTA 模式", style="Card.TLabelframe",
                                    padding=(12, 8))
        mode_card.pack(fill="x", padx=10, pady=(10, 6))
        ttk.Radiobutton(
            mode_card, text="常规 OTA（当前单板，一台一台烧录）",
            variable=self.mode, value="conventional").pack(side="left")
        ttk.Radiobutton(
            mode_card, text="TDMA 流式 OTA（单端口多板）",
            variable=self.mode, value="stream").pack(side="left", padx=(16, 0))
        ttk.Label(mode_card, text="开发中：仅开放 stream 状态/能力读取",
                  style="Warn.TLabel").pack(side="left", padx=(16, 0))

        image_card = ttk.LabelFrame(self, text="镜像", style="Card.TLabelframe",
                                     padding=(12, 8))
        image_card.pack(fill="x", padx=10, pady=6)
        ttk.Label(image_card, text="文件", style="Field.TLabel").grid(
            row=0, column=0, sticky="e")
        ttk.Entry(image_card, textvariable=self.path_var).grid(
            row=0, column=1, columnspan=4, sticky="ew", padx=(4, 8))
        ttk.Button(image_card, text="选择…", command=self.browse).grid(
            row=0, column=5)
        for index, (label, variable) in enumerate((
                ("大小", self.size_var), ("CRC32", self.crc_var),
                ("SHA-256", self.sha_var))):
            ttk.Label(image_card, text=label, style="Field.TLabel").grid(
                row=1 + index, column=0, sticky="e", pady=1)
            ttk.Label(image_card, textvariable=variable, style="Value.TLabel").grid(
                row=1 + index, column=1, columnspan=4, sticky="w", padx=(4, 0))
        image_card.columnconfigure(1, weight=1)

        options = ttk.Frame(self, style="Panel.TFrame", padding=(12, 6))
        options.pack(fill="x", padx=10, pady=6)
        ttk.Label(options, text="块大小", style="Field.TLabel").pack(side="left")
        ttk.Combobox(options, textvariable=self.block_var, width=8,
                     values=("256", "512", "1024", "2048", "4096"),
                     state="readonly").pack(side="left", padx=(4, 14))
        ttk.Label(options, text="镜像类型", style="Field.TLabel").pack(side="left")
        ttk.Combobox(options, textvariable=self.package_var, width=10,
                     values=("自动", "原始镜像", "签名包"),
                     state="readonly").pack(side="left", padx=(4, 14))
        ttk.Checkbutton(options, text="完成后自动签发凭证",
                        variable=self.auto_receipt).pack(side="left")
        ttk.Button(options, text="刷新状态", command=self.refresh_status).pack(
            side="right", padx=4)
        ttk.Button(options, text="读取流状态", command=self.read_stream).pack(
            side="right", padx=4)

        action = ttk.Frame(self, style="Panel.TFrame", padding=(12, 6))
        action.pack(fill="x", padx=10, pady=6)
        self.start_button = ttk.Button(action, text="开始升级", style="Accent.TButton",
                                       command=self.start)
        self.start_button.pack(side="left")
        self.resume_button = ttk.Button(action, text="恢复上次",
                                        command=self.resume)
        self.resume_button.pack(side="left", padx=6)
        self.abort_button = ttk.Button(action, text="中止", command=self.abort)
        self.abort_button.pack(side="left", padx=6)
        self.boot_button = ttk.Button(action, text="重启到新镜像", command=self.boot)
        self.boot_button.pack(side="left", padx=6)
        self.commit_button = ttk.Button(action, text="确认 COMMIT", command=self.commit)
        self.commit_button.pack(side="left", padx=6)
        ttk.Button(action, text="补发凭证", command=self.issue_receipt_now).pack(
            side="left", padx=6)
        ttk.Label(action, textvariable=self.status_var, style="Value.TLabel").pack(
            side="right")

        progress = ttk.Frame(self, style="Panel.TFrame", padding=(12, 4))
        progress.pack(fill="x", padx=10, pady=4)
        ttk.Progressbar(progress, maximum=1000, variable=self.progress_var,
                        style="OTA.Horizontal.TProgressbar").pack(
            fill="x", expand=True)
        ttk.Label(progress, textvariable=self.slot_var, style="Muted.TLabel").pack(
            anchor="w", pady=(4, 0))
        ttk.Label(progress, textvariable=self.result_var, style="Muted.TLabel").pack(
            anchor="w")
        ttk.Label(progress, textvariable=self.receipt_var, style="Muted.TLabel").pack(
            anchor="w")

        body = ttk.Panedwindow(self, orient="horizontal")
        body.pack(fill="both", expand=True, padx=10, pady=(4, 10))
        left = ttk.Frame(body, style="Panel.TFrame", padding=8)
        body.add(left, weight=1)
        ttk.Label(left, text="OTA 日志", style="Section.TLabel").pack(anchor="w")
        self.log_text = ScrolledText(left, height=10, font=("Consolas", 9),
                                     background="#f8fafc", relief="flat")
        self.log_text.pack(fill="both", expand=True, pady=(4, 0))
        right = ttk.Frame(body, style="Panel.TFrame", padding=8)
        body.add(right, weight=1)
        ttk.Label(right, text="TDMA 流状态（开发中）", style="Section.TLabel").pack(
            anchor="w")
        ttk.Label(right, textvariable=self.stream_var, style="Value.TLabel",
                  justify="left").pack(anchor="w", pady=(4, 0))
        ttk.Label(right, style="Muted.TLabel", justify="left", wraplength=480,
                  text="开发中：只读 stream 状态，不发起跨板烧录。").pack(
            anchor="w", pady=(8, 0))

    # ------------------------------------------------------------- helpers
    def _record(self, text: str, kind: str = "log") -> None:
        line = f"[{time.strftime('%H:%M:%S')}] {text}"
        self._log_history.append(line)
        self.app.ota_events.put((kind, line))

    def _package_mode(self, image: ota.OtaImage) -> bool:
        choice = self.package_var.get()
        if choice == "签名包":
            return True
        if choice == "原始镜像":
            return False
        return image.package

    def _request(self, command: str, timeout_s: float = 10.0):
        if self.app.dispatcher is None:
            raise RuntimeError("未连接")
        result = self.app.dispatcher.request(command, "ota", timeout_s)
        if result.error:
            raise RuntimeError(result.error)
        return result.response

    def _snapshot(self) -> dict[str, str]:
        snapshot: dict[str, str] = {}
        for key, command in (
                ("idn", "*IDN?"), ("build", "SYSTem:FW:BUILD?"),
                ("board_id", "SYSTem:BOARD:ID?"),
                ("board_no", "SYSTem:BOARD:NO?"),
                ("slot", "SYSTem:OTA:SLOT?"),
                ("status", "SYSTem:OTA:STATus?"),
                ("result", "SYSTem:OTA:RESult?"),
                ("progress", "SYSTem:OTA:PROGress?")):
            try:
                snapshot[key] = self._request(command, 5.0)
            except Exception as exc:
                snapshot[key] = f"ERROR:{exc}"
        return snapshot

    def browse(self) -> None:
        path = filedialog.askopenfilename(
            parent=self, title="选择 OTA 镜像",
            filetypes=[("OTA image", "*.pkg *.bin"), ("All", "*.*")])
        if not path:
            return
        self.path_var.set(path)
        try:
            image = ota.OtaImage.load(path)
        except Exception as exc:
            self.app.dialogs.error("镜像错误", str(exc))
            return
        self.image = image
        self.size_var.set(f"{image.size} B")
        self.crc_var.set(f"0x{image.crc32:08X}")
        self.sha_var.set(image.sha256)
        self.package_var.set("签名包" if image.package else "自动")

    def start(self) -> None:
        if self.mode.get() == "stream":
            self.app.dialogs.info(
                "TDMA 流式 OTA",
                "TDMA 流式 OTA 仍在开发中；当前只提供 stream 状态/能力读取，"
                "不会发起跨板烧录。")
            return
        if self.busy:
            return
        if not self.app.connected:
            self.app.dialogs.warning("未连接", "请先连接协调板端口（或演示模式）")
            return
        path = self.path_var.get().strip()
        if not path:
            self.app.dialogs.warning("未选择镜像", "请选择 .bin 或 .pkg 文件")
            return
        try:
            image = ota.OtaImage.load(path)
            block_size = int(self.block_var.get(), 10)
        except (ValueError, OSError) as exc:
            self.app.dialogs.error("参数错误", str(exc))
            return
        self.image = image
        self.busy = True
        self._cancel_event.clear()
        self.workflow = OtaWorkflow(self)
        self.workflow_runner = self.workflow.runner
        self.app.ota_busy = True
        self.progress_var.set(0)
        self.status_var.set("传输中…")
        self._set_busy(True)
        package = self._package_mode(image)
        auto_receipt = self.auto_receipt.get()
        backend = self.app.backend.get()
        resource = self.app.port.get()
        self._record(f"开始 OTA：{image.path} size={image.size} "
                     f"crc=0x{image.crc32:08X} block={block_size}")
        threading.Thread(target=self._run_workflow,
                         args=(image, block_size, package, auto_receipt,
                               backend, resource),
                         name="ota-transfer", daemon=True).start()

    def _run_workflow(self, image: ota.OtaImage, block_size: int, package: bool,
                      auto_receipt: bool, backend: str, resource: str) -> None:
        self._record(f"Workflow ota.conventional 启动：{image.path}")
        try:
            self.workflow.start(image, block_size, package, auto_receipt,
                                backend, resource)
        except Exception as exc:
            self.app.ota_events.put(("failed", str(exc)))
            self._record(f"OTA workflow 失败：{exc}")
        finally:
            self.app.ota_busy = False

    def resume(self) -> None:
        if self.busy:
            return
        state = self.workflow.latest()
        if state is None:
            self.app.dialogs.info("恢复 OTA", "没有找到可恢复的 OTA workflow")
            return
        if state.status == "SUCCEEDED":
            self.app.dialogs.info("恢复 OTA", "最近一次 OTA workflow 已完成")
            return
        inputs = state.inputs
        try:
            image = ota.OtaImage.load(inputs["image_path"])
        except Exception as exc:
            self.app.dialogs.error("恢复失败", f"镜像不可读：{exc}")
            return
        self.image = image
        self.path_var.set(str(image.path))
        self.block_var.set(str(inputs.get("block_size", "1024")))
        self.package_var.set("签名包" if inputs.get("package") else "原始镜像")
        self.auto_receipt.set(bool(inputs.get("auto_receipt", True)))
        self.busy = True
        self._cancel_event.clear()
        self.workflow = OtaWorkflow(self)
        self.workflow_runner = self.workflow.runner
        self.app.ota_busy = True
        self.progress_var.set(0)
        self.status_var.set("恢复中…")
        self._set_busy(True)
        self._record(f"恢复 workflow {state.run_id} @ step {state.cursor}")
        threading.Thread(target=self._resume_workflow, args=(state.run_id,),
                         name="ota-resume", daemon=True).start()

    def _resume_workflow(self, run_id: str) -> None:
        try:
            self.workflow.resume(run_id)
        except WorkflowNotResumable as exc:
            self.app.ota_events.put(("failed", f"不可恢复：{exc}"))
            self._record(f"OTA workflow 不可恢复：{exc}")
        except Exception as exc:
            self.app.ota_events.put(("failed", str(exc)))
            self._record(f"OTA workflow 恢复失败：{exc}")
        finally:
            self.app.ota_busy = False

    def _run(self, image: ota.OtaImage, block_size: int, package: bool,
             auto_receipt: bool, backend: str, resource: str) -> None:
        """Compatibility wrapper; all OTA work now goes through OtaWorkflow."""
        self._run_workflow(image, block_size, package, auto_receipt,
                           backend, resource)

    def _wait_ready(self) -> tuple[str, dict]:
        deadline = time.monotonic() + 60.0
        status = {"state": "UNKNOWN", "last_result": 1}
        while time.monotonic() < deadline:
            try:
                status = ota.parse_status(self._request("SYSTem:OTA:STATus?", 5.0))
            except Exception:
                time.sleep(0.5)
                continue
            state = str(status.get("state", ""))
            if state in ("READY_TO_REBOOT", "APPLIED", "FAILED", "ABORTED",
                         "IDLE"):
                return state, status
            time.sleep(0.3)
        return "TIMEOUT", status

    def _progress(self, received: int, expected: int) -> None:
        permille = 0 if expected <= 0 else min(1000, received * 1000 // expected)
        self.app.ota_events.put(("progress", (received, expected, permille)))

    def _emit_state(self, snapshot: dict[str, str]) -> None:
        self.app.ota_events.put(("state", snapshot))

    def _finish(self, image: ota.OtaImage, before: dict, after: dict,
                stage: str, passed: bool, transfer: dict,
                auto_receipt: bool, backend: str, resource: str) -> None:
        body = ota.receipt_body(
            mode="CONVENTIONAL_SINGLE_BOARD",
            backend=backend,
            resource=resource,
            board_id=before.get("board_id", ""),
            board_no=before.get("board_no", ""),
            image=image,
            before=before,
            after=after,
            transfer=transfer,
            stage=stage,
            passed=passed,
            log_sha256=hashlib.sha256(
                "\n".join(self._log_history).encode("utf-8")).hexdigest(),
        )
        path = None
        if auto_receipt:
            path = ota.write_receipt(Path("out/ota_receipts"), body)
        self.app.ota_events.put(("done", {
            "passed": passed, "stage": stage, "path": str(path) if path else "",
            "receipt": body,
        }))

    def abort(self) -> None:
        if self.busy:
            self._cancel_event.set()
            if getattr(self, "workflow_runner", None) is not None:
                self.workflow_runner.cancel()
            self._record("已请求中止（当前块结束后发送 ABORT）")
            return
        if self.app.connected:
            self.app.send_command("SYSTem:OTA:ABORt", "ota:abort")

    def boot(self) -> None:
        if not self.app.dialogs.confirm(
                "重启到新镜像",
                "确认设备已进入 READY_TO_REBOOT，并立即重启到新镜像？",
                danger=True):
            return
        if self.app.send_command("SYSTem:OTA:BOOT", "ota:boot"):
            self._record("已发送 BOOT；设备将重启，串口/VISA 可能断开")
            self.status_var.set("重启中")

    def commit(self) -> None:
        if not self.app.dialogs.confirm(
                "确认新镜像",
                "确认新镜像已正常启动并通过自检，执行 COMMIT？",
                danger=True):
            return
        if self.app.send_command("SYSTem:OTA:COMMit", "ota:commit"):
            self._record("已发送 COMMIT")
            self.app.scheduler.after(900, self.issue_receipt_now)

    def refresh_status(self) -> None:
        if not self.app.connected:
            return
        self.app.send_command("SYSTem:OTA:STATus?", "ota:status")
        self.app.send_command("SYSTem:OTA:SLOT?", "ota:slot")
        self.app.send_command("SYSTem:OTA:RESult?", "ota:result")
        self.app.send_command("SYSTem:OTA:PROGress?", "ota:progress")

    def read_stream(self) -> None:
        if not self.app.connected:
            return
        self.app.send_command("SYSTem:OTA:STReam:STATus?", "ota:stream")
        self.app.send_command("SYSTem:OTA:STReam:CAPability?", "ota:streamcap")
        self.app.send_command("SYSTem:OTA:TDMA:DIAG?", "ota:tdmadiag")

    def on_result(self, source: str, response: str, error: str) -> None:
        if error:
            self._record(f"{source} 失败：{error}")
            return
        if source == "ota:status":
            status = ota.parse_status(response)
            self.status_var.set(f"{status['state']} · target slot {status['target_slot']}")
            self._record(f"STATUS → {response}")
        elif source == "ota:slot":
            self.slot_var.set(f"SLOT? {response}")
            self._record(f"SLOT → {response}")
        elif source == "ota:result":
            self.result_var.set(f"RESULT? {response}")
            self._record(f"RESULT → {response}")
        elif source == "ota:progress":
            self._record(f"PROGRESS → {response}")
        elif source == "ota:stream":
            self.stream_var.set(f"STREAM:STAT? {response}")
        elif source == "ota:streamcap":
            self.stream_var.set(self.stream_var.get() + f"\nSTREAM:CAP? {response}")
        elif source == "ota:tdmadiag":
            self.stream_var.set(self.stream_var.get() + f"\nTDMA:DIAG? {response}")
        elif source in ("ota:abort", "ota:boot", "ota:commit"):
            self._record(f"{source} → {response}")
            if source == "ota:commit":
                self.app.scheduler.after(900, self.issue_receipt_now)

    def issue_receipt_now(self) -> None:
        if not self.app.connected or self.app.dispatcher is None:
            return
        backend = self.app.backend.get()
        resource = self.app.port.get()
        threading.Thread(target=self._issue_current,
                         args=(backend, resource),
                         name="ota-receipt", daemon=True).start()

    def _issue_current(self, backend: str, resource: str) -> None:
        try:
            snapshot = self._snapshot()
            status = ota.parse_status(snapshot.get("status", ""))
            result = ota.parse_result(snapshot.get("result", ""))
            stage = str(status.get("state", "UNKNOWN"))
            passed = result.get("last_result", 1) == 0 and stage in (
                "READY_TO_REBOOT", "APPLIED")
            body = ota.receipt_body(
                mode="CONVENTIONAL_SINGLE_BOARD",
                backend=backend,
                resource=resource,
                board_id=snapshot.get("board_id", ""),
                board_no=snapshot.get("board_no", ""),
                image=self.image,
                before={},
                after=snapshot,
                transfer={},
                stage=stage,
                passed=passed,
                log_sha256=hashlib.sha256(
                    "\n".join(self._log_history).encode("utf-8")).hexdigest(),
            )
            path = ota.write_receipt(Path("out/ota_receipts"), body)
            self.app.ota_events.put(("receipt", str(path)))
        except Exception as exc:
            self.app.ota_events.put(("failed", f"签发凭证失败：{exc}"))

    def note(self, kind: str, payload) -> None:
        if kind == "progress":
            received, expected, permille = payload
            self.progress_var.set(permille)
            self.status_var.set(f"传输中 {received}/{expected} B")
        elif kind == "state":
            snapshot = payload
            status = ota.parse_status(snapshot.get("status", ""))
            self.status_var.set(f"{status['state']} · slot {status['target_slot']}")
            self.slot_var.set(f"SLOT? {snapshot.get('slot', '-')}")
            self.result_var.set(f"RESULT? {snapshot.get('result', '-')}")
        elif kind == "log":
            self.log_text.insert("end", str(payload) + "\n")
            self.log_text.see("end")
        elif kind == "receipt":
            self._receipt_path = Path(str(payload))
            self.receipt_var.set(f"凭证：{payload}")
        elif kind == "done":
            info = payload if isinstance(payload, dict) else {}
            self.busy = False
            self.app.ota_busy = False
            if info.get("path"):
                self._receipt_path = Path(info["path"])
                self.receipt_var.set(f"凭证：{info['path']}")
            self.status_var.set(
                f"{'PASS' if info.get('passed') else 'FAIL'} · {info.get('stage')}")
            self._record(f"OTA 结束：{info.get('stage')} passed={info.get('passed')}")
            self._set_busy(False)
        elif kind == "failed":
            self.busy = False
            self.app.ota_busy = False
            self.status_var.set("FAILED")
            self._record(f"失败：{payload}")
            self._set_busy(False)

    def _set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        self.start_button.configure(state=state)
        self.resume_button.configure(state=state)
        self.abort_button.configure(state="normal" if busy else "normal")
        self.boot_button.configure(state="normal")
        self.commit_button.configure(state="normal")


    def render(self, vector) -> None:
        state = "normal" if vector.connection.connected else "disabled"
        self.start_button.configure(state=state)
        self.resume_button.configure(state=state)

    def cancel(self) -> None:
        self._cancel_event.set()
        if getattr(self, "workflow_runner", None) is not None:
            self.workflow_runner.cancel()

    def dispose(self) -> None:
        self.cancel()


class TcpPage(ttk.Frame):
    """Local TCP pass-through for external tools."""

    def __init__(self, master, app: "DemoApp") -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.host = tk.StringVar(value=app._settings.get("tcp_host", "127.0.0.1"))
        self.port = tk.StringVar(value=app._settings.get("tcp_port", "5025"))
        self.status = tk.StringVar(value="未启动")
        self.clients = tk.StringVar(value="客户端：0")
        self._stopping = False

        top = ttk.LabelFrame(self, text="本地 TCP 透传服务", style="Card.TLabelframe",
                             padding=(12, 8))
        top.pack(fill="x", padx=10, pady=10)
        ttk.Label(top, text="监听地址", style="Field.TLabel").grid(
            row=0, column=0, sticky="e")
        ttk.Entry(top, textvariable=self.host, width=16).grid(
            row=0, column=1, sticky="w", padx=(4, 12))
        ttk.Label(top, text="端口", style="Field.TLabel").grid(
            row=0, column=2, sticky="e")
        ttk.Entry(top, textvariable=self.port, width=10).grid(
            row=0, column=3, sticky="w", padx=(4, 12))
        self.button = ttk.Button(top, text="启动", style="Accent.TButton",
                                 command=self.toggle)
        self.button.grid(row=0, column=4)
        ttk.Label(top, textvariable=self.status, style="Value.TLabel").grid(
            row=0, column=5, padx=(16, 0))
        ttk.Label(top, textvariable=self.clients, style="Muted.TLabel").grid(
            row=0, column=6, padx=(12, 0))
        ttk.Label(
            top, style="Muted.TLabel", justify="left", wraplength=1100,
            text="外部程序连接 tcp://127.0.0.1:<端口>，每行一条 SCPI 命令，"
                 "服务返回一行响应。TCP 与 GUI 共用同一个调度器和同一条串口，"
                 "不会出现两个写者；连接断开或发送 QUIT 即结束会话。").grid(
            row=1, column=0, columnspan=7, sticky="w", pady=(8, 0))

        example = ttk.LabelFrame(self, text="外部程序示例", style="Card.TLabelframe",
                                 padding=(10, 6))
        example.pack(fill="x", padx=10, pady=(0, 6))
        self.example_text = tk.Text(example, height=7, font=("Consolas", 9),
                                    wrap="none", background="#f8fafc", relief="flat")
        self.example_text.insert(
            "1.0",
            "import socket\n"
            "with socket.create_connection(('127.0.0.1', 5025)) as s:\n"
            "    f = s.makefile('rwb', buffering=0)\n"
            "    f.write(b'*IDN?\\n'); print(f.readline().decode())\n"
            "    f.write(b'SYSTem:CTRL:EXECute TRIGGER_MODE,15,15,1\\n')\n"
            "    print(f.readline().decode())   # QUEUED,<seq>\n"
            "    f.write(b'SYSTem:CTRL:STATus?\\n'); print(f.readline().decode())\n")
        self.example_text.configure(state="disabled")
        self.example_text.pack(fill="x")
        ttk.Button(example, text="复制示例", command=self.copy_example).pack(
            anchor="e", pady=(4, 0))

        self.log = tk.Text(self, height=12, font=("Consolas", 9), wrap="none",
                           background="#f8fafc", relief="flat")
        self.log.pack(fill="both", expand=True, padx=10, pady=(0, 10))

    def copy_example(self) -> None:
        self.clipboard_clear()
        self.clipboard_append(self.example_text.get("1.0", "end-1c"))
        self.app.status_var.set("TCP 示例已复制")

    def toggle(self) -> None:
        if self.app.tcp_bridge is not None and self.app.tcp_bridge.running:
            self.stop()
        else:
            self.start()

    def start(self) -> None:
        if not self.app.connected or self.app.dispatcher is None:
            self.app.dialogs.warning("未连接", "请先连接协调板端口（或演示模式）")
            return
        try:
            port = int(self.port.get(), 10)
        except ValueError:
            self.app.dialogs.error("参数错误", "端口必须是整数")
            return
        bridge = device.TcpBridge(
            self.app.dispatcher, self.host.get().strip() or "127.0.0.1",
            port, lambda kind, text: self.app.tcp_events.put((kind, text)),
            timeout_s=3.0, can_accept=lambda: not self.app.ota_busy)
        try:
            bridge.start()
        except OSError as exc:
            self.app.dialogs.error("TCP 启动失败", str(exc))
            return
        self.app.tcp_bridge = bridge
        self._stopping = False
        self.port.set(str(bridge.actual_port))
        self.status.set(f"监听 {bridge.host}:{bridge.actual_port}")
        self.button.configure(text="停止", state="normal")
        self.app.overview_page.vars["tcp"].set(
            f"监听 {bridge.host}:{bridge.actual_port} · 客户端 0")
        self.app.log(f"TCP 透传已启动：{bridge.host}:{bridge.actual_port}", "INFO")
        self.app._save_settings()

    def stop(self, wait: bool = False) -> None:
        bridge = self.app.tcp_bridge
        if bridge is None:
            self._finish_stop()
            return
        if wait:
            bridge.stop()
            self.app.tcp_bridge = None
            self._finish_stop()
            return
        if self._stopping:
            return
        self._stopping = True
        self.button.configure(state="disabled")
        self.status.set("正在停止…")
        bridge.stop_async()
        self.app.scheduler.after(50, self._await_stop, bridge, 0)

    def _await_stop(self, bridge, attempt: int) -> None:
        if bridge.wait_stopped(0):
            if self.app.tcp_bridge is bridge:
                self.app.tcp_bridge = None
            self._finish_stop()
            return
        if attempt >= 40:
            self._stopping = False
            self.status.set("停止超时，仍在后台退出")
            self.button.configure(state="normal")
            return
        self.app.scheduler.after(50, self._await_stop, bridge, attempt + 1)

    def _finish_stop(self) -> None:
        self._stopping = False
        self.status.set("未启动")
        self.clients.set("客户端：0")
        self.button.configure(text="启动", state="normal")
        self.app.overview_page.vars["tcp"].set("未启动")
        self.app.log("TCP 透传已停止", "INFO")

    def note(self, kind: str, text: str) -> None:
        stamp = time.strftime("%H:%M:%S")
        self.log.insert("end", f"[{stamp}] {kind} {text}\n")
        self.log.see("end")
        if kind == "INFO" and ("客户端" in text or "监听" in text):
            bridge = self.app.tcp_bridge
            if bridge is not None:
                self.clients.set(f"客户端：{bridge.clients}")
                self.app.overview_page.vars["tcp"].set(
                    f"监听 {bridge.host}:{bridge.actual_port} · 客户端 {bridge.clients}")


    def render(self, vector) -> None:
        if self._stopping:
            return
        self.button.configure(
            state="normal" if vector.connection.connected else "disabled")

    def cancel(self) -> None:
        self.stop(wait=True)

    def dispose(self) -> None:
        self.stop(wait=True)


class DiagnosticsPage(ttk.Frame):
    def __init__(self, master, app: "DemoApp") -> None:
        super().__init__(master, style="Page.TFrame")
        self.app = app
        self.raw = tk.Text(self, height=16, font=("Consolas", 9), wrap="none",
                           background="#f8fafc", relief="flat")
        self.raw.pack(fill="both", expand=True, padx=10, pady=(10, 6))
        bar = ttk.Frame(self, style="Page.TFrame")
        bar.pack(fill="x", padx=10, pady=(0, 10))
        self.command_buttons = []
        for label, command in (("错误队列", "SYSTem:ERRor?"),
                               ("最近故障", "SYSTem:FAULT:LAST?"),
                               ("环状态", "SYSTem:TDMA:RING:STATus?"),
                               ("命令帧", "SYSTem:REFMEM:CMDFRame?"),
                               ("装载统计", "SYSTem:REFMEM:CMD:STATus?")):
            button = ttk.Button(
                bar, text=label,
                command=lambda c=command: self.app.send_command(c, "diag"))
            button.pack(side="left", padx=(0, 6))
            self.command_buttons.append(button)
        ttk.Button(bar, text="清空", command=lambda: self.raw.delete("1.0", "end")).pack(
            side="right")

    def render(self, vector) -> None:
        state = "normal" if vector.connection.connected else "disabled"
        for button in self.command_buttons:
            button.configure(state=state)

    def cancel(self) -> None:
        return None

    def dispose(self) -> None:
        return None

    def add(self, command: str, response: str) -> None:
        self.raw.insert("end", f"> {command}\n{response}\n\n")
        self.raw.see("end")


class DemoApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self._settings = self._load_settings()
        self.geometry(self._settings.get("geometry", "1280x880"))
        self.minsize(1080, 720)
        self.configure(bg=BG)
        self.dispatcher: device.Dispatcher | None = None
        self.transport = None
        self.store = ConsoleStore()
        self.scheduler = TkScheduler(self)
        self.dialogs = TkDialogService(self)
        self.errors = ErrorBoundary(self._on_ui_error)
        self.pump = UiPump(self.scheduler, self._handle_ui_event)
        self.page_host = PageHost(self.store, self.scheduler, self.dialogs)
        self.router: PageRouter | None = None
        self.tcp_events = PumpSink(self.pump, "tcp")
        self.ota_events = PumpSink(self.pump, "ota")
        self.tcp_bridge: device.TcpBridge | None = None
        self.ota_busy = False
        self.connected = False
        self.pending_ack = False
        self._ack_deadline = 0.0
        self._ack_seq = 0
        self._closing = False
        self._log_lines = 0

        self.backend = tk.StringVar(value=self._settings.get("backend", "演示模式"))
        self.port = tk.StringVar(value=self._settings.get("port", ""))
        self.baud = tk.StringVar(value=self._settings.get("baud", "115200"))
        self.auto_scroll = tk.BooleanVar(value=True)
        self.status_var = tk.StringVar(value="未连接")
        self.conn_var = tk.StringVar(value="未连接")
        self.mask_text = tk.StringVar(value="target=15 required=15")

        self._style()
        self._build()
        self.refresh_previews()
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.pump.start()
        self.scheduler.after(150, self._startup)
        self._sync_store()

    # ------------------------------------------------------------- settings
    def _load_settings(self) -> dict:
        return load_settings(SETTINGS_PATH)

    def _save_settings(self) -> None:
        data = {"backend": self.backend.get(), "port": self.port.get(),
                "baud": self.baud.get(), "geometry": self.geometry()}
        if hasattr(self, "tcp_page"):
            data["tcp_host"] = self.tcp_page.host.get()
            data["tcp_port"] = self.tcp_page.port.get()
        try:
            save_settings(SETTINGS_PATH, data)
        except OSError:
            pass

    # ----------------------------------------------------------------- style
    def _style(self) -> None:
        apply_tk_scaling(self)
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", font=("Microsoft YaHei UI", 9), background=BG, foreground=INK)
        style.configure("TFrame", background=BG)
        style.configure("Page.TFrame", background=BG)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("Card.TFrame", background=PANEL)
        style.configure("Card.TLabelframe", background=PANEL, bordercolor="#d7dee9",
                        relief="solid", borderwidth=1)
        style.configure("Card.TLabelframe.Label", background=PANEL, foreground=INK,
                        font=("Microsoft YaHei UI", 9, "bold"))
        style.configure("Card.TRadiobutton", background=PANEL)
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", padding=(14, 7), font=("Microsoft YaHei UI", 9))
        style.map("TNotebook.Tab",
                  background=[("selected", PANEL), ("!selected", "#dde6f2")],
                  foreground=[("selected", ACCENT), ("!selected", MUTED)])
        style.configure("Section.TLabel", background=PANEL, foreground=INK,
                        font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("Muted.TLabel", background=BG, foreground=MUTED)
        style.configure("Field.TLabel", background=PANEL, foreground=MUTED)
        style.configure("Hint.TLabel", background=PANEL, foreground="#94a3b8",
                        font=("Microsoft YaHei UI", 8))
        style.configure("Value.TLabel", background=PANEL, foreground=INK,
                        font=("Consolas", 9, "bold"))
        style.configure("Result.TLabel", background=PANEL, foreground=OK,
                        font=("Microsoft YaHei UI", 9, "bold"))
        style.configure("Warn.TLabel", background=PANEL, foreground=WARN,
                        font=("Microsoft YaHei UI", 8))
        style.configure("Accent.TButton", background=ACCENT, foreground="white",
                        borderwidth=0, padding=(12, 5))
        style.map("Accent.TButton", background=[("active", "#1e40af")])
        style.configure("TButton", padding=(8, 4))
        style.configure("TLabelframe", background=PANEL)
        style.configure("TLabelframe.Label", background=PANEL, foreground=INK)
        style.configure("TEntry", fieldbackground="white")
        style.configure("TCombobox", fieldbackground="white")
        style.configure("Treeview", background="white", fieldbackground="white",
                        rowheight=24)
        style.configure("Treeview.Heading", font=("Microsoft YaHei UI", 9, "bold"))
        style.configure("OTA.Horizontal.TProgressbar", thickness=14,
                        background=ACCENT, troughcolor="#e2e8f0")

    # ------------------------------------------------------------------ build
    def _build(self) -> None:
        header = tk.Frame(self, bg="#0b1220", height=86)
        header.pack(fill="x")
        header.pack_propagate(False)
        left = tk.Frame(header, bg="#0b1220")
        left.pack(side="left", padx=18, pady=10)
        tk.Label(left, text="DHRT100 · 触发调试 Demo", bg="#0b1220", fg="white",
                 font=("Microsoft YaHei UI", 16, "bold")).pack(anchor="w")
        tk.Label(left, text="单端口 → REFMEM 统一控制面 → 四板；也可直接使用应用层 SCPI",
                 bg="#0b1220", fg="#93a4bd",
                 font=("Microsoft YaHei UI", 9)).pack(anchor="w", pady=(2, 0))

        conn = tk.Frame(header, bg="#0b1220")
        conn.pack(side="right", padx=18)
        self.backend_box = ttk.Combobox(conn, textvariable=self.backend,
                                        values=("演示模式", "串口", "USB TMC (VISA)"),
                                        width=14, state="readonly")
        self.backend_box.grid(row=0, column=0, padx=4)
        self.backend_box.bind("<<ComboboxSelected>>", lambda _e: self._backend_changed())
        self.port_box = ttk.Combobox(conn, textvariable=self.port, width=34)
        self.port_box.grid(row=0, column=1, padx=4)
        self.refresh_button = ttk.Button(conn, text="刷新", width=5,
                                         command=self.refresh_ports)
        self.refresh_button.grid(row=0, column=2, padx=4)
        ttk.Entry(conn, textvariable=self.baud, width=8).grid(row=0, column=3, padx=4)
        self.connect_button = ttk.Button(conn, text="连接", style="Accent.TButton",
                                         command=self.toggle_connect)
        self.connect_button.grid(row=0, column=4, padx=(8, 0))
        tk.Label(conn, textvariable=self.conn_var, bg="#0b1220", fg="#93a4bd",
                 font=("Microsoft YaHei UI", 8)).grid(row=1, column=0, columnspan=5,
                                                     sticky="e", pady=(4, 0))

        self.notebook = ttk.Notebook(self)
        self.router = PageRouter(self.notebook, self.page_host)

        self.overview_page = OverviewPage(self.notebook, self)
        self.router.register_legacy("overview", "总览", self.overview_page)
        for module, title in (
            ("run", "运行控制"),
            ("trigger_config", "触发配置"),
            ("angle", "角度扫描"),
            ("switch", "开关"),
            ("sequence", "序列计划"),
        ):
            self.router.register_legacy(module, title, self._functions_page(module))
        self.readback_page = QueryPanel(self.notebook, self,
                                        catalog.queries_for("readback"))
        self.router.register_legacy("readback", "状态回读", self.readback_page)
        self.ack_page = AckPage(self.notebook, self)
        self.router.register_legacy("ack", "板卡与 ACK", self.ack_page)
        control_holder = ScrollFrame(self.notebook)
        self.control_page = ControlPage(control_holder.inner, self)
        self.control_page.pack(fill="x")
        control_holder.render = self.control_page.render
        self.router.register_legacy("control", "统一控制面", control_holder)
        self.ota_page = OtaPage(self.notebook, self)
        self.router.register_legacy("ota", "OTA", self.ota_page)
        self.tcp_page = TcpPage(self.notebook, self)
        self.router.register_legacy("tcp", "TCP 透传", self.tcp_page)
        self.diagnostics_page = DiagnosticsPage(self.notebook, self)
        self.router.register_legacy("diagnostics", "诊断", self.diagnostics_page)
        self.raw_page = RawPage(self.notebook, self)
        self.router.register_legacy("raw", "原始命令", self.raw_page)
        self.notebook.bind("<<NotebookTabChanged>>", self._tab_changed)

        log_frame = ttk.LabelFrame(self, text="通信日志", style="Card.TLabelframe",
                                   padding=(8, 4))
        self.log_frame = log_frame
        self.log_view = LogView(log_frame, limit=LOG_LIMIT)
        self.log_view.pack(fill="both", expand=True)
        bar = ttk.Frame(log_frame, style="Card.TFrame")
        bar.pack(fill="x", pady=(4, 0))
        ttk.Checkbutton(bar, text="自动滚动", variable=self.auto_scroll).pack(side="left")
        ttk.Button(bar, text="清空", command=self.clear_log).pack(side="left", padx=6)
        ttk.Button(bar, text="导出", command=self.export_log).pack(side="left")
        ttk.Label(bar, textvariable=self.status_var, style="Muted.TLabel").pack(
            side="right")

        self.status = ttk.Label(self, textvariable=self.mask_text, anchor="w",
                                style="Muted.TLabel")
        self.error_banner = ErrorBanner(self)
        # Pack the fixed bottom widgets before the expanding notebook, otherwise
        # pack gives the notebook all leftover space and squeezes the log.
        self.status.pack(side="bottom", fill="x", padx=12, pady=(0, 6))
        self.log_frame.pack(side="bottom", fill="x", padx=10, pady=(0, 4))
        self.notebook.pack(side="top", fill="both", expand=True, padx=10, pady=(10, 4))

    def _tab_changed(self, event=None) -> None:
        try:
            if self.router is not None:
                self.router.on_tab_changed(event)
        except Exception as exc:
            self.log(f"页面切换失败：{exc}", "ERR")
        if not self.connected:
            return
        try:
            selected = self.notebook.select()
            if selected == str(self.ack_page):
                self.ack_page.read_aggregate()
                self.ack_page.read_targets()
            self._sync_store()
        except (AttributeError, tk.TclError):
            pass

    def _functions_page(self, module: str) -> FunctionPage:
        page = FunctionPage(self.notebook)
        functions = catalog.functions_for(module)
        for function in functions:
            card = FunctionCard(page.inner, self, function)
            card.pack(fill="x", padx=10, pady=6)
            page.add_card(card)
            setattr(self, f"card_{function.key.replace('.', '_')}", card)
        return page

    # ------------------------------------------------------------- connection
    def _startup(self) -> None:
        self.refresh_ports()
        self._backend_changed()
        if self.backend.get() == "演示模式":
            self.connect()

    def _backend_changed(self) -> None:
        demo = self.backend.get() == "演示模式"
        state = "disabled" if demo else "normal"
        self.port_box.configure(state=state)
        self.refresh_button.configure(state=state)
        if demo:
            self.port.set("模拟协调板（4 板）")
        else:
            self.refresh_ports()

    def refresh_ports(self) -> None:
        if self.backend.get() == "USB TMC (VISA)":
            ports = device.available_visa_resources()
            self.port_box.configure(values=ports)
            if ports and self.port.get() not in ports:
                self.port.set(ports[0])
            if not ports:
                self.port.set("")
                self.status_var.set("未发现 USB TMC 资源（检查 NI-VISA / pyvisa）")
            return
        ports = device.available_ports()
        self.port_box.configure(values=ports)
        if ports and self.port.get() not in ports:
            self.port.set(ports[0])
        if not ports and self.backend.get() == "串口":
            self.port.set("")

    def toggle_connect(self) -> None:
        if self.connected:
            self.disconnect()
        else:
            self.connect()

    def connect(self) -> None:
        if self.connected:
            return
        backend = self.backend.get()
        if backend == "演示模式":
            transport = device.SimulatedTransport()
            label = "演示模式"
        elif backend == "USB TMC (VISA)":
            resource = self.port.get().strip()
            if not resource:
                self.dialogs.warning("未选择资源",
                                     "未发现 USB TMC 资源；请检查 NI-VISA / pyvisa")
                return
            transport = device.VisaTransport(resource, 2.0)
            label = resource
        else:
            port = device.port_device(self.port.get())
            if not port:
                self.dialogs.warning("未选择端口", "请选择串口")
                return
            try:
                baud = int(self.baud.get(), 10)
            except ValueError:
                self.dialogs.error("参数错误", "波特率必须是整数")
                return
            transport = device.SerialTransport(port, baud, 2.0)
            label = port
        try:
            transport.open()
        except Exception as exc:
            self.dialogs.error("连接失败", str(exc))
            return
        self.transport = transport
        self.dispatcher = device.Dispatcher(transport, self._on_result)
        self.dispatcher.start()
        self.connected = True
        self.connect_button.configure(text="断开")
        self.conn_var.set(f"{label} · 已连接")
        self.status_var.set(f"{label} · 就绪")
        self.log(f"已连接：{label}", "INFO")
        self._sync_store()
        self.send_command("*IDN?", "connect")

    def disconnect(self) -> None:
        if self.tcp_bridge is not None and hasattr(self, "tcp_page"):
            self.tcp_page.stop(wait=True)
        safe_to_close = True
        if self.dispatcher is not None:
            dispatcher = self.dispatcher
            self.dispatcher = None
            dispatcher.stop()
            dispatcher.join(timeout=2.5)
            if dispatcher.is_alive():
                safe_to_close = False
                self.log("Dispatcher 未在超时内退出；保留 transport 等待进程回收", "WARN")
        if self.transport is not None:
            if safe_to_close:
                try:
                    self.transport.close()
                except Exception:
                    pass
                self.transport = None
            else:
                self.log("Dispatcher 仍存活，跳过 transport.close（避免并发关闭）", "WARN")
        self.connected = False
        self.connect_button.configure(text="连接")
        self.conn_var.set("未连接")
        self.status_var.set("未连接")
        self.log("已断开", "INFO")
        self._sync_store()

    # ------------------------------------------------------------- command I/O
    def target_mask(self) -> int:
        return self.ack_page.target_mask() if hasattr(self, "ack_page") else 15

    def required_mask(self) -> int:
        target = self.target_mask()
        if not hasattr(self, "ack_page"):
            return target
        try:
            required = int(self.ack_page.required_var.get(), 10)
        except ValueError:
            return target
        if required < 0 or required & ~target:
            return target
        return required

    def refresh_previews(self) -> None:
        mask = self.target_mask()
        required = self.required_mask()
        self.mask_text.set(f"目标掩码 target={mask}  ·  必需掩码 required={required}  ·  "
                           f"单端口 REFMEM 统一控制面")
        for function in catalog.FUNCTIONS:
            card = getattr(self, f"card_{function.key.replace('.', '_')}", None)
            if card is not None:
                card._refresh()
        if hasattr(self, "control_page"):
            self.control_page.mask_label.configure(
                text=f"target={mask} required={required}")
        self._sync_store()

    def _sync_store(self) -> None:
        """Refresh the read-only UI vector from the legacy app state."""
        if not hasattr(self, "store"):
            return
        connected = bool(getattr(self, "connected", False))
        backend = self.backend.get()
        port = self.port.get()
        self.store.update_connection(
            backend=backend,
            port=port,
            connected=connected,
            tcp_listening=bool(getattr(self, "tcp_bridge", None)),
            transport_mode="single_port_refmem" if connected else "offline",
            occupied_ports=(port,) if connected and port else (),
            exit_criteria_visible=False,
        )
        self.store.update_mask(target_mask=self.target_mask(),
                               required_mask=self.required_mask())
        self.store.update_run(busy=bool(getattr(self, "ota_busy", False) or
                                       getattr(self, "pending_ack", False)))
        page_id = None
        if getattr(self, "router", None) is not None:
            page_id = self.router.current_page_id()
        self.store.update_ui(status_text=self.status_var.get(),
                             selected_item=page_id)

    def send_command(self, command: str, source: str = "") -> bool:
        if self.ota_busy and not source.startswith("ota"):
            # OTA owns the single command queue.  Drop non-OTA commands
            # silently and surface the reason in the status bar; opening a
            # modal here would recurse through periodic ACK refreshes.
            self.status_var.set("OTA 独占中：非 OTA 命令已丢弃")
            self.log("OTA 独占中，丢弃非 OTA 命令", "WARN")
            self._sync_store()
            return False
        if not self.connected or self.dispatcher is None:
            self.dialogs.warning("未连接", "请先连接协调板端口（或演示模式）")
            return False
        self.log(f"> {command}", "TX")
        self.dispatcher.submit(command, source)
        self.status_var.set(f"已排队：{command[:80]}")
        self._sync_store()
        return True

    def _on_result(self, result: device.Result) -> None:
        # Called from the worker thread: only hand the immutable result to the
        # Tk thread through the pump.
        self.pump.post(UiEvent("result", result))

    def _handle_ui_event(self, event: UiEvent) -> None:
        if event.kind == "result":
            self._handle_result(event.payload)
        elif event.kind == "tcp":
            kind, text = event.payload
            self.tcp_page.note(kind, text)
            if kind in ("TCP>", "TCP<", "INFO"):
                self.log(f"[TCP] {kind} {text}", "INFO")
        elif event.kind == "ota":
            kind, payload = event.payload
            self.ota_page.note(kind, payload)

    def report_callback_exception(self, _exc, value, _tb) -> None:
        self.errors.capture(value, "tk")

    def _on_ui_error(self, exc: BaseException, where: str) -> None:
        text = f"{where}: {type(exc).__name__}: {exc}"
        try:
            self.log(text, "ERR")
        except tk.TclError:
            pass
        try:
            if hasattr(self, "error_banner"):
                self.error_banner.show(text)
            if hasattr(self, "status_var"):
                self.status_var.set(text)
        except tk.TclError:
            pass

    def _handle_result(self, result: device.Result) -> None:
        if result.error:
            self.log(f"! {result.command} → {result.error}", "ERR")
            self.status_var.set(f"失败：{result.error}")
        else:
            self.log(f"< {result.response}", "RX")
            self.status_var.set(f"{result.command[:60]} → {result.response[:60]}  "
                                f"({result.elapsed_ms:.0f} ms)")
        self._sync_store()
        source = result.source
        if source == "connect":
            if not result.error:
                self.conn_var.set(f"已连接 · {result.response.strip(chr(34))}")
            return
        if source.startswith("function:"):
            key = source.split(":", 1)[1]
            card = getattr(self, f"card_{key.replace('.', '_')}", None)
            if card is not None:
                card.show_result(result.response, result.error)
            if not result.error and result.response.startswith("QUEUED"):
                self._begin_ack(result.response)
            return
        if source.startswith("query:"):
            key = source.split(":", 1)[1]
            self.readback_page.show_result(key, result.response, result.error)
            return
        if source.startswith("overview:"):
            if not result.error:
                self.overview_page.show(source, result.response)
            return
        if source.startswith("ack:status"):
            if not result.error:
                self.ack_page.show_status(result.response)
                self._ack_followup(result.response)
            return
        if source.startswith("ack:target:"):
            if not result.error:
                slot = int(source.rsplit(":", 1)[1])
                self.ack_page.show_target(slot, result.response)
            return
        if source == "ack:reply":
            if not result.error:
                self.ack_page.show_reply(result.response)
            return
        if source.startswith("catalog:"):
            if not result.error:
                self.control_page.show_catalog(int(source.split(":", 1)[1]),
                                               result.response)
            return
        if source == "control:owner":
            if not result.error:
                self.overview_page.show("overview:owner", result.response)
            return
        if source == "diag":
            self.diagnostics_page.add(result.command, result.response if not result.error
                                      else result.error)
            return
        if source.startswith("ota:"):
            self.ota_page.on_result(source, result.response, result.error)
            return
        if source == "raw":
            self.raw_page.add(result.command, result.response if not result.error
                              else result.error)
            return
        if source == "tcp":
            # An external TCP client shares the same command plane; surface its
            # ACK in the GUI instead of leaving the ACK page stale.
            if not result.error and result.response.startswith("QUEUED"):
                self._begin_ack(result.response)
            return
        if source.startswith("ctl:"):
            if not result.error and result.response.startswith("QUEUED"):
                self._begin_ack(result.response)

    def _begin_ack(self, response: str) -> None:
        try:
            seq = int(response.split(",", 1)[1])
        except (IndexError, ValueError):
            return
        self._ack_seq = seq
        self._ack_deadline = time.monotonic() + 8.0
        if self.pending_ack:
            return
        self.pending_ack = True
        self.ack_page.read_targets()
        self._sync_store()
        self.scheduler.after(180, self._poll_ack)

    def _poll_ack(self) -> None:
        if not self.pending_ack or not self.connected:
            return
        if time.monotonic() > self._ack_deadline:
            self.pending_ack = False
            self.log("ACK 等待超时（8 s）", "ERR")
            return
        self.send_command("SYSTem:CTRL:STATus?", "ack:status")

    def _ack_followup(self, response: str) -> None:
        values = _csv(response)
        if len(values) < 13:
            return
        try:
            state = int(values[2])
            ack = int(values[6])
            required = int(values[5])
        except ValueError:
            return
        terminal = state in (4, 5, 6) or (required and (ack & required) == required)
        if terminal:
            self.pending_ack = False
            self._sync_store()
            self.ack_page.read_targets()
            self.ack_page.read_reply()
        elif self.pending_ack:
            self.scheduler.after(300, self._poll_ack)

    # -------------------------------------------------------------------- log
    def log(self, text: str, level: str = "INFO") -> None:
        stamp = time.strftime("%H:%M:%S")
        self.log_view.append(f"[{stamp}] {text}", level,
                             scroll=self.auto_scroll.get())

    def clear_log(self) -> None:
        self.log_view.clear()

    def export_log(self) -> None:
        path = filedialog.asksaveasfilename(
            parent=self, defaultextension=".log",
            filetypes=[("Log", "*.log"), ("Text", "*.txt")],
            initialfile=time.strftime("trigger_demo_%Y%m%d_%H%M%S.log"))
        if not path:
            return
        Path(path).write_text(self.log_view.value(), encoding="utf-8")
        self.log(f"日志已导出：{path}", "INFO")

    # ------------------------------------------------------------------ close
    def close(self) -> None:
        if self._closing:
            return
        self._closing = True
        self._save_settings()
        try:
            self.pump.stop()
            if self.router is not None:
                self.router.close()
            self.disconnect()
        finally:
            self.destroy()


def _csv(text: str) -> list[str]:
    import csv
    try:
        return [item.strip() for item in next(csv.reader([text.strip()], strict=True))]
    except csv.Error:
        return []


def _run_headless(args) -> int:
    """Run only the TCP pass-through (no Tk window)."""
    if args.visa:
        transport = device.VisaTransport(args.visa, 2.0)
    elif args.serial:
        transport = device.SerialTransport(args.serial, args.baud, 2.0)
    else:
        transport = device.SimulatedTransport()
    try:
        transport.open()
    except Exception as exc:
        print(f"连接失败：{exc}", file=sys.stderr)
        return 2
    dispatcher = device.Dispatcher(transport, lambda _result: None)
    dispatcher.start()
    bridge = device.TcpBridge(
        dispatcher, args.tcp_host, args.tcp_port,
        on_event=lambda kind, text: print(f"{kind} {text}", flush=True),
        timeout_s=args.timeout)
    try:
        bridge.start()
    except OSError as exc:
        print(f"TCP 启动失败：{exc}", file=sys.stderr)
        dispatcher.stop()
        transport.close()
        return 2
    print(f"TCP 透传就绪：tcp://{bridge.host}:{bridge.actual_port}  "
          f"({args.visa or args.serial or '模拟四板'})", flush=True)
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        bridge.stop()
        dispatcher.stop()
        transport.close()
    return 0


def main(argv=None) -> int:
    import argparse
    parser = argparse.ArgumentParser(description=APP_TITLE)
    parser.add_argument("--headless", action="store_true",
                        help="只启动 TCP 透传，不打开图形界面")
    parser.add_argument("--demo", action="store_true", help="图形界面使用模拟四板")
    parser.add_argument("--serial", default="", help="串口端口，例如 COM5")
    parser.add_argument("--visa", default="",
                        help="USB TMC 资源，例如 USB0::0x2E8A::...::INSTR")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--tcp-host", default="127.0.0.1")
    parser.add_argument("--tcp-port", type=int, default=5025)
    parser.add_argument("--timeout", type=float, default=3.0,
                        help="TCP 单条命令响应超时（秒）")
    args = parser.parse_args(argv)

    if args.headless:
        return _run_headless(args)

    app = DemoApp()
    if args.visa:
        app.backend.set("USB TMC (VISA)")
        app.port.set(args.visa)
        app.after(250, app.connect)
    elif args.serial:
        app.backend.set("串口")
        app.port.set(args.serial)
        app.after(250, app.connect)
    else:
        app.backend.set("演示模式")
    if args.tcp_port:
        app.tcp_page.port.set(str(args.tcp_port))
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

