from __future__ import annotations

import json
from pathlib import Path

from tools.flash_map.flash_wire_check import validate


ROOT = Path(__file__).resolve().parents[2]
CONTRACTS = ROOT / "config" / "flash_wire_contracts.json"


def test_flash_wire_contracts_are_valid() -> None:
    assert validate(CONTRACTS) == []


def test_flash_wire_contracts_reject_unknown_golden_contract(tmp_path: Path) -> None:
    data = json.loads(CONTRACTS.read_text(encoding="utf-8"))
    data["golden_vectors"][0]["contract"] = "UNKNOWN"
    candidate = tmp_path / "wire.json"
    candidate.write_text(json.dumps(data), encoding="utf-8")
    assert any("unknown contract" in error for error in validate(candidate))


def test_flash_wire_contracts_require_parser_corpus_for_each_contract(
    tmp_path: Path,
) -> None:
    data = json.loads(CONTRACTS.read_text(encoding="utf-8"))
    data["parser_corpus"].pop()
    candidate = tmp_path / "wire.json"
    candidate.write_text(json.dumps(data), encoding="utf-8")

    assert any("parser corpus missing contracts" in error
               for error in validate(candidate))


def test_flash_wire_contracts_reject_missing_parser_anchor(tmp_path: Path) -> None:
    data = json.loads(CONTRACTS.read_text(encoding="utf-8"))
    data["parser_corpus"][0]["parser"] = "missing_parser_symbol"
    candidate = tmp_path / "wire.json"
    candidate.write_text(json.dumps(data), encoding="utf-8")

    errors = validate(candidate)
    assert any("parser missing from source" in error for error in errors)
    assert any("parser missing from fixture" in error for error in errors)
