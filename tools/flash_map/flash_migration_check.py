#!/usr/bin/env python3
"""Validate HAOFV v1-to-v2 migration and rollback policy inputs."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(path: Path) -> list[str]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"policy unreadable: {exc}"]
    errors: list[str] = []
    if data.get("policy_version") != 1:
        errors.append("policy_version must be 1")
    if data.get("source_map") != "v1_compat" or data.get("target_map") != "v2":
        errors.append("source_map/target_map must be v1_compat/v2")
    if data.get("online_relocation") != "forbidden":
        errors.append("online relocation must be forbidden")
    factory = data.get("factory_migration", {})
    for key in ("required", "artifact", "transport", "erase_policy", "tool"):
        if key not in factory:
            errors.append(f"factory_migration missing {key}")
    if factory.get("transport") != "BOOTSEL":
        errors.append("factory migration transport must be BOOTSEL")
    if factory.get("erase_policy") != "full_erase_before_factory_image":
        errors.append("factory migration must full erase before image")
    expected_data = {"identity", "product_config", "ota_metadata", "calibration", "report_index"}
    if set(data.get("data_policy", {})) != expected_data:
        errors.append("data_policy must cover identity/product_config/ota_metadata/calibration/report_index")
    expected_boot = {"blank", "v1_compat", "unknown", "v2", "user_signal"}
    if set(data.get("boot_behavior", {})) != expected_boot:
        errors.append("boot_behavior must cover blank/v1/unknown/v2/user_signal")
    rollback = data.get("rollback", {})
    if rollback.get("map_migration") != "BOOTSEL_factory_full_erase_reflash":
        errors.append("map migration rollback must use BOOTSEL factory full erase/reflash")
    if rollback.get("destructive_scpi") != "forbidden":
        errors.append("destructive SCPI must be forbidden")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("policy", type=Path)
    args = parser.parse_args()
    errors = validate(args.policy)
    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    print("flash_migration=OK source=v1_compat target=v2 online_relocation=forbidden")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
