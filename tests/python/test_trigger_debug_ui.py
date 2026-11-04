#!/usr/bin/env python3
"""Host tests for the Tk application framework used by the debug demo."""

from __future__ import annotations

from tools.domains.trigger.trigger_debug_demo.ui import (
    ConsoleStore,
    ConsoleUiVector,
    ErrorBoundary,
    FakeDialogService,
    FakeScheduler,
    NavigationDecision,
    PageHost,
    PageRouter,
    PumpSink,
    StepSpec,
    UiEvent,
    UiPump,
    WorkflowNotResumable,
    WorkflowRunner,
    WorkflowSpec,
    load_settings,
    save_settings,
)


class _Widget:
    def __init__(self, name: str) -> None:
        self.name = name
        self.disposed = False
        self.cancelled = False
        self.visible = False

    def __str__(self) -> str:
        return self.name

    def on_show(self) -> None:
        self.visible = True

    def on_hide(self) -> None:
        self.visible = False

    def cancel(self) -> None:
        self.cancelled = True

    def dispose(self) -> None:
        self.disposed = True


class _Notebook:
    def __init__(self) -> None:
        self._tabs: list[str] = []
        self._selected = ""

    def add(self, widget, text: str) -> None:
        self._tabs.append(str(widget))
        if not self._selected:
            self._selected = str(widget)

    def select(self, widget) -> None:
        self._selected = str(widget)

    def tabs(self) -> tuple[str, ...]:
        return tuple(self._tabs)

    def current(self) -> str:
        return self._selected


def _host() -> PageHost:
    return PageHost(ConsoleStore(), FakeScheduler(), FakeDialogService())


def test_store_slices_revision_and_unsubscribe() -> None:
    store = ConsoleStore()
    seen: list[int] = []
    unsubscribe = store.subscribe(lambda vector: seen.append(vector.ui.revision))

    store.update_connection(connected=True, port="COM1")
    store.update_mask(target_mask=3, required_mask=1)
    vector = store.update_run(busy=True, workflow_state="RUNNING")
    store.update_ack(seq="101", state="4 ACKED", ack="3")

    assert isinstance(vector, ConsoleUiVector)
    assert vector.connection.connected is True
    assert vector.connection.port == "COM1"
    assert (vector.mask.target_mask, vector.mask.required_mask) == (3, 1)
    assert vector.run.busy is True
    assert store.snapshot().ui.revision == 4
    assert store.snapshot().ack.seq == "101"
    assert store.snapshot().ack.state == "4 ACKED"
    assert seen == [1, 2, 3, 4]

    unsubscribe()
    store.set_status("idle")
    assert seen == [1, 2, 3, 4]


def test_fake_scheduler_cancel_and_advance() -> None:
    scheduler = FakeScheduler()
    seen: list[str] = []
    token = scheduler.after(100, seen.append, "a")
    scheduler.after(200, seen.append, "b")
    scheduler.cancel(token)
    scheduler.advance(0.25)
    assert seen == ["b"]
    assert scheduler.pending() == 0


def test_pump_is_bounded_and_keeps_terminal_events() -> None:
    scheduler = FakeScheduler()
    seen: list[UiEvent] = []
    pump = UiPump(scheduler, seen.append, interval_ms=30, max_events=2, max_queue=8)
    pump.start()
    for index in range(7):
        pump.post(UiEvent("log", index))
    pump.post(UiEvent("error", "terminal"))
    for _ in range(4):
        scheduler.advance(0.031)
    assert seen[-1].kind == "error"
    pump.stop()

    dropped_scheduler = FakeScheduler()
    dropped = UiPump(dropped_scheduler, lambda event: None, max_queue=2)
    for index in range(3):
        dropped.post(UiEvent("log", index))
    assert dropped.dropped == 1
    dropped.stop()


def test_pump_sink_adapts_legacy_queue_producers() -> None:
    scheduler = FakeScheduler()
    seen: list[UiEvent] = []
    pump = UiPump(scheduler, seen.append, interval_ms=10)
    sink = PumpSink(pump, "ota")
    sink.put(("progress", 1))
    pump.start()
    scheduler.advance(0.011)
    assert seen[0].kind == "ota"
    assert seen[0].payload == ("progress", 1)
    pump.stop()


def test_router_guards_navigation_and_disposes_legacy_pages() -> None:
    notebook = _Notebook()
    host = _host()
    router = PageRouter(notebook, host)

    first = _Widget("first")
    second = _Widget("second")
    first.can_close = lambda: NavigationDecision(False, "workflow running")
    router.register_legacy("first", "First", first)
    router.register_legacy("second", "Second", second)

    assert router.show("first").allowed is True
    decision = router.show("second")
    assert decision.allowed is False
    assert decision.reason == "workflow running"
    assert notebook.current() == "first"

    first.can_close = lambda: NavigationDecision(True)
    assert router.show("second").allowed is True
    assert first.visible is False
    assert second.visible is True

    router.close()
    assert first.cancelled is True
    assert second.disposed is True


