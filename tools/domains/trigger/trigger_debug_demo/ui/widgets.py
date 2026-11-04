"""Small reusable Tk widgets; intentionally minimal."""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk


class ErrorBanner(ttk.Frame):
    """Inline, dismissible error strip; never opens a modal."""

    def __init__(self, master, on_dismiss=None) -> None:
        super().__init__(master, style="Card.TFrame", padding=(8, 4))
        self._on_dismiss = on_dismiss
        self._label = ttk.Label(self, text="", style="Warn.TLabel",
                                justify="left", wraplength=1100)
        self._label.pack(side="left", fill="x", expand=True)
        ttk.Button(self, text="关闭", width=6, command=self._dismiss).pack(
            side="right")

    def show(self, text: str) -> None:
        self._label.configure(text=text)
        if not self.winfo_manager():
            self.pack(fill="x", padx=10, pady=(0, 4))

    def clear(self) -> None:
        self._label.configure(text="")
        if self.winfo_manager():
            self.pack_forget()

    def _dismiss(self) -> None:
        self.clear()
        if self._on_dismiss is not None:
            self._on_dismiss()


class LogView(ttk.Frame):
    """Bounded read-only log view with a hard line cap."""

    def __init__(self, master, limit: int = 2500, **kwargs) -> None:
        super().__init__(master, style="Card.TFrame", **kwargs)
        self.limit = limit
        self._lines = 0
        self.text = tk.Text(self, height=7, font=("Consolas", 9),
                            background="#0f172a", foreground="#dbeafe",
                            insertbackground="white", relief="flat")
        self.text.pack(fill="both", expand=True)
        self.text.configure(state="disabled")
        for tag, color in (("TX", "#93c5fd"), ("RX", "#86efac"),
                           ("ERR", "#fca5a5"), ("INFO", "#cbd5e1"),
                           ("WARN", "#fbbf24")):
            self.text.tag_configure(tag, foreground=color)

    def append(self, text: str, level: str = "INFO", scroll: bool = True) -> None:
        self.text.configure(state="normal")
        self.text.insert("end", text + "\n", level)
        self._lines += 1
        if self._lines > self.limit:
            self.text.delete("1.0", "100.0")
            self._lines -= 99
        self.text.configure(state="disabled")
        if scroll:
            self.text.see("end")

    def clear(self) -> None:
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.configure(state="disabled")
        self._lines = 0

    def value(self) -> str:
        return self.text.get("1.0", "end")
