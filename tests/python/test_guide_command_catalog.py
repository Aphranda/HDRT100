"""Guide command-plane registration pinned against the generated catalog.

The C/T segments travel as REFMEM commands (CAL_GUIDE_CONFIG id 50 with a slot
reference, CAL_GUIDE_TRIGGER id 51 carrying action_id + epoch). The YAML is the
single source of truth for the generated table, so these cases assert the
registration itself cannot silently drift.
"""
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "config" / "refmem" / "command_catalog.yaml"


def _commands():
    data = yaml.safe_load(CATALOG.read_text(encoding="utf-8"))
    return {command["name"]: command for command in data["commands"]}


def test_guide_commands_are_registered() -> None:
    commands = _commands()
    assert "CAL_GUIDE_CONFIG" in commands
    assert "CAL_GUIDE_TRIGGER" in commands


def test_guide_config_carries_the_plan_inline() -> None:
    command = _commands()["CAL_GUIDE_CONFIG"]
    assert command["id"] == 50
    assert command["owner"] == "CAL"
    # The 64-byte plan fits the 16-word inline block, so the command carries it
    # directly instead of referencing a parameter-pool slot (the config handler
    # only receives inline words).
    assert command["param_kind"] == "inline"
    assert command["param_words"] == 16
    # Re-sending the same plan sequence must be safe.
    assert command["idempotent"] is True


def test_guide_trigger_carries_action_and_epoch() -> None:
    command = _commands()["CAL_GUIDE_TRIGGER"]
    assert command["id"] == 51
    assert command["owner"] == "CAL"
    assert command["param_kind"] == "inline"
    assert command["params"] == ["action_id", "epoch"]
    # A trigger is an intent: duplicates are not idempotent.
    assert command["idempotent"] is False


def test_catalog_ids_are_unique() -> None:
    data = yaml.safe_load(CATALOG.read_text(encoding="utf-8"))
    ids = [command["id"] for command in data["commands"]]
    assert len(ids) == len(set(ids))
