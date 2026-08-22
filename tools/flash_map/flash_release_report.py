#!/usr/bin/env python3
"""Emit an independent Flash owner/link contract report for a DHRT100 build."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

# Keep direct ``python tools/flash_map/flash_release_report.py`` invocation
# equivalent to module execution from the repository root.
REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.flash_map.flash_link_check import validate_link_contract


ARTIFACTS = (
    ("app_a", "DHRT100.elf.map", "DHRT100.dis", "app"),
    ("app_b", "DHRT100_B.elf.map", "DHRT100_B.dis", "app"),
    ("boot", "DHRT100_BOOT.elf.map", "DHRT100_BOOT.dis", "boot"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_revision(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def collect_report(root: Path, build_dir: Path) -> dict[str, object]:
    build = build_dir if build_dir.is_absolute() else root / build_dir
    entries: list[dict[str, object]] = []
    artifacts = list(ARTIFACTS)
    if (build / "DHRT100_RECOVERY.elf.map").exists():
        artifacts.append(("recovery", "DHRT100_RECOVERY.elf.map",
                          "DHRT100_RECOVERY.dis", "recovery"))
    for name, map_name, dis_name, profile in artifacts:
        map_path = build / map_name
        dis_path = build / dis_name
        entry: dict[str, object] = {
            "name": name,
            "profile": profile,
            "map": map_name,
            "disassembly": dis_name,
            "failures": [],
        }
        if not map_path.exists() or not dis_path.exists():
            entry["failures"] = [f"missing artifact: {map_name} or {dis_name}"]
        else:
            entry["map_sha256"] = sha256(map_path)
            entry["disassembly_sha256"] = sha256(dis_path)
            entry["failures"] = validate_link_contract(
                map_path.read_text(encoding="utf-8", errors="replace"),
                dis_path.read_text(encoding="utf-8", errors="replace"),
                profile,
            )
        entries.append(entry)
    return {
        "schema": 1,
        "product": "DHRT100",
        "git_revision": git_revision(root),
        "build_dir": str(build),
        "entries": entries,
        "ok": all(not entry["failures"] for entry in entries),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    report = collect_report(root, args.build_dir)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        output = args.output if args.output.is_absolute() else root / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded, encoding="utf-8")
        print(f"flash_release_report={output}")
    else:
        print(encoded, end="")
    print(f"flash_release_contract={'OK' if report['ok'] else 'FAILED'}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
