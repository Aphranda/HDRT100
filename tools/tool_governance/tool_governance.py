#!/usr/bin/env python3
"""Inventory and check repository tools without importing or running them.

inventory: Git-visible file hashes, roles and static dependency hints.
check: registry/schema, ownership and explicit dependency validation.
catalog: deterministic Markdown navigation from the registry.
No command opens a device, imports a registered entrypoint or writes files.
Redirect stdout to an evidence file when persistence is needed.
"""
from __future__ import annotations

import argparse
import ast
from collections import defaultdict
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = "tools/tool_registry.json"
SCHEMA = "tools/tool_registry.schema.json"
LAYOUT = "tools/tool_layout.json"
WRAPPERS = {
    "tools/check_scpi_usb_namespace.py": "tools/checks/check_scpi_usb_namespace.py",
    "tools/rp2350_tk_toolbox.py": "tools/bench/rp2350_tk_toolbox.py",
    **{f"tools/run_{name}_tests.ps1": f"tools/tests/run_{name}_tests.ps1"
       for name in ("biss_protocol", "portable_log", "portable_ota")},
}


def visible_files(root: Path) -> list[str]:
    """Include new nonignored files so adding an unregistered tool is visible."""
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
         "--exclude-standard", "--", "tools"], check=True, capture_output=True)
    return sorted(set(result.stdout.decode("utf-8").strip("\0").split("\0")) - {""})


def safe_path(root: Path, value: str) -> bool:
    p = PurePosixPath(value)
    return bool(value and "\\" not in value and ":" not in value and
                not p.is_absolute() and ".." not in p.parts and
                p.as_posix() == value and (root / value).resolve().is_relative_to(root.resolve()))


def file_inventory(root: Path, path: str) -> dict:
    item = {"path": path, "role": "resource"}
    if not safe_path(root, path):
        return dict(item, error="path escapes repository or is not normalized")
    try:
        data = (root / path).read_bytes()
    except OSError as exc:
        return dict(item, error=f"{type(exc).__name__}: {exc}")
    item["sha256"] = hashlib.sha256(data).hexdigest()
    suffix = Path(path).suffix
    if suffix not in (".py", ".md", ".ps1", ".cmd", ".json", ".txt", ".scpi", ".c"):
        return dict(item, review_hints={}, content_review="opaque resource; hash only")
    try:
        text = data.decode("utf-8-sig")
    except UnicodeError as exc:
        return dict(item, error=f"{type(exc).__name__}: {exc}")
    if suffix == ".md":
        item["role"] = "documentation"
    elif suffix in (".ps1", ".cmd"):
        item["role"] = "test_runner" if path.startswith("tools/tests/") else "entrypoint"
    elif suffix == ".py":
        try:
            tree = ast.parse(text, filename=path)
        except SyntaxError as exc:
            return dict(item, role="module", error=str(exc))
        item["role"] = "module"
        # A static entrypoint candidate, not a claim of safe import or safe --help.
        if any(isinstance(n, ast.If) and isinstance(n.test, ast.Compare) and
               isinstance(n.test.left, ast.Name) and n.test.left.id == "__name__" and
               any(isinstance(c, ast.Constant) and c.value == "__main__" for c in n.test.comparators)
               for n in tree.body):
            item["role"] = "entrypoint"
        if any(isinstance(n, (ast.With, ast.AsyncWith)) for n in tree.body):
            item["role"] = "entrypoint"
            item["top_level_execution_review"] = True
        item["description"] = (ast.get_docstring(tree) or "").split("\n")[0]
        imports = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imports.extend(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom):
                prefix = "." * node.level + (node.module or "")
                imports.append(prefix)
                imports.extend(prefix + ("" if prefix.endswith(".") else ".") + alias.name for alias in node.names
                               if alias.name != "*")
        item["import_hints"] = sorted(set(imports))
    if path in WRAPPERS:
        item.update(role="compatibility_wrapper", forwards_to=WRAPPERS[path])
    # Keep locations only: never emit source lines that may contain local secrets.
    patterns = {
        "dynamic_loader": r"spec_from_file_location|runpy\.|import_module\(",
        "out_reference": r"out[/\\]|['\"]out['\"]",
        "historical_evidence_reference": r"HardwareAcceptance[/\\]20\d{6}",
        "absolute_windows_path": r"[A-Za-z]:[/\\]",
        "environment_access": r"getenv\(|environ(?:\[|\.)|\$env:",
        "device_api": r"serial\.Serial|ResourceManager\(|open_resource\(",
    }
    item["review_hints"] = {name: [i for i, line in enumerate(text.splitlines(), 1)
                                  if re.search(pattern, line)]
                            for name, pattern in patterns.items()}
    item["review_hints"] = {k: v for k, v in item["review_hints"].items() if v}
    return item


