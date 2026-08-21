#!/usr/bin/env python3
"""Static release gate checks for RP2350_TRIG firmware artifacts."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


REQUIRED_ARTIFACTS = (
    "RP2350_TRIG_FACTORY.uf2",
    "RP2350_TRIG.bin",
    "RP2350_TRIG_B.bin",
    "RP2350_TRIG_UPDATE.pkg",
    "RP2350_TRIG_BOOT.uf2",
    "RP2350_TRIG.elf.map",
    "RP2350_TRIG_B.elf.map",
    "RP2350_TRIG_BOOT.elf.map",
    "RP2350_TRIG_BOOT.bin",
    "ota_metadata_clear.bin",
)

FORBIDDEN_RELEASE_STRINGS = (
    b"SYSTem:OTA:INJect",
    b"SYST:OTA:INJ",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--preset", default="pico2-release")
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    return parser.parse_args()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def fail(message: str, failures: list[str]) -> None:
    failures.append(message)
    print(f"FAIL {message}")


def ok(message: str) -> None:
    print(f"OK   {message}")


def get_preset_cache_variables(root: Path, preset_name: str) -> dict[str, str]:
    presets_path = root / "CMakePresets.json"
    data = json.loads(read_text(presets_path))
    for preset in data.get("configurePresets", []):
        if preset.get("name") == preset_name:
            return preset.get("cacheVariables", {})
    raise KeyError(f"configure preset not found: {preset_name}")


def check_preset(root: Path, preset_name: str, failures: list[str]) -> None:
    cache_variables = get_preset_cache_variables(root, preset_name)
    fault_injection = str(cache_variables.get("PROJECT_ENABLE_OTA_FAULT_INJECTION", "")).upper()
    if fault_injection != "OFF":
        fail(f"{preset_name} must set PROJECT_ENABLE_OTA_FAULT_INJECTION=OFF", failures)
    else:
        ok(f"{preset_name} disables OTA fault injection")

    uart_stdio = str(cache_variables.get("PROJECT_ENABLE_UART_STDIO", "")).upper()
    if uart_stdio != "OFF":
        fail(f"{preset_name} must set PROJECT_ENABLE_UART_STDIO=OFF", failures)
    else:
        ok(f"{preset_name} disables UART stdio")

    use_freertos = str(cache_variables.get("PROJECT_USE_FREERTOS", "")).upper()
    if use_freertos != "ON":
        fail(f"{preset_name} must set PROJECT_USE_FREERTOS=ON", failures)
    else:
        ok(f"{preset_name} enables FreeRTOS")

    use_multicore = str(cache_variables.get("PROJECT_USE_MULTICORE", "")).upper()
    if use_multicore != "ON":
        fail(f"{preset_name} must set PROJECT_USE_MULTICORE=ON", failures)
    else:
        ok(f"{preset_name} enables multicore AMP")

    default_boot_mode = str(cache_variables.get("PROJECT_OTA_DEFAULT_BOOT_MODE", "")).upper()
    if default_boot_mode != "DIRECT_AB":
        fail(f"{preset_name} must set PROJECT_OTA_DEFAULT_BOOT_MODE=DIRECT_AB", failures)
    else:
        ok(f"{preset_name} defaults OTA boot mode to DIRECT_AB")

    deployment_map = str(cache_variables.get("PROJECT_FLASH_DEPLOYMENT_MAP", ""))
    if deployment_map != "v1_compat":
        fail(f"{preset_name} must set PROJECT_FLASH_DEPLOYMENT_MAP=v1_compat", failures)
    else:
        ok(f"{preset_name} selects the deployed v1 compatibility map")


def find_project_config_define(config_text: str, name: str) -> str | None:
    pattern = re.compile(rf"^\s*#define\s+{re.escape(name)}\s+(.+?)\s*$", re.MULTILINE)
    match = pattern.search(config_text)
    return match.group(1).strip() if match else None


def check_project_config(root: Path, failures: list[str]) -> None:
    config_text = read_text(root / "config" / "project_config.h")

    health_log = find_project_config_define(config_text, "PROJECT_ENABLE_HEALTH_LOG")
    if health_log != "0":
        fail("PROJECT_ENABLE_HEALTH_LOG default must be 0", failures)
    else:
        ok("health log default is disabled")

    fault_injection = find_project_config_define(config_text, "PROJECT_ENABLE_OTA_FAULT_INJECTION")
    if fault_injection != "0":
        fail("PROJECT_ENABLE_OTA_FAULT_INJECTION fallback default must be 0", failures)
    else:
        ok("OTA fault injection fallback default is disabled")

    direct_ab_default = find_project_config_define(config_text, "PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB")
    if direct_ab_default != "1":
        fail("PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB fallback default must be 1", failures)
    else:
        ok("OTA fallback default boot mode is DIRECT_AB")


def check_artifacts(root: Path, build_dir: Path, failures: list[str]) -> None:
    build_path = build_dir if build_dir.is_absolute() else root / build_dir
    for artifact in REQUIRED_ARTIFACTS:
        path = build_path / artifact
        if not path.exists() or path.stat().st_size == 0:
            fail(f"missing release artifact: {path}", failures)
        else:
            ok(f"artifact exists: {path}")


def check_forbidden_strings(root: Path, build_dir: Path, failures: list[str]) -> None:
    build_path = build_dir if build_dir.is_absolute() else root / build_dir
    files_to_scan = (
        build_path / "RP2350_TRIG.bin",
        build_path / "RP2350_TRIG.elf",
        build_path / "RP2350_TRIG_BOOT.elf",
    )

    for path in files_to_scan:
        if not path.exists():
            fail(f"cannot scan missing artifact: {path}", failures)
            continue

        data = path.read_bytes()
        for needle in FORBIDDEN_RELEASE_STRINGS:
            if needle in data:
                fail(f"forbidden OTA injection string found in {path}: {needle.decode('ascii')}", failures)
                break
        else:
            ok(f"no OTA injection command strings in {path.name}")


def check_flash_contracts(root: Path, build_dir: Path, failures: list[str]) -> None:
    generated = root / "config" / "flash_map_gen"
    cmake_text = read_text(root / "CMakeLists.txt")
    geometry_match = re.search(r"set\(PICO_FLASH_SIZE_BYTES\s+([0-9]+)\b", cmake_text)
    if geometry_match is None:
        fail("cannot read PICO_FLASH_SIZE_BYTES from CMakeLists.txt", failures)
        return
    expected_geometry = geometry_match.group(1)
    commands = (
        [
            sys.executable,
            str(root / "tools" / "flash_map" / "flash_map.py"),
            str(root / "config" / "flash_map_v1_compat.json"),
            "--schema", str(root / "config" / "flash_map.schema.json"),
            "--header", str(generated / "flash_map_v1_compat.h"),
            "--manifest", str(generated / "flash_map_v1_compat_manifest.json"),
            "--cmake", str(generated / "flash_map_v1_compat.cmake"),
            "--ld", str(generated / "flash_map_v1_compat.ldinc"),
            "--symbol-prefix", "FLASH_COMPAT_MAP",
            "--header-guard", "FLASH_MAP_V1_COMPAT_GENERATED_H",
            "--expected-geometry", expected_geometry,
            "--check",
        ],
        [
            sys.executable,
            str(root / "tools" / "flash_map" / "flash_map.py"),
            str(root / "config" / "flash_map_v2.json"),
            "--schema", str(root / "config" / "flash_map.schema.json"),
            "--header", str(generated / "flash_map_v2.h"),
            "--manifest", str(generated / "flash_map_v2_manifest.json"),
            "--cmake", str(generated / "flash_map_v2.cmake"),
            "--ld", str(generated / "flash_map_v2.ldinc"),
            "--expected-geometry", expected_geometry,
            "--check",
        ],
        [
            sys.executable,
            str(root / "tools" / "flash_map" / "flash_inventory.py"),
            "--root", str(root),
            "--inventory", str(root / "config" / "flash_raw_call_allowlist.json"),
        ],
        [
            sys.executable,
            str(root / "tools" / "flash_map" / "flash_consumer_check.py"),
            "--root", str(root),
            "--manifest", str(generated / "flash_map_v1_compat_manifest.json"),
            "--build-dir", str(build_dir if build_dir.is_absolute() else root / build_dir),
        ],
    )
    labels = (
        "generated v1 compatibility FlashMap artifacts are current",
        "generated FlashMap artifacts are current",
        "raw Flash inventory matches source",
        "live Flash consumers and artifacts match the deployed map",
    )
    for command, label in zip(commands, labels):
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            fail(f"{label}: {detail}", failures)
        else:
            ok(label)


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    failures: list[str] = []

    check_preset(root, args.preset, failures)
    check_project_config(root, failures)
    check_flash_contracts(root, args.build_dir, failures)
    check_artifacts(root, args.build_dir, failures)
    check_forbidden_strings(root, args.build_dir, failures)

    if failures:
        print(f"\nrelease_check=FAILED failures={len(failures)}")
        return 1

    print("\nrelease_check=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
