"""Schema-checked, atomic settings persistence."""

from __future__ import annotations

import json
from pathlib import Path

DEFAULTS = {
    "backend": "演示模式",
    "port": "",
    "baud": "115200",
    "geometry": "1280x880",
    "tcp_host": "127.0.0.1",
    "tcp_port": "5025",
}
DEFAULT_SETTINGS = DEFAULTS

_TYPES = {
    "backend": str,
    "port": str,
    "baud": str,
    "geometry": str,
    "tcp_host": str,
    "tcp_port": str,
}


def load_settings(path: Path | str) -> dict:
    path = Path(path)
    try:
        raw = path.read_bytes()
    except OSError:
        return dict(DEFAULTS)
    for encoding in ("utf-8", "gbk"):
        try:
            parsed = json.loads(raw.decode(encoding))
            break
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
    else:
        _backup_bad(path)
        return dict(DEFAULTS)
    if not isinstance(parsed, dict):
        _backup_bad(path)
        return dict(DEFAULTS)
    result = dict(DEFAULTS)
    for key, kind in _TYPES.items():
        value = parsed.get(key)
        if isinstance(value, kind):
            result[key] = value
    return result


def save_settings(path: Path | str, data: dict) -> None:
    path = Path(path)
    clean = {}
    for key, default in DEFAULTS.items():
        value = data.get(key, default)
        clean[key] = value if isinstance(value, _TYPES[key]) else default
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(clean, ensure_ascii=False, indent=2),
                   encoding="utf-8")
    tmp.replace(path)


def _backup_bad(path: Path) -> None:
    try:
        if path.exists():
            path.replace(path.with_name(path.name + ".bad"))
    except OSError:
        pass
