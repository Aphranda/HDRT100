from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.ota.ota_stream_send.tdma_night import (
    RelayNightPlan,
    build_relay_command,
    run,
    to_night_plan,
)


def plan(tmp_path: Path, **overrides) -> RelayNightPlan:
    values = dict(port="COM5", package=Path("out/pkg.pkg"), relay_node=1,
                  out_dir=tmp_path / "night")
    values.update(overrides)
    return RelayNightPlan(**values)


def test_relay_command_carries_node_and_package(tmp_path: Path) -> None:
    night = to_night_plan(plan(tmp_path, session_id=0x1234))
    command = build_relay_command(plan(tmp_path, session_id=0x1234))
    assert str(night.command) == str(command)
    text = " ".join(command)
    assert "--relay-node 1" in text
    assert "--package" in text
    assert "--port COM5" in text
    assert "--block-size 4096" in text
    assert "--session-id 4660" in text


def test_relay_plan_rejects_out_of_range_node(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        plan(tmp_path, relay_node=32)


def test_relay_plan_rejects_bad_budget(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        plan(tmp_path, attempts=0)


def test_dry_run_returns_command_without_writing(tmp_path: Path) -> None:
    summary = run(plan(tmp_path), dry_run=True)
    assert summary["dry_run"] is True
    assert summary["attempts"] == 1
    assert "--relay-node" in summary["command"]
    assert not (tmp_path / "night").exists()


def test_dry_run_json_shape(tmp_path: Path) -> None:
    summary = run(plan(tmp_path), dry_run=True)
    payload = json.dumps(summary)
    assert "OTA_TDMA_NIGHT_PLAN_V1" in payload
