"""The DPLL60 workflow verifies the firmware that is flashed right now.

It neither builds nor flashes, so its build expectation must come from the
boards and never from build records: an interrupted acceptance leaves the
provisional receipt behind, and record archaeology then refused the reset while
all four boards already ran the current build (measured 2026-10-14).
"""
import importlib.util
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools/vdc_priority_trace/DPLL60_workflow.py"


def load_module():
    spec = importlib.util.spec_from_file_location("dpll60_workflow", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_ctx():
    return SimpleNamespace(log=lambda message: None, failures=[], config={})


def test_expected_build_comes_from_the_online_boards(monkeypatch):
    module = load_module()
    monkeypatch.setattr(module, "online_builds",
                        lambda ctx: {board: "20261014032934" for board in module.BENCH_BOARDS})
    monkeypatch.setattr(module, "record_build",
                        lambda ctx: (_ for _ in ()).throw(
                            AssertionError("record fallback must not be consulted")))
    assert module.expected_build(make_ctx()) == ("20261014032934", "online boards")


def test_mixed_online_builds_are_reported_not_silently_compared(monkeypatch):
    module = load_module()
    boards = list(module.BENCH_BOARDS)
    monkeypatch.setattr(module, "online_builds", lambda ctx: {
        boards[0]: "20261014032934", boards[1]: "20261013132342"})
    build, source = module.expected_build(make_ctx())
    assert source == "online boards (mixed)"
    assert "20261013132342" in build and "20261014032934" in build


def test_record_fallback_only_when_the_boards_cannot_be_read(monkeypatch):
    module = load_module()
    monkeypatch.setattr(module, "online_builds", lambda ctx: {})
    monkeypatch.setattr(module, "record_build", lambda ctx: ("20261014020356", "receipt"))
    assert module.expected_build(make_ctx()) == ("20261014020356", "receipt")
