"""Bounded UI event pump: workers post, the Tk thread drains."""

from __future__ import annotations

import queue
from dataclasses import dataclass
from typing import Any, Callable

from .scheduler import SchedulerPort


@dataclass(frozen=True)
class UiEvent:
    kind: str
    payload: Any = None


class UiPump:
    def __init__(self, scheduler: SchedulerPort, handler: Callable[[UiEvent], None],
                 interval_ms: int = 30, max_events: int = 256,
                 max_queue: int = 4096) -> None:
        self._scheduler = scheduler
        self._handler = handler
        self._interval_ms = interval_ms
        self._max_events = max_events
        self._queue: queue.Queue[UiEvent] = queue.Queue(maxsize=max_queue)
        self._token: str | None = None
        self._running = False
        self._dropped = 0

    @property
    def dropped(self) -> int:
        return self._dropped

    def post(self, event: UiEvent) -> bool:
        try:
            self._queue.put_nowait(event)
            return True
        except queue.Full:
            if event.kind in ("log", "progress", "state_changed"):
                self._dropped += 1
                return False
            # Preserve terminal results/errors rather than dropping them.
            try:
                self._queue.put(event, timeout=0.5)
                return True
            except queue.Full:
                self._dropped += 1
                return False

    def start(self) -> None:
        if self._running:
            return
        self._running = True
        self._token = self._scheduler.after(self._interval_ms, self._drain)

    def stop(self) -> None:
        self._running = False
        if self._token is not None:
            self._scheduler.cancel(self._token)
            self._token = None
        while True:
            try:
                self._queue.get_nowait()
            except queue.Empty:
                break

    def _drain(self) -> None:
        if not self._running:
            return
        for _ in range(self._max_events):
            try:
                event = self._queue.get_nowait()
            except queue.Empty:
                break
            try:
                self._handler(event)
            except Exception:
                # A view error must not stop the pump.
                continue
        self._token = self._scheduler.after(self._interval_ms, self._drain)


class PumpSink:
    """Queue-like adapter for legacy producers that call ``put``."""

    def __init__(self, pump: UiPump, kind: str) -> None:
        self._pump = pump
        self._kind = kind

    def put(self, payload) -> None:
        self._pump.post(UiEvent(self._kind, payload))

    def put_nowait(self, payload) -> None:
        self.put(payload)

