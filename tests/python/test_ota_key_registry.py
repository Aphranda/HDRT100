from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.ota_keys import gen_ota_key_registry


PUBLIC_KEY = "04" + "11" * 64


def write_config(path: Path, keys: list[dict[str, object]]) -> None:
    path.write_text(
        json.dumps({
            "schema_version": 1,
            "algorithm": "ecdsa-p256-sha256",
            "keys": keys,
            "profiles": {
                "candidate": {
                    "require_signature": True,
                    "allowed_roles": ["release", "factory"],
                }
            },
        }),
        encoding="utf-8",
    )


def test_registry_generator_emits_public_only_profile(tmp_path: Path) -> None:
    config = tmp_path / "keys.json"
    write_config(config, [{
        "key_id": 7,
        "role": "factory",
        "revoked": False,
        "public_key_hex": PUBLIC_KEY,
    }])
    keys, allowed_mask, required = gen_ota_key_registry.load_registry(config, "candidate")
    source = gen_ota_key_registry.render_source(keys, allowed_mask)

    assert required is True
    assert allowed_mask == 6
    assert ".key_id = 7u" in source
    assert ".role_mask = 4u" in source
    assert "private" not in source.lower()


@pytest.mark.parametrize(
    "keys",
    [
        [{"key_id": 0, "role": "factory", "public_key_hex": PUBLIC_KEY}],
        [{"key_id": 1, "role": "unknown", "public_key_hex": PUBLIC_KEY}],
        [{"key_id": 1, "role": "factory", "public_key_hex": "04"}],
        [
            {"key_id": 1, "role": "factory", "public_key_hex": PUBLIC_KEY},
            {"key_id": 1, "role": "release", "public_key_hex": PUBLIC_KEY},
        ],
    ],
)
def test_registry_generator_rejects_unsafe_entries(
    tmp_path: Path, keys: list[dict[str, object]]
) -> None:
    config = tmp_path / "keys.json"
    write_config(config, keys)
    with pytest.raises(ValueError):
        gen_ota_key_registry.load_registry(config, "candidate")


def test_registry_generator_rejects_duplicate_allowed_roles(tmp_path: Path) -> None:
    config = tmp_path / "keys.json"
    write_config(config, [])
    document = json.loads(config.read_text(encoding="utf-8"))
    document["profiles"]["candidate"]["allowed_roles"] = ["release", "release"]
    config.write_text(json.dumps(document), encoding="utf-8")

    with pytest.raises(ValueError, match="duplicate role"):
        gen_ota_key_registry.load_registry(config, "candidate")
