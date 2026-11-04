"""Page-level error boundary with rate limiting."""

from __future__ import annotations

import time
import traceback
from typing import Callable


class ErrorBoundary:
    def __init__(self, handler: Callable[[BaseException, str], None],
                 cooldown_s: float = 5.0, clock: Callable[[], float] = time.monotonic) -> None:
        self._handler = handler
        self._cooldown_s = cooldown_s
        self._clock = clock
        self._last: dict[str, float] = {}

    def capture(self, exc: BaseException, where: str = "ui") -> bool:
        key = f"{where}:{type(exc).__name__}:{exc}"
        now = self._clock()
        if now - self._last.get(key, -self._cooldown_s) < self._cooldown_s:
            return False
        self._last[key] = now
        try:
            self._handler(exc, where)
        except Exception:
            # Error handling must never take down the UI loop.
            traceback.print_exc()
        return True


def install_tk_error_handler(root, boundary: ErrorBoundary) -> None:
    def report(exc, value, tb) -> None:
        boundary.capture(value, "tk")

    root.report_callback_exception = report
