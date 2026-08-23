#!/usr/bin/env python3
"""Verify a signed factory package before an explicit USB BOOTSEL restore.

The default action is a read-only plan.  ``--execute`` is required before the
existing picotool full-erase/load workflow is invoked.  The package signature,
region hashes, map identity and UF2 target addresses are checked first.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.flash_map.flash_consumer_check import (  # noqa: E402
    FlashConsumerError,
    uf2_target_addresses,
)
from tools.factory_package.factory_package import (  # noqa: E402
    FactoryPackageError,
    verify_factory_package,
)


DEFAULT_PICOTOOL = (
    Path(os.environ.get("USERPROFILE", ""))
    / ".pico-sdk"
    / "picotool"
    / "2.2.0-a4"
    / "picotool"
    / "picotool.exe"
)


class FactoryRestoreError(ValueError):
    """Raised when a restore plan is not safe to execute."""


def expected_uf2_addresses(summary: dict[str, Any]) -> set[int]:
    xip_base = summary.get("xip_base")
    regions = summary.get("regions")
    if not isinstance(xip_base, int) or not isinstance(regions, list):
        raise FactoryRestoreError("factory package geometry/regions are missing")
    addresses: set[int] = set()
    for region in regions:
        if not isinstance(region, dict):
            raise FactoryRestoreError("factory package contains an invalid region")
        offset = region.get("offset")
        size = region.get("size")
        partition = region.get("partition")
        if (not isinstance(offset, int) or not isinstance(size, int) or size <= 0 or
                offset < 0 or not isinstance(partition, str)):
            raise FactoryRestoreError(f"factory region geometry is invalid: {partition}")
        start = xip_base + offset
        aligned_size = (size + 255) & ~255
        addresses.update(range(start, start + aligned_size, 256))
    return addresses


def validate_factory_artifacts(
    package: Path,
    factory_uf2: Path,
    *,
    key_config: Path,
    profile: str,
) -> dict[str, Any]:
    try:
        summary = verify_factory_package(
            package.read_bytes(), key_config=key_config, profile=profile
        )
        actual = uf2_target_addresses(factory_uf2)
    except (OSError, FactoryPackageError, FlashConsumerError) as exc:
        raise FactoryRestoreError(str(exc)) from exc
    expected = expected_uf2_addresses(summary)
    if actual != expected:
        extra = sorted(actual - expected)
        missing = sorted(expected - actual)
        raise FactoryRestoreError(
            f"factory UF2 does not match signed package: extra={extra[:3]} missing={missing[:3]}"
        )
    return {
        "package": str(package),
        "factory_uf2": str(factory_uf2),
        "package_sha256": summary["package_sha256"],
        "map_version": summary["map_version"],
        "key_id": summary["key_id"],
        "region_count": summary["region_count"],
        "uf2_block_count": len(actual),
        "full_erase_required": True,
    }


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--factory-uf2", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--profile", default="v2_candidate")
    parser.add_argument("--picotool", type=Path, default=DEFAULT_PICOTOOL)
    parser.add_argument("--serial-number")
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    try:
        plan = validate_factory_artifacts(
            args.package,
            args.factory_uf2,
            key_config=args.config,
            profile=args.profile,
        )
        if args.execute:
            flash_tool = ROOT / "tools" / "picotool_flash" / "picotool_flash.py"
            command = [
                sys.executable,
                str(flash_tool),
                str(args.factory_uf2),
                "--picotool",
                str(args.picotool),
                "--full-erase",
            ]
            if args.serial_number:
                command.extend(["--serial-number", args.serial_number])
            completed = subprocess.run(
                command,
                cwd=ROOT,
                text=True,
                encoding="utf-8",
                errors="replace",
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            plan["execute_command"] = command
            plan["flash_output"] = completed.stdout
            plan["exit_code"] = completed.returncode
            if completed.returncode != 0:
                raise FactoryRestoreError("picotool factory restore failed")
            print("factory_restore=OK executed=true")
        else:
            plan["execute_required"] = True
            print("factory_restore=READY executed=false; pass --execute to write")
        if args.out:
            output = args.out if args.out.is_absolute() else ROOT / args.out
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return 0
    except (FactoryRestoreError, OSError) as exc:
        print(f"factory_restore=FAILED detail={exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

