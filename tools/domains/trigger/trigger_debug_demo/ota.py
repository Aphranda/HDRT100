#!/usr/bin/env python3
"""OTA image helpers and automatic receipt issuance for the debug demo.

Only the host-side preparation and evidence format live here.  The actual
commands go through the shared Dispatcher, never through a second port.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import binascii
import hashlib
import json
from pathlib import Path
from typing import Any


TOOL_VERSION = "1.0"
RECEIPT_SCHEMA = "DHRT100_OTA_DEBUG_RECEIPT_V1"


@dataclass(frozen=True)
class OtaImage:
    path: Path
    data: bytes
    size: int
    crc32: int
    sha256: str
    package: bool

    @classmethod
    def load(cls, path: str | Path) -> "OtaImage":
        file_path = Path(path)
        data = file_path.read_bytes()
        if not data:
            raise ValueError("镜像为空")
        return cls(
            path=file_path,
            data=data,
            size=len(data),
            crc32=binascii.crc32(data) & 0xFFFFFFFF,
            sha256=hashlib.sha256(data).hexdigest(),
            package=file_path.suffix.lower() == ".pkg",
        )


def scpi_block(data: bytes) -> bytes:
    """Definite-length SCPI arbitrary block."""
    length = str(len(data)).encode("ascii")
    return b"#" + str(len(length)).encode("ascii") + length + data


def chunks(image: OtaImage, block_size: int):
    if not 256 <= block_size <= 4096:
        raise ValueError("块大小必须在 256..4096")
    for offset in range(0, image.size, block_size):
        yield offset, image.data[offset:offset + block_size]


def _source_fingerprint() -> tuple[str, str, bool, int]:
    """Cheap, honest repository identity for the receipt.

    A full tracked-file hash (credential_scope.scope_fingerprint) takes tens of
    seconds and would stall every OTA.  The git revision/tree plus the dirty
    flag is the same identity used by the build checkout and is cheap.
    """
    root = Path(__file__).resolve().parents[4]
    try:
        import subprocess
        def git(*args: str) -> str:
            return subprocess.check_output(
                ["git", *args], cwd=root, stderr=subprocess.DEVNULL,
                timeout=5).decode("utf-8", errors="replace").strip()
        revision = git("rev-parse", "HEAD")
        tree = git("rev-parse", "HEAD^{tree}")
        dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
        return revision, tree, dirty, 0
    except Exception:
        return "", "", False, 0


def receipt_body(
    *,
    mode: str,
    backend: str,
    resource: str,
    board_id: str,
    board_no: str,
    image: OtaImage | None,
    before: dict[str, Any],
    after: dict[str, Any],
    transfer: dict[str, Any],
    stage: str,
    passed: bool,
    log_sha256: str,
) -> dict[str, Any]:
    generated = datetime.now(timezone.utc).isoformat()
    source_revision, source_tree, source_dirty, source_file_count = \
        _source_fingerprint()
    body = {
        "schema": RECEIPT_SCHEMA,
        "tool": "trigger_debug_demo",
        "tool_version": TOOL_VERSION,
        "generated_at_utc": generated,
        "source_revision": source_revision,
        "source_tree": source_tree,
        "source_dirty": source_dirty,
        "source_file_count": source_file_count,
        "mode": mode,
        "backend": backend,
        "resource": resource,
        "board_id": board_id,
        "board_no": board_no,
        "image": (None if image is None else {
            "path": str(image.path),
            "size": image.size,
            "crc32": f"0x{image.crc32:08X}",
            "sha256": image.sha256,
            "package": image.package,
        }),
        "before": before,
        "after": after,
        "transfer": transfer,
        "stage": stage,
        "passed": passed,
        "log_sha256": log_sha256,
    }
    canonical = json.dumps(body, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":")).encode("utf-8")
    body["receipt_sha256"] = hashlib.sha256(canonical).hexdigest()
    return body


def write_receipt(directory: Path, body: dict[str, Any]) -> Path:
    """Write JSON plus a detached .sha256 sidecar (automatic issuance)."""
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    board = body.get("board_id") or "board"
    safe_board = "".join(ch if ch.isalnum() or ch in "-_" else "_"
                         for ch in str(board))[:32]
    path = directory / f"ota_receipt_{stamp}_{safe_board}.json"
    path.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")
    digest = body["receipt_sha256"]
    path.with_suffix(".json.sha256").write_text(
        f"{digest}  {path.name}\n", encoding="utf-8")
    return path


def parse_slot(response: str) -> dict[str, int | str]:
    values = [item.strip() for item in response.split(",")]
    keys = ("active", "pending", "confirmed", "boot_attempts", "rollback")
    out: dict[str, int | str] = {}
    for index, key in enumerate(keys):
        try:
            out[key] = int(values[index])
        except (IndexError, ValueError):
            out[key] = response
    return out


def parse_status(response: str) -> dict[str, str | int]:
    values = [item.strip().strip('"') for item in response.split(",")]
    if len(values) < 4:
        return {"state": response, "target_slot": 0, "error": "PARSE",
                "last_result": 0}
    try:
        slot = int(values[1])
        result = int(values[3])
    except ValueError:
        slot, result = 0, 0
    return {"state": values[0], "target_slot": slot, "error": values[2],
            "last_result": result}


def parse_progress(response: str) -> dict[str, int]:
    values = [item.strip() for item in response.split(",")]
    try:
        return {"received": int(values[0]), "expected": int(values[1]),
                "permille": int(values[2]), "stage": int(values[3])}
    except (IndexError, ValueError):
        return {"received": 0, "expected": 0, "permille": 0, "stage": 0}


def parse_result(response: str) -> dict[str, Any]:
    values = [item.strip().strip('"') for item in response.split(",")]
    if len(values) < 6:
        return {"raw": response}
    try:
        result = int(values[0])
        source = int(values[3])
        size = int(values[4])
        crc = int(values[5])
    except ValueError:
        return {"raw": response}
    return {"last_result": result, "error": values[1], "boot_result": values[2],
            "source_slot": source, "size": size, "crc32": crc}
