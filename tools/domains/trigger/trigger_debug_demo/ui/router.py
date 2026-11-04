"""Notebook-backed page router with legacy adapters and navigation guards."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .page import LegacyPageAdapter, NavigationDecision, Page, PageHost, PageSpec


@dataclass
class _Entry:
    page_id: str
    title: str
    spec: PageSpec | None
    page: Any
    widget: Any
    built: bool


class PageRouter:
    def __init__(self, notebook, host: PageHost) -> None:
        self._notebook = notebook
        self._host = host
        self._entries: dict[str, _Entry] = {}
        self._order: list[str] = []
        self._current: str | None = None
        self._unsubscribers: list[Any] = []
        host.router = self

    def register_legacy(self, page_id: str, title: str, widget,
                        capabilities: frozenset[str] = frozenset()) -> LegacyPageAdapter:
        adapter = LegacyPageAdapter(widget, title)
        self._entries[page_id] = _Entry(page_id, title, None, adapter, widget, True)
        self._order.append(page_id)
        self._notebook.add(widget, text=title)
        if callable(getattr(widget, "render", None)):
            adapter.render(self._host.store.snapshot())
            self._unsubscribers.append(self._host.store.subscribe(adapter.render))
        return adapter

    def register(self, spec: PageSpec) -> None:
        if spec.id in self._entries:
            raise ValueError(f"duplicate page id: {spec.id}")
        self._entries[spec.id] = _Entry(spec.id, spec.title, spec, None, None, False)
        self._order.append(spec.id)

    def show(self, page_id: str) -> NavigationDecision:
        entry = self._entries.get(page_id)
        if entry is None:
            return NavigationDecision(False, f"unknown page: {page_id}")
        if page_id == self._current:
            return NavigationDecision(True)
        if self._current is not None:
            decision = self._page(self._current).can_close()
            if not decision.allowed:
                return decision
            self._page(self._current).on_hide()
        if not entry.built:
            self._build(entry)
        self._current = page_id
        self._notebook.select(entry.widget)
        self._page(page_id).on_show(self._host)
        return NavigationDecision(True)

    def on_tab_changed(self, _event=None) -> None:
        current = self.current_page_id()
        if current is None or current == self._current:
            return
        if self._current is not None:
            self._page(self._current).on_hide()
        self._current = current
        self._page(current).on_show(self._host)

    def current_page_id(self) -> str | None:
        selected = str(self._notebook.select())
        for page_id, entry in self._entries.items():
            if entry.widget is not None and str(entry.widget) == selected:
                return page_id
        return None

    def page(self, page_id: str) -> Any:
        return self._page(page_id)

    def pages(self) -> tuple[str, ...]:
        return tuple(self._order)

    def tabs(self) -> tuple[str, ...]:
        return tuple(self._notebook.tabs())

    def close(self) -> None:
        for unsubscribe in self._unsubscribers:
            try:
                unsubscribe()
            except Exception:
                pass
        self._unsubscribers.clear()
        for page_id in reversed(self._order):
            page = self._page(page_id)
            try:
                page.cancel()
            except Exception:
                pass
            try:
                page.dispose()
            except Exception:
                pass

    def _build(self, entry: _Entry) -> None:
        spec = entry.spec
        if spec is None:
            return
        page = spec.factory(self._host)
        page.build(self._notebook)
        entry.page = page
        entry.widget = page
        entry.built = True
        self._notebook.add(page, text=spec.title)

    def _page(self, page_id: str):
        return self._entries[page_id].page
