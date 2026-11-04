"""Page contract and the legacy adapter used by the strangler migration."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable, Protocol

from .dialogs import DialogPort
from .scheduler import SchedulerPort
from .state import ConsoleStore, ConsoleUiVector


@dataclass(frozen=True)
class NavigationDecision:
    allowed: bool
    reason: str = ""


@dataclass(frozen=True)
class PageSpec:
    id: str
    title: str
    factory: Callable[["PageHost"], "Page"]
    capabilities: frozenset[str] = frozenset()
    lazy: bool = True
    shortcut: str | None = None


class Page(Protocol):
    def build(self, parent) -> None:
        ...

    def render(self, vector: ConsoleUiVector) -> None:
        ...

    def on_show(self, host: "PageHost") -> None:
        ...

    def on_hide(self) -> None:
        ...

    def can_close(self) -> NavigationDecision:
        ...

    def cancel(self) -> None:
        ...

    def dispose(self) -> None:
        ...


@dataclass
class PageHost:
    store: ConsoleStore
    scheduler: SchedulerPort
    dialogs: DialogPort
    router: Any = None
    context: dict[str, Any] = field(default_factory=dict)


class LegacyPageAdapter:
    """Expose an existing ``ttk.Frame`` through the new Page lifecycle.

    The legacy widget keeps running its own code; this adapter only normalizes
    lifecycle and guard behavior so the router can treat it like a Page.
    """

    def __init__(self, widget, title: str) -> None:
        self.widget = widget
        self.title = title

    def build(self, parent) -> None:
        return None

    def render(self, vector: ConsoleUiVector) -> None:
        callback = getattr(self.widget, "render", None)
        if callable(callback):
            callback(vector)

    def on_show(self, host: PageHost) -> None:
        callback = getattr(self.widget, "on_show", None)
        if callable(callback):
            callback()

    def on_hide(self) -> None:
        callback = getattr(self.widget, "on_hide", None)
        if callable(callback):
            callback()

    def can_close(self) -> NavigationDecision:
        callback = getattr(self.widget, "can_close", None)
        if not callable(callback):
            return NavigationDecision(True)
        result = callback()
        if isinstance(result, NavigationDecision):
            return result
        return NavigationDecision(bool(result))

    def cancel(self) -> None:
        callback = getattr(self.widget, "cancel", None)
        if callable(callback):
            callback()

    def dispose(self) -> None:
        callback = getattr(self.widget, "dispose", None)
        if callable(callback):
            callback()
