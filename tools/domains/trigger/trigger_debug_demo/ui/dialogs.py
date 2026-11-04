"""Dialog port so pages never own messagebox behavior."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol

from tkinter import messagebox


class DialogPort(Protocol):
    def confirm(self, title: str, message: str, danger: bool = False) -> bool:
        ...

    def error(self, title: str, message: str) -> None:
        ...

    def warning(self, title: str, message: str) -> None:
        ...

    def info(self, title: str, message: str) -> None:
        ...


class TkDialogService:
    def __init__(self, parent) -> None:
        self._parent = parent

    def confirm(self, title: str, message: str, danger: bool = False) -> bool:
        return bool(messagebox.askyesno(title, message, parent=self._parent,
                                        icon="warning" if danger else "question"))

    def error(self, title: str, message: str) -> None:
        messagebox.showerror(title, message, parent=self._parent)

    def warning(self, title: str, message: str) -> None:
        messagebox.showwarning(title, message, parent=self._parent)

    def info(self, title: str, message: str) -> None:
        messagebox.showinfo(title, message, parent=self._parent)


@dataclass
class FakeDialogService:
    answer: bool = False
    calls: list[tuple[str, str, str, bool]] = field(default_factory=list)

    def confirm(self, title: str, message: str, danger: bool = False) -> bool:
        self.calls.append(("confirm", title, message, danger))
        return self.answer

    def error(self, title: str, message: str) -> None:
        self.calls.append(("error", title, message, False))

    def warning(self, title: str, message: str) -> None:
        self.calls.append(("warning", title, message, False))

    def info(self, title: str, message: str) -> None:
        self.calls.append(("info", title, message, False))
