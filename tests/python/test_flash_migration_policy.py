from __future__ import annotations

import json
from pathlib import Path

from tools.flash_map.flash_migration_check import validate


ROOT = Path(__file__).resolve().parents[2]
POLICY = ROOT / "config" / "flash_migration_policy.json"


def test_flash_migration_policy_is_valid() -> None:
    assert validate(POLICY) == []


def test_flash_migration_policy_rejects_online_relocation(tmp_path: Path) -> None:
    data = json.loads(POLICY.read_text(encoding="utf-8"))
    data["online_relocation"] = "allowed"
    candidate = tmp_path / "policy.json"
    candidate.write_text(json.dumps(data), encoding="utf-8")
    assert any("online relocation" in error for error in validate(candidate))
