"""Local GUI drafts only; never persist device state or queued commands."""
from __future__ import annotations

import json
import os
from pathlib import Path


SCHEMA_VERSION = 1
MAX_SETTINGS_BYTES = 65536


def default_path() -> Path:
    root = Path(os.environ.get("LOCALAPPDATA") or Path.home() / ".config")
    return root / "DHRT100" / "sequence_trigger_debug_ui.json"


def load(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8-sig") as stream:
            raw = stream.read(MAX_SETTINGS_BYTES + 1)
    except FileNotFoundError:
        return {}
    if len(raw) > MAX_SETTINGS_BYTES:
        raise ValueError("configuration file is too large")
    data = json.loads(raw)
    if not isinstance(data, dict) or type(data.get("version")) is not int or \
            data["version"] != SCHEMA_VERSION or not isinstance(data.get("values"), dict):
        raise ValueError("unsupported configuration schema")
    return data["values"]


def save(path: Path, values: dict) -> None:
    raw = json.dumps({"version": SCHEMA_VERSION, "values": values},
                     ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    if len(raw.encode("utf-8")) > MAX_SETTINGS_BYTES:
        raise ValueError("configuration file is too large")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
