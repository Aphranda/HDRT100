from __future__ import annotations

import json
from pathlib import Path

from tools.flash_map.persistence_schema_check import validate


ROOT = Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "config" / "persistence_schema_registry.json"


def test_persistence_registry_is_valid() -> None:
    assert validate(REGISTRY) == []


def test_persistence_registry_rejects_duplicate_type_id(tmp_path: Path) -> None:
    data = json.loads(REGISTRY.read_text(encoding="utf-8"))
    data["objects"][1]["type_id"] = data["objects"][0]["type_id"]
    candidate = tmp_path / "registry.json"
    candidate.write_text(json.dumps(data), encoding="utf-8")
    assert any("duplicate type_id" in error for error in validate(candidate))


def test_persistence_registry_rejects_missing_negative_inventory(tmp_path: Path) -> None:
    data = json.loads(REGISTRY.read_text(encoding="utf-8"))
    data["negative_inventory"] = []
    candidate = tmp_path / "registry.json"
    candidate.write_text(json.dumps(data), encoding="utf-8")
    assert any("negative inventory missing" in error for error in validate(candidate))
