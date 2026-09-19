import json

import pytest

from tools.sequence_trigger_debug_ui import settings


def test_settings_roundtrip_and_missing(tmp_path):
    path = tmp_path / "app" / "settings.json"
    assert settings.load(path) == {}
    values = {"mode": "turntable", "speed": "5", "enabled": True, "outputs": [True, False]}
    settings.save(path, values)
    assert settings.load(path) == values
    settings.save(path, {"speed": "10"})
    assert settings.load(path) == {"speed": "10"}


@pytest.mark.parametrize("value", [None, [], {}, {"version": 2, "values": {}},
    {"version": True, "values": {}}, {"version": 1, "values": []}])
def test_invalid_schema(tmp_path, value):
    path = tmp_path / "settings.json"
    path.write_text(json.dumps(value), encoding="utf-8")
    with pytest.raises(ValueError):
        settings.load(path)


def test_failed_replace_preserves_previous_config(tmp_path, monkeypatch):
    path = tmp_path / "settings.json"
    settings.save(path, {"speed": "1"})
    def fail(*args):
        raise PermissionError("read-only configuration")
    monkeypatch.setattr(settings.os, "replace", fail)
    with pytest.raises(PermissionError):
        settings.save(path, {"speed": "5"})
    assert settings.load(path) == {"speed": "1"}


def test_size_limit_does_not_replace_valid_settings(tmp_path):
    path = tmp_path / "settings.json"
    settings.save(path, {"speed": "1"})
    with pytest.raises(ValueError):
        settings.save(path, {"speed": "x" * settings.MAX_SETTINGS_BYTES})
    assert settings.load(path) == {"speed": "1"}