def inventory(root: Path) -> dict:
    files = [file_inventory(root, path) for path in visible_files(root)]
    paths = {item["path"] for item in files}
    hashes = defaultdict(list)
    modules = defaultdict(list)
    for item in files:
        if "sha256" in item:
            hashes[item["sha256"]].append(item["path"])
        if item["path"].endswith(".py"):
            stem = item["path"][:-3].replace("/", ".")
            modules[stem].append(item["path"])
            modules[stem.removeprefix("tools.")].append(item["path"])
            modules[Path(item["path"]).stem].append(item["path"])
    for item in files:
        candidates = set()
        for hint in item.get("import_hints", []):
            candidates.update(modules.get(hint, []))
            if hint.startswith("."):
                level = len(hint) - len(hint.lstrip("."))
                parts = PurePosixPath(item["path"]).parent.parts
                prefix = parts[:len(parts) - level + 1] if level <= len(parts) else ()
                candidate = str(PurePosixPath(*prefix) / (hint.lstrip(".").replace(".", "/") + ".py"))
                if prefix and candidate in paths:
                    candidates.add(candidate)
        item["local_dependency_candidates"] = sorted(candidates - {item["path"]})
    return {
        "schema": "TOOLS_INVENTORY_V1", "file_count": len(files), "files": files,
        "identical_content_groups": [paths for paths in hashes.values() if len(paths) > 1],
        "limitations": ["Static candidates only; dynamic imports, subprocess calls and shell paths need review.",
                        "Out references include valid output destinations; no automatic violation inference.",
                        "Identical bytes do not imply tools have interchangeable behavior.",
                        "Git-visible working files, including new nonignored files; not a staged-source receipt."],
    }


def load_json(path: Path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs)


def _proof_reference_error(root: Path, path: str, label: str) -> str | None:
    """Require proof references to contain bytes, not only to exist."""
    try:
        if (root / path).stat().st_size == 0:
            return f"empty {label} reference: {path}"
    except OSError as exc:
        return f"unreadable {label} reference: {path}: {type(exc).__name__}"
    return None


def check(root: Path, registry: dict, schema: dict, layout: dict | None = None) -> dict:
    try:
        result = _check_registry(root, registry, schema)
        if layout is not None:
            result["errors"].extend(layout_errors(root, layout))
            result["passed"] = not result["errors"]
        return result
    except (KeyError, TypeError, AttributeError, ValueError) as exc:
        return dict(passed=False, errors=[f"invalid registry/schema input: {type(exc).__name__}: {exc}"], warnings=[])


