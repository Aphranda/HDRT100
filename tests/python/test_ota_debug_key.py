"""Offline checks for the auto-signed debug OTA preset helper."""

from __future__ import annotations

import json
from pathlib import Path

from tools.ota.ota_keys.ensure_debug_key import main as ensure_main


def _source_config(path: Path) -> Path:
    path.write_text(json.dumps({
        "schema_version": 1,
        "algorithm": "ecdsa-p256-sha256",
        "keys": [],
        "profiles": {
            "v2_debug": {"require_signature": True, "allowed_roles": ["dev"]},
        },
    }), encoding="utf-8")
    return path


def test_ensure_debug_key_generates_private_key_and_build_local_registry(
        tmp_path, monkeypatch):
    private = tmp_path / "keys" / "debug.pem"
    output = tmp_path / "generated" / "ota_keys.json"
    source = _source_config(tmp_path / "source.json")
    monkeypatch.setattr(
        "sys.argv",
        ["ensure_debug_key.py", "--private-key", str(private),
         "--source-config", str(source), "--output-config", str(output),
         "--key-id", "3506438145", "--profile", "v2_debug", "--role", "dev"],
    )

    assert ensure_main() == 0
    assert private.is_file()
    document = json.loads(output.read_text(encoding="utf-8"))
    assert document["profiles"]["v2_debug"]["allowed_roles"] == ["dev"]
    assert document["keys"][0]["key_id"] == 3506438145
    assert len(document["keys"][0]["public_key_hex"]) == 130


def test_ensure_debug_key_reuses_existing_key(tmp_path, monkeypatch):
    private = tmp_path / "keys" / "debug.pem"
    output = tmp_path / "generated" / "ota_keys.json"
    source = _source_config(tmp_path / "source.json")
    argv = ["ensure_debug_key.py", "--private-key", str(private),
            "--source-config", str(source), "--output-config", str(output),
            "--key-id", "3506438145", "--profile", "v2_debug", "--role", "dev"]
    monkeypatch.setattr("sys.argv", argv)
    assert ensure_main() == 0

    first = json.loads(output.read_text(encoding="utf-8"))
    assert ensure_main() == 0
    second = json.loads(output.read_text(encoding="utf-8"))
    assert first["keys"] == second["keys"]
