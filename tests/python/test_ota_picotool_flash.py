"""Offline checks for the picotool BOOTSEL helper used by OTA_workflow."""

from __future__ import annotations

import importlib.util
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "picotool_flash" / "picotool_flash.py"


def _module():
    spec = importlib.util.spec_from_file_location("picotool_flash", MODULE_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_wait_bootsel_probes_only_the_selected_target(monkeypatch):
    module = _module()
    calls = []

    def fake_run(picotool, args, timeout):
        calls.append((list(args), timeout))
        return subprocess.CompletedProcess(
            [str(picotool), *args], 0, "Boot type: BOOTSEL\n", ""
        )

    monkeypatch.setattr(module, "run", fake_run)
    text = module.wait_bootsel(Path("picotool"), ["--ser", "UID"], 1.0, 5.0)

    assert text is not None
    assert calls
    assert calls[0][0] == ["info", "-a", "--ser", "UID"]
    assert calls[0][1] == 5.0


def test_wait_bootsel_does_not_accept_another_board(monkeypatch):
    module = _module()

    def fake_run(picotool, args, timeout):
        return subprocess.CompletedProcess(
            [str(picotool), *args],
            1,
            "No accessible RP-series devices in BOOTSEL mode were found.\n",
            "",
        )

    monkeypatch.setattr(module, "run", fake_run)
    assert module.wait_bootsel(Path("picotool"), ["--ser", "UID"], 0.1, 1.0) is None


def test_bootsel_timeout_is_bounded_by_command_timeout():
    source = MODULE_PATH.read_text(encoding="utf-8")
    assert "--bootsel-timeout" in source
    assert "wait_bootsel(picotool, selection, args.bootsel_timeout" in source
    workflow = (ROOT / "tools" / "ota" / "OTA_workflow.py").read_text(encoding="utf-8")
    assert "\"--bootsel-timeout\", str(PICOTOOL_BOOTSEL_TIMEOUT_S)" in workflow
    assert "timeout=PICOTOOL_FLASH_WALL_TIMEOUT_S" in workflow
