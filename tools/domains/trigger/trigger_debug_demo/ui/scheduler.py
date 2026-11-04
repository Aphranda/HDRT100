"""Tk scheduler port and a deterministic fake for tests."""

from __future__ import annotations

import time
from typing import Any, Callable, Protocol


class SchedulerPort(Protocol):
    def after(self, milliseconds: int, callback: Callable[..., Any], *args) -> str:
        ...

    def cancel(self, token: str) -> None:
        ...

    def now(self) -> float:
        ...


class TkScheduler:
    def __init__(self, widget) -> None:
        self._widget = widget

    def after(self, milliseconds: int, callback: Callable[..., Any], *args) -> str:
        return self._widget.after(milliseconds, callback, *args)

    def cancel(self, token: str) -> None:
        try:
            self._widget.after_cancel(token)
        except Exception:
            pass

    def now(self) -> float:
        return time.monotonic()


class FakeScheduler:
    """Run ``after`` callbacks synchronously during ``advance``."""

    def __init__(self) -> None:
        self._clock = 0.0
        self._seq = 0
        self._jobs: dict[str, tuple[float, int, Callable[..., Any], tuple]] = {}

    def after(self, milliseconds: int, callback: Callable[..., Any], *args) -> str:
        self._seq += 1
        token = f"fake-{self._seq}"
        self._jobs[token] = (self._clock + milliseconds / 1000.0, self._seq, callback, args)
        return token

    def cancel(self, token: str) -> None:
        self._jobs.pop(token, None)

    def now(self) -> float:
        return self._clock

    def advance(self, seconds: float) -> None:
        target = self._clock + max(0.0, seconds)
        while True:
            due = [item for item in self._jobs.items() if item[1][0] <= target]
            if not due:
                break
            token, (when, _seq, callback, args) = min(due, key=lambda item: (item[1][0], item[1][1]))
            self._jobs.pop(token, None)
            self._clock = when
            callback(*args)
        self._clock = target

    def pending(self) -> int:
        return len(self._jobs)