def test_router_lazy_page_builds_on_first_show() -> None:
    notebook = _Notebook()
    router = PageRouter(notebook, _host())

    class Page:
        def __init__(self) -> None:
            self.built = False
            self.shown = False
            self.hidden = False

        def build(self, parent) -> None:
            self.built = True

        def render(self, vector) -> None:
            pass

        def on_show(self, host) -> None:
            self.shown = True

        def on_hide(self) -> None:
            self.hidden = True

        def can_close(self):
            return NavigationDecision(True)

        def cancel(self) -> None:
            pass

        def dispose(self) -> None:
            pass

        def __str__(self) -> str:
            return "lazy"

    page = Page()
    from tools.domains.trigger.trigger_debug_demo.ui import PageSpec

    router.register(PageSpec("lazy", "Lazy", lambda host: page))
    assert page.built is False
    assert router.show("lazy").allowed is True
    assert page.built is True
    assert page.shown is True
    router.close()


def test_router_renders_store_updates_into_legacy_widget() -> None:
    notebook = _Notebook()
    host = _host()
    router = PageRouter(notebook, host)

    class RenderWidget(_Widget):
        def __init__(self) -> None:
            super().__init__("render")
            self.last = None

        def render(self, vector) -> None:
            self.last = vector

    widget = RenderWidget()
    router.register_legacy("render", "Render", widget)
    host.store.update_connection(connected=True, port="COM9")
    assert widget.last is not None
    assert widget.last.connection.connected is True
    assert widget.last.connection.port == "COM9"
    router.close()


def test_workflow_persists_and_resumes_safe_step(tmp_path) -> None:
    attempts = {"two": 0}

    def one(ctx) -> None:
        ctx.checkpoint(marker="one")

    def two(ctx) -> None:
        attempts["two"] += 1
        if attempts["two"] == 1:
            raise RuntimeError("transient")

    spec = WorkflowSpec("test.flow", "1", (
        StepSpec("one", "One", one),
        StepSpec("two", "Two", two),
    ))
    runner = WorkflowRunner(spec, tmp_path)
    try:
        runner.start({"input": 1})
    except RuntimeError:
        pass
    state = runner.latest()
    assert state is not None
    assert state.status == "FAILED"
    assert state.cursor == 1
    assert state.active_step == "two"

    resumed = runner.resume(state.run_id)
    assert resumed.status == "SUCCEEDED"
    assert attempts["two"] == 2
    assert (tmp_path / f"test.flow-{state.run_id}.json").is_file()


def test_workflow_refuses_unsafe_resume(tmp_path) -> None:
    def fail(_ctx) -> None:
        raise RuntimeError("boom")

    spec = WorkflowSpec("test.unsafe", "1", (
        StepSpec("unsafe", "Unsafe", fail, resume_safe=False),
    ))
    runner = WorkflowRunner(spec, tmp_path)
    try:
        runner.start({})
    except RuntimeError:
        pass
    state = runner.latest()
    assert state is not None
    try:
        runner.resume(state.run_id)
    except WorkflowNotResumable:
        pass
    else:
        raise AssertionError("unsafe resume must fail closed")


def test_workflow_cancel_marks_cancelled(tmp_path) -> None:
    seen: list[str] = []

    def step(ctx) -> None:
        seen.append("step")
        ctx.checkpoint(running=True)

    spec = WorkflowSpec("test.cancel", "1", (StepSpec("one", "One", step),))
    runner = WorkflowRunner(spec, tmp_path)
    runner.cancel()
    state = runner.start({})
    assert state.status == "CANCELLED"
    assert seen == []


def test_settings_schema_atomic_write_and_bad_backup(tmp_path) -> None:
    path = tmp_path / "settings.json"
    save_settings(path, {"backend": "串口", "baud": 115200,
                         "tcp_host": "127.0.0.1", "tcp_port": "5025"})
    loaded = load_settings(path)
    assert loaded["backend"] == "串口"
    assert loaded["baud"] == "115200"
    assert loaded["tcp_port"] == "5025"

    path.write_text("[1, 2, 3]", encoding="utf-8")
    recovered = load_settings(path)
    assert recovered["backend"] == "演示模式"
    assert (tmp_path / "settings.json.bad").is_file()


def test_error_boundary_rate_limits_same_error() -> None:
    now = [0.0]
    seen: list[str] = []
    boundary = ErrorBoundary(lambda exc, where: seen.append(f"{where}:{exc}"),
                             cooldown_s=5.0, clock=lambda: now[0])
    error = RuntimeError("boom")
    assert boundary.capture(error, "page") is True
    assert boundary.capture(error, "page") is False
    now[0] = 6.0
    assert boundary.capture(error, "page") is True
    assert seen == ["page:boom", "page:boom"]


