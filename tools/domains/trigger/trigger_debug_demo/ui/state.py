"""Immutable UI state for the debug console.

The store is deliberately small: pages read a snapshot, presenters/workflow
services update slices.  No Tk variable is a source of truth.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from threading import RLock
from typing import Callable


@dataclass(frozen=True)
class ConnectionSlice:
    backend: str = ""
    port: str = ""
    connected: bool = False
    tcp_listening: bool = False
    transport_mode: str = "offline"
    occupied_ports: tuple[str, ...] = ()
    exit_criteria_visible: bool = False


@dataclass(frozen=True)
class MaskSlice:
    target_mask: int = 15
    required_mask: int = 15


@dataclass(frozen=True)
class RunSlice:
    busy: bool = False
    workflow_id: str | None = None
    workflow_step: str | None = None
    workflow_state: str | None = None
    error_code: int = 0


@dataclass(frozen=True)
class AckSlice:
    seq: str = "-"
    state: str = "-"
    ack: str = "-"
    nack: str = "-"
    timeout: str = "-"
    reason: str = "-"


@dataclass(frozen=True)
class UiSlice:
    revision: int = 0
    status_text: str = ""
    selected_item: str | None = None
    dirty: bool = False


@dataclass(frozen=True)
class ConsoleUiVector:
    connection: ConnectionSlice = ConnectionSlice()
    mask: MaskSlice = MaskSlice()
    run: RunSlice = RunSlice()
    ack: AckSlice = AckSlice()
    ui: UiSlice = UiSlice()


class ConsoleStore:
    """Thread-safe, single-writer state store.

    Callbacks run on the thread that performs the update.  UI callers should
    update from the Tk thread, or post ``state_changed`` through the UiPump.
    """

    def __init__(self, initial: ConsoleUiVector | None = None) -> None:
        self._lock = RLock()
        self._vector = initial or ConsoleUiVector()
        self._subscribers: list[tuple[Callable[[ConsoleUiVector], None], object]] = []

    def snapshot(self) -> ConsoleUiVector:
        with self._lock:
            return self._vector

    def subscribe(
        self,
        callback: Callable[[ConsoleUiVector], None],
        selector: Callable[[ConsoleUiVector], object] | None = None,
    ) -> Callable[[], None]:
        entry = (callback, selector)
        with self._lock:
            self._subscribers.append(entry)

        def unsubscribe() -> None:
            with self._lock:
                if entry in self._subscribers:
                    self._subscribers.remove(entry)

        return unsubscribe

    def update_connection(self, **changes) -> ConsoleUiVector:
        return self._update("connection", ConnectionSlice, changes)

    def update_mask(self, **changes) -> ConsoleUiVector:
        return self._update("mask", MaskSlice, changes)

    def update_run(self, **changes) -> ConsoleUiVector:
        return self._update("run", RunSlice, changes)

    def update_ack(self, **changes) -> ConsoleUiVector:
        return self._update("ack", AckSlice, changes)

    def update_ui(self, **changes) -> ConsoleUiVector:
        return self._update("ui", UiSlice, changes)

    def set_status(self, text: str, **changes) -> ConsoleUiVector:
        return self.update_ui(status_text=text, **changes)

    def _update(self, name: str, kind, changes: dict) -> ConsoleUiVector:
        with self._lock:
            current = getattr(self._vector, name)
            if name == "ui":
                updated = replace(current, revision=current.revision + 1, **changes)
                vector = replace(self._vector, ui=updated)
            else:
                updated = replace(current, **changes)
                vector = replace(
                    self._vector,
                    **{
                        name: updated,
                        "ui": replace(self._vector.ui,
                                       revision=self._vector.ui.revision + 1),
                    },
                )
            self._vector = vector
            subscribers = list(self._subscribers)
        for callback, selector in subscribers:
            try:
                if selector is None or selector(vector) != selector(current):
                    callback(vector)
            except Exception:
                # A broken view must not break the writer.
                continue
        return vector
