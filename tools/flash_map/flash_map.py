#!/usr/bin/env python3
"""Validate the HAOFV FlashMap source and generate deterministic consumers."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


UINT32_MAX = (1 << 32) - 1
CONTEXTS = ("boot", "app", "factory")
PERMISSIONS = {"read": 1, "write": 2, "execute": 4}
TOP_KEYS = {"schema_version", "map_version", "deployment_state", "geometry", "compatibility", "partitions"}
GEOMETRY_KEYS = {"profile", "total_size", "erase_size", "program_size", "xip_base"}
COMPATIBILITY_KEYS = {"predecessor_map_versions", "migration", "unknown_map_policy", "reserved_gap_policy"}
PARTITION_KEYS = {"id", "offset", "size", "alignment", "executable", "store_type", "update_policy", "permissions"}


class FlashMapError(ValueError):
    pass


def parse_number(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise FlashMapError(f"{field} must be an integer or hexadecimal string")
    if isinstance(value, int):
        result = value
    elif isinstance(value, str) and re.fullmatch(r"0x[0-9A-Fa-f]+", value):
        result = int(value, 16)
    else:
        raise FlashMapError(f"{field} must be an integer or hexadecimal string")
    if result < 0 or result > UINT32_MAX:
        raise FlashMapError(f"{field} is outside uint32 range")
    return result


def require_exact_keys(value: Any, expected: set[str], field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FlashMapError(f"{field} must be an object")
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise FlashMapError(f"{field} keys mismatch: missing={missing} extra={extra}")
    return value


def is_power_of_two(value: int) -> bool:
    return value > 0 and (value & (value - 1)) == 0


def validate_map(source: dict[str, Any]) -> dict[str, Any]:
    require_exact_keys(source, TOP_KEYS, "root")
    if source["schema_version"] != 1:
        raise FlashMapError("schema_version must be 1")
    if not isinstance(source["map_version"], int) or source["map_version"] < 1:
        raise FlashMapError("map_version must be a positive integer")
    if source["deployment_state"] not in {
            "deployed_compatibility", "target_not_deployed",
            "factory_validation", "deployed"}:
        raise FlashMapError("deployment_state is invalid")

    geometry_source = require_exact_keys(source["geometry"], GEOMETRY_KEYS, "geometry")
    profile = geometry_source["profile"]
    if not isinstance(profile, str) or re.fullmatch(r"[a-z0-9_]+", profile) is None:
        raise FlashMapError("geometry.profile is invalid")
    geometry = {"profile": profile}
    for name in ("total_size", "erase_size", "program_size", "xip_base"):
        geometry[name] = parse_number(geometry_source[name], f"geometry.{name}")
    if not is_power_of_two(geometry["erase_size"]):
        raise FlashMapError("geometry.erase_size must be a power of two")
    if not is_power_of_two(geometry["program_size"]):
        raise FlashMapError("geometry.program_size must be a power of two")
    if geometry["program_size"] > geometry["erase_size"]:
        raise FlashMapError("geometry.program_size exceeds erase_size")
    if geometry["total_size"] == 0 or geometry["total_size"] % geometry["erase_size"] != 0:
        raise FlashMapError("geometry.total_size must be non-zero and erase aligned")
    if geometry["xip_base"] > UINT32_MAX - geometry["total_size"]:
        raise FlashMapError("geometry XIP range overflows uint32")

    compatibility = require_exact_keys(source["compatibility"], COMPATIBILITY_KEYS, "compatibility")
    predecessors = compatibility["predecessor_map_versions"]
    if (not isinstance(predecessors, list) or any(not isinstance(item, int) or item < 1 for item in predecessors)
            or len(predecessors) != len(set(predecessors))):
        raise FlashMapError("compatibility.predecessor_map_versions is invalid")
    if compatibility["unknown_map_policy"] != "fail_closed":
        raise FlashMapError("compatibility.unknown_map_policy must be fail_closed")
    if compatibility["reserved_gap_policy"] not in {"forbid", "explicit_reserved_partition"}:
        raise FlashMapError("compatibility.reserved_gap_policy is invalid")
    if not isinstance(compatibility["migration"], str) or not compatibility["migration"]:
        raise FlashMapError("compatibility.migration must be non-empty")

    source_partitions = source["partitions"]
    if not isinstance(source_partitions, list) or not source_partitions:
        raise FlashMapError("partitions must be a non-empty array")

    partitions: list[dict[str, Any]] = []
    ids: set[str] = set()
    cursor = 0
    for index, raw in enumerate(source_partitions):
        field = f"partitions[{index}]"
        partition = require_exact_keys(raw, PARTITION_KEYS, field)
        partition_id = partition["id"]
        if not isinstance(partition_id, str) or re.fullmatch(r"[A-Z][A-Z0-9_]*", partition_id) is None:
            raise FlashMapError(f"{field}.id is invalid")
        if partition_id in ids:
            raise FlashMapError(f"duplicate partition id: {partition_id}")
        ids.add(partition_id)
        offset = parse_number(partition["offset"], f"{field}.offset")
        size = parse_number(partition["size"], f"{field}.size")
        alignment = parse_number(partition["alignment"], f"{field}.alignment")
        if size == 0:
            raise FlashMapError(f"{partition_id} size must be non-zero")
        if not is_power_of_two(alignment) or alignment < geometry["erase_size"]:
            raise FlashMapError(f"{partition_id} alignment must be a power of two at least erase_size")
        if offset % alignment != 0 or size % geometry["erase_size"] != 0:
            raise FlashMapError(f"{partition_id} range is not aligned")
        if offset != cursor:
            relation = "overlaps" if offset < cursor else "leaves an implicit gap before"
            raise FlashMapError(f"{partition_id} {relation} offset 0x{cursor:08X}")
        if size > geometry["total_size"] - offset:
            raise FlashMapError(f"{partition_id} exceeds geometry")

        executable = partition["executable"]
        if not isinstance(executable, bool):
            raise FlashMapError(f"{partition_id}.executable must be boolean")
        store_type = partition["store_type"]
        update_policy = partition["update_policy"]
        if not isinstance(store_type, str) or re.fullmatch(r"[a-z][a-z0-9_]*", store_type) is None:
            raise FlashMapError(f"{partition_id}.store_type is invalid")
        if not isinstance(update_policy, str) or re.fullmatch(r"[a-z][a-z0-9_]*", update_policy) is None:
            raise FlashMapError(f"{partition_id}.update_policy is invalid")

        permissions_source = require_exact_keys(partition["permissions"], set(CONTEXTS), f"{field}.permissions")
        permissions: dict[str, list[str]] = {}
        for context in CONTEXTS:
            values = permissions_source[context]
            if (not isinstance(values, list) or any(value not in PERMISSIONS for value in values)
                    or len(values) != len(set(values))):
                raise FlashMapError(f"{partition_id} {context} permissions are invalid")
            permissions[context] = list(values)
        any_execute = any("execute" in permissions[context] for context in CONTEXTS)
        if executable != any_execute:
            raise FlashMapError(f"{partition_id} executable flag disagrees with permissions")
        if executable and store_type != "image":
            raise FlashMapError(f"{partition_id} executable partition must use image store_type")
        if not executable and any_execute:
            raise FlashMapError(f"{partition_id} non-executable partition grants execute")
        if executable and "read" not in permissions["boot"]:
            raise FlashMapError(f"{partition_id} executable partition must be boot-readable")
        if partition_id in {"BOOTLOADER", "RECOVERY"} and "write" in permissions["app"]:
            raise FlashMapError(f"{partition_id} must not be App-writable")
        if partition_id == "FUTURE_POOL" and any(permissions[context] for context in CONTEXTS):
            raise FlashMapError("FUTURE_POOL must not grant permissions")

        partitions.append({
            "id": partition_id,
            "offset": offset,
            "size": size,
            "alignment": alignment,
            "executable": executable,
            "store_type": store_type,
            "update_policy": update_policy,
            "permissions": permissions,
        })
        cursor = offset + size

    if cursor != geometry["total_size"]:
        raise FlashMapError(f"map tail 0x{cursor:08X} does not equal geometry 0x{geometry['total_size']:08X}")
    by_id = {partition["id"]: partition for partition in partitions}
    required_partitions = {
        "BOOTLOADER", "BOOT_CONTROL", "APP_A", "APP_B", "SCRATCH",
        "FUTURE_POOL",
    }
    if source["deployment_state"] != "deployed_compatibility":
        required_partitions.add("RECOVERY")
    for required in sorted(required_partitions):
        if required not in by_id:
            raise FlashMapError(f"required partition is missing: {required}")
    if by_id["APP_A"]["size"] != by_id["APP_B"]["size"]:
        raise FlashMapError("APP_A and APP_B must have equal sizes")

    return {
        "schema_version": source["schema_version"],
        "map_version": source["map_version"],
        "deployment_state": source["deployment_state"],
        "geometry": geometry,
        "compatibility": compatibility,
        "partitions": partitions,
    }


def load_and_validate(path: Path) -> dict[str, Any]:
    try:
        source = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FlashMapError(f"cannot load {path}: {exc}") from exc
    if not isinstance(source, dict):
        raise FlashMapError("FlashMap root must be an object")
    return validate_map(source)


def hex_u(value: int, width: int = 8) -> str:
    return f"0x{value:0{width}X}u"


def geometry_symbol_prefix(symbol_prefix: str) -> str:
    suffix = "_MAP"
    if symbol_prefix.endswith(suffix):
        return symbol_prefix[:-len(suffix)] + "_GEOMETRY"
    return symbol_prefix + "_GEOMETRY"


def render_header(data: dict[str, Any], source_path: Path,
                  symbol_prefix: str = "FLASH_MAP",
                  header_guard: str = "FLASH_MAP_V2_GENERATED_H") -> str:
    geometry = data["geometry"]
    geometry_prefix = geometry_symbol_prefix(symbol_prefix)
    lines = [
        f"#ifndef {header_guard}",
        f"#define {header_guard}",
        "",
        "/* Generated by tools/flash_map/flash_map.py; do not edit. */",
        f"/* Source: {source_path.as_posix()} ({data['deployment_state']}). */",
        "",
        f"#define {symbol_prefix}_SCHEMA_VERSION {data['schema_version']}u",
        f"#define {symbol_prefix}_VERSION        {data['map_version']}u",
        f'#define {symbol_prefix}_DEPLOYMENT_STATE "{data["deployment_state"]}"',
        f"#define {geometry_prefix}_TOTAL_SIZE_BYTES {hex_u(geometry['total_size'])}",
        f"#define {geometry_prefix}_ERASE_SIZE_BYTES {hex_u(geometry['erase_size'])}",
        f"#define {geometry_prefix}_PROGRAM_SIZE_BYTES {hex_u(geometry['program_size'])}",
        f"#define {geometry_prefix}_XIP_BASE {hex_u(geometry['xip_base'])}",
        "",
        f"#define {symbol_prefix}_PERMISSION_READ    (1u << 0)",
        f"#define {symbol_prefix}_PERMISSION_WRITE   (1u << 1)",
        f"#define {symbol_prefix}_PERMISSION_EXECUTE (1u << 2)",
        "",
    ]
    for index, partition in enumerate(data["partitions"]):
        prefix = f"{symbol_prefix}_{partition['id']}"
        lines.extend([
            f"#define {prefix}_ID {index}u",
            f"#define {prefix}_OFFSET {hex_u(partition['offset'])}",
            f"#define {prefix}_SIZE {hex_u(partition['size'])}",
            f"#define {prefix}_ALIGNMENT {hex_u(partition['alignment'])}",
            f"#define {prefix}_EXECUTABLE {1 if partition['executable'] else 0}u",
        ])
        for context in CONTEXTS:
            mask = sum(PERMISSIONS[value] for value in partition["permissions"][context])
            lines.append(f"#define {prefix}_{context.upper()}_PERMISSIONS {hex_u(mask)}")
        lines.append("")
    lines.append(f"#define {symbol_prefix}_PARTITION_TABLE(X) \\")
    for index, partition in enumerate(data["partitions"]):
        prefix = f"{symbol_prefix}_{partition['id']}"
        continuation = " \\" if index + 1 < len(data["partitions"]) else ""
        lines.append(
            f"    X({partition['id']}, {prefix}_ID, {prefix}_OFFSET, {prefix}_SIZE, "
            f"{prefix}_ALIGNMENT, {prefix}_BOOT_PERMISSIONS, {prefix}_APP_PERMISSIONS, "
            f"{prefix}_FACTORY_PERMISSIONS, {prefix}_EXECUTABLE){continuation}"
        )
    lines.append("")
    lines.extend([
        f"#define {symbol_prefix}_PARTITION_COUNT {len(data['partitions'])}u",
        "",
        f"#if defined(PICO_FLASH_SIZE_BYTES) && (PICO_FLASH_SIZE_BYTES != {geometry_prefix}_TOTAL_SIZE_BYTES)",
        '#error "PICO_FLASH_SIZE_BYTES disagrees with the generated FlashGeometry"',
        "#endif",
        "",
        "#endif",
        "",
    ])
    return "\n".join(lines)


def render_manifest(data: dict[str, Any]) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def render_cmake(data: dict[str, Any], source_path: Path,
                 symbol_prefix: str = "FLASH_MAP") -> str:
    geometry = data["geometry"]
    geometry_prefix = geometry_symbol_prefix(symbol_prefix)
    lines = [
        "# Generated by tools/flash_map/flash_map.py; do not edit.",
        f"# Source: {source_path.as_posix()} ({data['deployment_state']}).",
        f"set({symbol_prefix}_SCHEMA_VERSION {data['schema_version']})",
        f"set({symbol_prefix}_VERSION {data['map_version']})",
        f"set({symbol_prefix}_DEPLOYMENT_STATE {data['deployment_state']})",
        f"set({geometry_prefix}_TOTAL_SIZE_BYTES {geometry['total_size']})",
        f"set({geometry_prefix}_XIP_BASE 0x{geometry['xip_base']:08X})",
    ]
    for partition in data["partitions"]:
        prefix = f"{symbol_prefix}_{partition['id']}"
        lines.append(f"set({prefix}_OFFSET 0x{partition['offset']:08X})")
        lines.append(f"set({prefix}_SIZE 0x{partition['size']:08X})")
        lines.append(f"set({prefix}_XIP_ADDRESS 0x{geometry['xip_base'] + partition['offset']:08X})")
    return "\n".join(lines) + "\n"


def render_ld(data: dict[str, Any], source_path: Path,
              symbol_prefix: str = "FLASH_MAP") -> str:
    geometry = data["geometry"]
    geometry_prefix = geometry_symbol_prefix(symbol_prefix)
    lines = [
        "/* Generated by tools/flash_map/flash_map.py; do not edit. */",
        f"/* Source: {source_path.as_posix()} ({data['deployment_state']}). */",
        f"{geometry_prefix}_XIP_BASE = 0x{geometry['xip_base']:08X};",
        f"{geometry_prefix}_TOTAL_SIZE = 0x{geometry['total_size']:08X};",
    ]
    for partition in data["partitions"]:
        prefix = f"{symbol_prefix}_{partition['id']}"
        lines.append(f"{prefix}_ORIGIN = 0x{geometry['xip_base'] + partition['offset']:08X};")
        lines.append(f"{prefix}_LENGTH = 0x{partition['size']:08X};")
    return "\n".join(lines) + "\n"


def check_or_write(path: Path, content: str, check: bool) -> None:
    if check:
        try:
            actual = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise FlashMapError(f"generated artifact is missing: {path}: {exc}") from exc
        if actual != content:
            raise FlashMapError(f"generated artifact is stale: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--schema", type=Path)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--ld", type=Path, required=True)
    parser.add_argument("--expected-geometry", type=lambda value: int(value, 0))
    parser.add_argument("--symbol-prefix", default="FLASH_MAP")
    parser.add_argument("--header-guard", default="FLASH_MAP_V2_GENERATED_H")
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        data = load_and_validate(args.source)
        if re.fullmatch(r"[A-Z][A-Z0-9_]*", args.symbol_prefix) is None:
            raise FlashMapError("symbol prefix is invalid")
        if re.fullmatch(r"[A-Z][A-Z0-9_]*", args.header_guard) is None:
            raise FlashMapError("header guard is invalid")
        if args.schema is not None:
            schema = json.loads(args.schema.read_text(encoding="utf-8"))
            if schema.get("properties", {}).get("schema_version", {}).get("const") != data["schema_version"]:
                raise FlashMapError("schema file version disagrees with map source")
        if args.expected_geometry is not None and args.expected_geometry != data["geometry"]["total_size"]:
            raise FlashMapError(
                f"expected geometry {args.expected_geometry} disagrees with map geometry {data['geometry']['total_size']}")
        relative_source = Path("config") / args.source.name
        outputs = {
            args.header: render_header(data, relative_source,
                                       args.symbol_prefix, args.header_guard),
            args.manifest: render_manifest(data),
            args.cmake: render_cmake(data, relative_source, args.symbol_prefix),
            args.ld: render_ld(data, relative_source, args.symbol_prefix),
        }
        for path, content in outputs.items():
            check_or_write(path, content, args.check)
    except (FlashMapError, OSError, json.JSONDecodeError) as exc:
        print(f"flash_map=FAILED: {exc}", file=sys.stderr)
        return 1
    action = "checked" if args.check else "generated"
    print(
        f"flash_map=OK action={action} map_version={data['map_version']} "
        f"state={data['deployment_state']} partitions={len(data['partitions'])} "
        f"geometry={data['geometry']['total_size']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