def _check_registry(root: Path, registry: dict, schema: dict) -> dict:
    try:
        from jsonschema import Draft202012Validator
        from referencing import Registry
    except ImportError:  # C2: pre-commit must run without third-party packages
        Draft202012Validator = None
        Registry = None

    errors, warnings = [], []
    if not isinstance(schema, dict) or schema.get("$id") != "urn:hdrt100:tools-registry:v1":
        return dict(passed=False, errors=["schema identity missing or unsupported"], warnings=[])
    if Draft202012Validator is None:
        warnings.append(
            "jsonschema not installed: schema validation skipped "
            "(ownership/layout/catalog checks still run)")
    else:
        try:
            Draft202012Validator.check_schema(schema)
            validator = Draft202012Validator(schema, registry=Registry())
            errors.extend(f"schema {list(e.absolute_path)}: {e.message}"
                          for e in validator.iter_errors(registry))
        except Exception as exc:
            # Invalid schemas and unresolved references must fail closed. An empty
            # resource registry never retrieves remote schema documents.
            errors.append(f"schema validation failed: {type(exc).__name__}: {exc}")
    if errors:
        return dict(passed=False, errors=errors, warnings=warnings)
    ids, ownership = {}, {}
    visible = set(visible_files(root))
    for tool in registry["tools"]:
        identity = tool["id"]
        if identity in ids:
            errors.append(f"duplicate tool id: {identity}")
        ids[identity] = tool
        owned = {f["path"] for f in tool["files"]}
        for field in tool["files"]:
            path = field["path"]
            if not safe_path(root, path) or not path.startswith("tools/"):
                errors.append(f"unsafe tool path: {path}")
                continue
            if path in ownership:
                errors.append(f"duplicate ownership: {path}")
            ownership[path] = identity
            if not (root / path).is_file():
                errors.append(f"missing file: {path}")
            if path not in visible:
                errors.append(f"file not Git-visible: {path}")
        for entry in tool["entrypoints"]:
            if entry not in owned:
                errors.append(f"entrypoint outside ownership: {identity}: {entry}")
            elif next(f["role"] for f in tool["files"] if f["path"] == entry) not in (
                    "entrypoint", "compatibility_wrapper", "test_runner"):
                errors.append(f"entrypoint has non-executable role: {entry}")
        for evidence in tool["validation"]:
            if not safe_path(root, evidence) or not (root / evidence).is_file():
                errors.append(f"missing/unsafe validation reference: {identity}: {evidence}")
            else:
                proof_error = _proof_reference_error(root, evidence, "validation")
                if proof_error:
                    errors.append(f"{identity}: {proof_error}")
        if tool["lifecycle"] == "UNASSESSED":
            warnings.append(f"unassessed legacy tool family: {identity}")
        if tool["dependency_review"] != "COMPLETE":
            warnings.append(f"dependency review pending: {identity}")
        if tool["lifecycle"] == "SUPPORTED" and (
                tool["dependency_review"] != "COMPLETE" or not tool["validation"] or
                not tool["handoff_evidence"] or tool["operation"] == "UNASSESSED"):
            errors.append(f"unsupported qualification claim: {identity}")
        if tool["handoff_evidence"] and (not safe_path(root, tool["handoff_evidence"]) or
                                        not (root / tool["handoff_evidence"]).is_file()):
            errors.append(f"missing/unsafe handoff evidence: {identity}")
        elif tool["handoff_evidence"]:
            proof_error = _proof_reference_error(root, tool["handoff_evidence"],
                                                 "handoff evidence")
            if proof_error:
                errors.append(f"{identity}: {proof_error}")
    alias_targets = {}
    for tool in registry["tools"]:
        for dep in tool["depends_on"]:
            if dep not in ids or dep == tool["id"]:
                errors.append(f"invalid dependency: {tool['id']} -> {dep}")
        for alias in tool["aliases"]:
            if alias["path"] in alias_targets:
                errors.append(f"duplicate alias: {alias['path']}")
            alias_targets[alias["path"]] = alias["target"]
            if alias["path"] not in {f["path"] for f in tool["files"] if f["role"] == "compatibility_wrapper"}:
                errors.append(f"alias is not an owned compatibility wrapper: {alias['path']}")
            if alias["target"] not in ownership:
                errors.append(f"alias target not registered: {alias['target']}")
            else:
                target_owner = ids[ownership[alias["target"]]]
                target_role = next(
                    field["role"] for field in target_owner["files"]
                    if field["path"] == alias["target"])
                if (alias["target"] not in target_owner["entrypoints"] and
                        target_role != "module"):
                    errors.append(
                        f"alias target is not an entrypoint or module: {alias['target']}")
    for path in alias_targets:
        seen, cursor = set(), path
        while cursor in alias_targets:
            if cursor in seen:
                errors.append(f"alias cycle: {path}")
                break
            seen.add(cursor)
            cursor = alias_targets[cursor]
    errors.extend(f"unregistered file: {p}" for p in sorted(visible - set(ownership)))
    return dict(passed=not errors, errors=errors, warnings=warnings,
                tool_families=len(ids), owned_files=len(ownership), visible_files=len(visible),
                scope="Structure/ownership only. No entrypoint execution, runtime dependency or hardware qualification.")


