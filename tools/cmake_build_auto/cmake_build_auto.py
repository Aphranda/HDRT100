#!/usr/bin/env python3
"""Configure/build a CMake tree after a workspace drive-letter move."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="project root")
    parser.add_argument("--preset", default="pico2-release", help="configure preset to mirror")
    parser.add_argument("--build-dir", type=Path, default=Path("out/build/default"), help="CMake build directory")
    parser.add_argument("--force-configure", action="store_true", help="run cmake configure even if cache matches")
    parser.add_argument("--no-build", action="store_true", help="only configure, do not run cmake --build")
    return parser.parse_args()


def normalized_path(path: Path) -> str:
    resolved = path.resolve()
    text = str(resolved).replace("\\", "/")
    return text.casefold() if os.name == "nt" else text


def read_cache_var(cache_path: Path, name: str) -> Path | None:
    if not cache_path.exists():
        return None
    prefix = f"{name}:INTERNAL="
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix):
            value = line[len(prefix) :].strip()
            return Path(value) if value else None
    return None


def load_configure_preset(root: Path, preset_name: str) -> tuple[str, dict[str, str]]:
    data = json.loads((root / "CMakePresets.json").read_text(encoding="utf-8"))
    for preset in data.get("configurePresets", []):
        if preset.get("name") == preset_name:
            generator = str(preset.get("generator", "Ninja"))
            cache_vars = {str(k): str(v) for k, v in preset.get("cacheVariables", {}).items()}
            return generator, cache_vars
    raise SystemExit(f"configure preset not found: {preset_name}")


def cache_matches(root: Path, build_dir: Path) -> bool:
    cache_path = build_dir / "CMakeCache.txt"
    cached_root = read_cache_var(cache_path, "CMAKE_HOME_DIRECTORY")
    cached_build = read_cache_var(cache_path, "CMAKE_CACHEFILE_DIR")
    if cached_root is None or cached_build is None:
        return False
    return normalized_path(cached_root) == normalized_path(root) and normalized_path(cached_build) == normalized_path(build_dir)


def ensure_build_dir_is_safe(root: Path, build_dir: Path) -> None:
    try:
        build_dir.relative_to(root)
    except ValueError as exc:
        raise SystemExit(f"refusing to repair cache outside project root: {build_dir}") from exc


def repair_stale_cache(root: Path, build_dir: Path) -> None:
    ensure_build_dir_is_safe(root, build_dir)
    stale_files = (
        build_dir / "CMakeCache.txt",
        build_dir / "build.ninja",
        build_dir / "cmake_install.cmake",
        build_dir / ".ninja_deps",
        build_dir / ".ninja_log",
    )
    stale_dirs = (build_dir / "CMakeFiles",)

    for path in stale_files:
        if path.exists():
            print(f"remove stale cmake file: {path}", flush=True)
            path.unlink()
    for path in stale_dirs:
        if path.exists():
            print(f"remove stale cmake dir: {path}", flush=True)
            shutil.rmtree(path)


def run(command: list[str], cwd: Path) -> None:
    print("$ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def run_source_size_audit(root: Path, build_dir: Path) -> None:
    """Emit the pre-build single-file size report without blocking a build."""
    report = build_dir / "source_size_report.json"
    run([
        sys.executable,
        str(root / "tools" / "source_size_check" / "source_size_check.py"),
        "--root", str(root),
        "--max-lines", "1000",
        "--output", str(report),
    ], root)


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    build_dir = args.build_dir if args.build_dir.is_absolute() else root / args.build_dir
    build_dir = build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    run_source_size_audit(root, build_dir)
    generator, cache_vars = load_configure_preset(root, args.preset)

    needs_configure = args.force_configure or not cache_matches(root, build_dir)
    if needs_configure:
        print(f"configure=needed build_dir={build_dir}", flush=True)
        repair_stale_cache(root, build_dir)
        cmake_command = ["cmake", "-S", str(root), "-B", str(build_dir), "-G", generator]
        for key, value in cache_vars.items():
            cmake_command.append(f"-D{key}={value}")
        run(cmake_command, root)
    else:
        print(f"configure=skip cache matches current root build_dir={build_dir}", flush=True)

    if not args.no_build:
        run(["cmake", "--build", str(build_dir)], root)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