def layout_errors(root: Path, layout: dict) -> list[str]:
    """Reject growth outside classified roots and inside frozen legacy roots."""
    required = {
        "schema", "standard_roots", "stable_roots", "allowed_root_files",
        "legacy_roots", "legacy_files",
    }
    if not isinstance(layout, dict) or set(layout) != required:
        return ["invalid tool layout fields"]
    if layout["schema"] != "TOOLS_LAYOUT_V1":
        return ["unsupported tool layout schema"]

    errors = []
    list_fields = required - {"schema"}
    for field in sorted(list_fields):
        values = layout[field]
        if (not isinstance(values, list) or any(not isinstance(value, str) for value in values)
                or len(values) != len(set(values)) or values != sorted(values)):
            errors.append(f"tool layout {field} must be a sorted unique string list")
    if errors:
        return errors

    roots = {}
    for field in ("standard_roots", "stable_roots", "legacy_roots"):
        for value in layout[field]:
            if (not value or "/" in value or "\\" in value or value in roots
                    or not re.fullmatch(r"[a-z][a-z0-9_]*", value)):
                errors.append(f"invalid or duplicate tool layout root: {value}")
            else:
                roots[value] = field

    root_files = set(layout["allowed_root_files"])
    for path in root_files:
        parts = PurePosixPath(path).parts
        if not safe_path(root, path) or len(parts) != 2 or parts[0] != "tools":
            errors.append(f"invalid allowed tool root file: {path}")

    legacy_roots = set(layout["legacy_roots"])
    legacy_files = set(layout["legacy_files"])
    for path in legacy_files:
        parts = PurePosixPath(path).parts
        if (not safe_path(root, path) or len(parts) < 3 or parts[0] != "tools"
                or parts[1] not in legacy_roots):
            errors.append(f"legacy baseline path outside a legacy root: {path}")

    if errors:
        return errors

    for path in visible_files(root):
        parts = PurePosixPath(path).parts
        if len(parts) == 2:
            if path not in root_files:
                errors.append(f"unclassified tool root file: {path}")
            continue
        top = parts[1]
        category = roots.get(top)
        if category is None:
            errors.append(f"unclassified top-level tool directory: tools/{top}/")
        elif category == "legacy_roots" and path not in legacy_files:
            errors.append(f"new file in frozen legacy tool root: {path}")
    return errors


def catalog(registry: dict) -> str:
    lines = ["<!-- BEGIN TOOL REGISTRY CATALOG -->",
             "Generated from `tools/tool_registry.json`; do not edit these rows manually.",
             "UNASSESSED means inventoried, not qualified or deprecated. IDs denote tool families; every entry remains explicit.",
             "", "| ID | Domain / owner | Lifecycle | Entry points |",
             "|---|---|---|---|"]
    for tool in sorted(registry["tools"], key=lambda row: row["id"]):
        entries = "<br>".join(f"`{p}`" for p in tool["entrypoints"]) or "Library / resources"
        lines.append(f"| {tool['id']} | {tool['domain']} / {tool['owner']} | {tool['lifecycle']} | {entries} |")
    return "\n".join(lines + ["<!-- END TOOL REGISTRY CATALOG -->"]) + "\n"


def catalog_errors(root: Path, registry: dict) -> list[str]:
    text = (root / "tools/README.md").read_text(encoding="utf-8")
    begin = "<!-- BEGIN TOOL REGISTRY CATALOG -->"
    end = "<!-- END TOOL REGISTRY CATALOG -->"
    if text.count(begin) != 1 or text.count(end) != 1 or text.index(begin) > text.index(end):
        return ["catalog markers missing, duplicated or out of order"]
    actual = text[text.index(begin):text.index(end) + len(end)] + "\n"
    return [] if actual == catalog(registry) else ["tools/README.md catalog differs from registry"]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("inventory", "check", "catalog"))
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check-catalog", action="store_true",
                        help="with check, also verify the generated README block")
    parser.add_argument("--quiet", action="store_true",
                        help="with check, print the summary and first errors only")
    args = parser.parse_args(argv)
    if args.check_catalog and args.command != "check":
        parser.error("--check-catalog is only valid with check")
    try:
        if args.command == "inventory":
            result = inventory(args.root)
            code = int(any("error" in item for item in result["files"]))
        else:
            registry = load_json(args.root / REGISTRY)
            result = check(args.root, registry, load_json(args.root / SCHEMA),
                           load_json(args.root / LAYOUT))
            if args.check_catalog and result["passed"]:
                result["errors"].extend(catalog_errors(args.root, registry))
                result["passed"] = not result["errors"]
            code = int(not result["passed"])
            if args.command == "catalog" and not code:
                print(catalog(registry), end="")
                return 0
        if args.quiet:
            print(f"SUMMARY {'PASS' if result['passed'] else 'FAIL'} "
                  f"errors={len(result['errors'])} warnings={len(result['warnings'])}")
            for error in result["errors"][:8]:
                print(f"  FAIL {error}")
            return code
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return code
    except (ValueError, OSError, ImportError, subprocess.SubprocessError) as exc:
        print(json.dumps({"passed": False, "errors": [str(exc)]}, ensure_ascii=False))
        return 2


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
