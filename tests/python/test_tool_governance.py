"""Offline governance contracts exercised against isolated Git worktrees.

Only the checker is imported. Inventoried scripts must never execute, including
their top-level code, and dependency hints never establish runtime validity.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "tool_governance_test_subject", ROOT / "tools/tool_governance/tool_governance.py")
governance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(governance)


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], check=True,
                          capture_output=True, text=True, encoding="utf-8")


def write(root, name, text="", *, tracked=True):
    target = root / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    if tracked:
        git(root, "add", "--", name)
    return target


def family(identity="TOOL-ALPHA", path="tools/alpha.py", role="entrypoint"):
    return dict(id=identity, purpose="Offline fixture", domain="Tools", owner="Core0 tooling",
                lifecycle="UNASSESSED", operation="UNASSESSED", entrypoints=[path],
                files=[dict(path=path, role=role)], depends_on=[], external_dependencies=[],
                dependency_review="PENDING", aliases=[], validation=[], handoff_evidence=None,
                notes="Inventory does not qualify execution")


@pytest.fixture
def repo(tmp_path):
    root = tmp_path / "repo"
    root.mkdir()
    git(root, "init", "--quiet")
    write(root, "tools/alpha.py", '"""Alpha fixture."""\nif __name__ == "__main__":\n    pass\n')
    registry = dict(schema_version=1, tools=[family()])
    schema = governance.load_json(ROOT / "tools/tool_registry.schema.json")
    return root, registry, schema


def rejected(repo, contains):
    result = governance.check(*repo)
    assert result["passed"] is False
    assert any(contains in error for error in result["errors"]), result
    return result


def layout(**overrides):
    value = dict(
        schema="TOOLS_LAYOUT_V1",
        standard_roots=["archive", "build", "common", "domains", "host", "workflows"],
        stable_roots=["tool_governance"],
        allowed_root_files=["tools/alpha.py"],
        legacy_roots=["legacy"],
        legacy_files=[],
    )
    value.update(overrides)
    return value


def test_valid_unassessed_is_inventory_not_qualification(repo):
    result = governance.check(*repo)
    assert result["passed"] and not result["errors"]
    assert result["tool_families"] == result["owned_files"] == result["visible_files"] == 1
    assert any("unassessed" in warning.lower() for warning in result["warnings"])
    assert "runtime" in result["scope"].lower()


def test_new_untracked_tool_is_discovered_and_rejected(repo):
    root, _, _ = repo
    write(root, "tools/new_tool.py", "raise RuntimeError('never execute')\n", tracked=False)
    assert "tools/new_tool.py" in governance.visible_files(root)
    assert any(item["path"] == "tools/new_tool.py" for item in governance.inventory(root)["files"])
    rejected(repo, "unregistered file: tools/new_tool.py")


def test_ignored_untracked_tool_is_not_claimed_visible(repo):
    root, _, _ = repo
    write(root, ".gitignore", "tools/scratch.py\n")
    write(root, "tools/scratch.py", "pass\n", tracked=False)
    assert "tools/scratch.py" not in governance.visible_files(root)
    assert governance.check(*repo)["passed"]


def test_layout_allows_classified_standard_path(repo):
    root, registry, schema = repo
    write(root, "tools/domains/vdc/new_tool.py", "pass\n")
    registry["tools"].append(family("TOOL-VDC", "tools/domains/vdc/new_tool.py"))
    assert governance.check(root, registry, schema, layout())["passed"]


def test_layout_rejects_new_top_level_directory(repo):
    root, registry, schema = repo
    write(root, "tools/new_family/new_tool.py", "pass\n")
    registry["tools"].append(family("TOOL-NEW", "tools/new_family/new_tool.py"))
    result = governance.check(root, registry, schema, layout())
    assert any("unclassified top-level tool directory" in error for error in result["errors"])


def test_layout_rejects_new_root_script_even_when_registered(repo):
    root, registry, schema = repo
    write(root, "tools/new_tool.py", "pass\n")
    registry["tools"].append(family("TOOL-NEW", "tools/new_tool.py"))
    result = governance.check(root, registry, schema, layout())
    assert any("unclassified tool root file" in error for error in result["errors"])


def test_layout_allows_frozen_legacy_file(repo):
    root, registry, schema = repo
    path = "tools/legacy/old_tool.py"
    write(root, path, "pass\n")
    registry["tools"].append(family("TOOL-OLD", path))
    frozen = layout(legacy_files=[path])
    assert governance.check(root, registry, schema, frozen)["passed"]


def test_layout_rejects_growth_inside_legacy_root(repo):
    root, registry, schema = repo
    path = "tools/legacy/new_tool.py"
    write(root, path, "pass\n")
    registry["tools"].append(family("TOOL-NEW", path))
    result = governance.check(root, registry, schema, layout())
    assert any("new file in frozen legacy tool root" in error for error in result["errors"])


def test_layout_allows_reduced_legacy_baseline(repo):
    assert governance.check(*repo, layout())["passed"]


@pytest.mark.parametrize("case", ["cross_family", "within_family"])
def test_duplicate_file_ownership(repo, case):
    _, registry, _ = repo
    if case == "cross_family":
        registry["tools"].append(family("TOOL-BETA"))
    else:
        registry["tools"][0]["files"].append(dict(path="tools/alpha.py", role="module"))
    rejected(repo, "duplicate ownership")


def test_duplicate_tool_id(repo):
    root, registry, _ = repo
    write(root, "tools/beta.py", "pass\n")
    registry["tools"].append(family(path="tools/beta.py"))
    rejected(repo, "duplicate tool id")


@pytest.mark.parametrize("role", ["module", "resource", "documentation"])
def test_entrypoint_requires_executable_role(repo, role):
    repo[1]["tools"][0]["files"][0]["role"] = role
    rejected(repo, "non-executable role")


def test_entrypoint_requires_ownership(repo):
    repo[1]["tools"][0]["entrypoints"] = ["tools/other.py"]
    rejected(repo, "entrypoint outside ownership")


def test_missing_registered_file(repo):
    tool = repo[1]["tools"][0]
    tool["files"].append(dict(path="tools/missing.py", role="module"))
    rejected(repo, "missing file")


@pytest.mark.parametrize("path", ["../outside.py", "/tmp/outside.py", "tools/../outside.py",
    "tools//alpha.py", "tools/./alpha.py", "tools/alpha.py/", "C:/outside.py", "tools\\alpha.py"])
def test_unsafe_and_unnormalized_paths(repo, path):
    root, registry, _ = repo
    assert not governance.safe_path(root, path)
    registry["tools"][0]["files"][0]["path"] = path
    registry["tools"][0]["entrypoints"] = [path]
    assert not governance.check(*repo)["passed"]


def test_file_outside_tools_is_not_a_tool_owner(repo):
    root, registry, _ = repo
    write(root, "other.py", "pass\n")
    registry["tools"].append(family("TOOL-OTHER", "other.py"))
    rejected(repo, "unsafe tool path")


def test_symlink_cannot_escape_repository(repo, tmp_path):
    root, registry, _ = repo
    external = tmp_path / "external.py"
    external.write_text("SECRET_SHOULD_NOT_BE_READ\n", encoding="utf-8")
    link = root / "tools/escape.py"
    try:
        link.symlink_to(external)
    except (OSError, NotImplementedError) as exc:
        pytest.skip(f"Symlink unavailable on this platform: {exc}")
    assert not governance.safe_path(root, "tools/escape.py")
    item = governance.file_inventory(root, "tools/escape.py")
    assert "error" in item and "sha256" not in item
    assert "SECRET_SHOULD_NOT_BE_READ" not in json.dumps(item)
    registry["tools"].append(family("TOOL-ESCAPE", "tools/escape.py"))
    rejected(repo, "unsafe tool path")


@pytest.mark.parametrize("case", ["version", "unknown_field", "missing_owner", "unknown_role"])
def test_registry_schema_rejects_invalid_shapes(repo, case):
    registry = repo[1]
    if case == "version":
        registry["schema_version"] = 99
    elif case == "unknown_field":
        registry["tools"][0]["unknown"] = True
    elif case == "missing_owner":
        del registry["tools"][0]["owner"]
    else:
        registry["tools"][0]["files"][0]["role"] = "MAGIC"
    rejected(repo, "schema")


def test_invalid_schema_is_rejected_before_repository_access(repo, monkeypatch):
    monkeypatch.setattr(governance, "visible_files", lambda root: pytest.fail("Schema must fail first"))
    result = governance.check(repo[0], repo[1], {
        "$id": repo[2]["$id"], "type": "not-a-json-schema-type"})
    assert not result["passed"]
    assert any("schema" in error.lower() for error in result["errors"])


def test_unresolved_schema_reference_does_not_fetch_network(repo, monkeypatch):
    import socket
    monkeypatch.setattr(socket, "create_connection", lambda *a, **kw: pytest.fail("No remote schema retrieval"))
    result = governance.check(repo[0], repo[1], {
        "$id": repo[2]["$id"], "$ref": "https://invalid.example/schema.json"})
    assert not result["passed"] and result["errors"]
    assert any("Unresolvable" in error for error in result["errors"])


@pytest.mark.parametrize("schema", [{}, None, [], {"$id": "urn:other:registry:v1"}])
def test_missing_or_wrong_schema_identity_fails_before_repository_access(repo, monkeypatch, schema):
    monkeypatch.setattr(governance, "visible_files", lambda root: pytest.fail("Identity must fail first"))
    result = governance.check(repo[0], {}, schema)
    assert not result["passed"]
    assert any("schema identity" in error for error in result["errors"])


@pytest.mark.parametrize("registry", [{}, None, [], {"tools": [None]}, {"tools": [{"id": []}]}])
def test_schema_with_identity_but_no_constraints_cannot_raise_on_malformed_registry(repo, registry):
    result = governance.check(repo[0], registry, {"$id": repo[2]["$id"]})
    assert result["passed"] is False and result["errors"]
    assert any("invalid registry/schema input" in error for error in result["errors"])


@pytest.mark.parametrize("suffix", ["\n", "\r", "\r\n"])
@pytest.mark.parametrize("field", ["domain", "owner", "id", "path"])
def test_schema_rejects_trailing_newline_in_catalog_fields(repo, monkeypatch, field, suffix):
    tool = repo[1]["tools"][0]
    if field == "path":
        tool["entrypoints"][0] += suffix
        tool["files"][0]["path"] += suffix
    else:
        tool[field] += suffix
    monkeypatch.setattr(governance, "visible_files", lambda root: pytest.fail("Schema must fail first"))
    rejected(repo, "schema")


def test_cli_bad_schema_returns_json_failure(repo):
    import sys
    root, registry, _ = repo
    write(root, governance.REGISTRY, json.dumps(registry))
    write(root, governance.SCHEMA, '{"type":"not-a-json-schema-type"}')
    result = subprocess.run([sys.executable, str(ROOT / "tools/tool_governance/tool_governance.py"),
        "check", "--root", str(root)], capture_output=True, text=True, encoding="utf-8", timeout=15)
    assert result.returncode != 0
    parsed = json.loads(result.stdout)
    assert parsed["passed"] is False and parsed["errors"]
    assert "Traceback" not in result.stderr


@pytest.mark.parametrize("content", ['{"a":1,"a":2}', '{"nested":{"x":1,"x":2}}'])
def test_load_json_rejects_duplicate_keys(tmp_path, content):
    path = tmp_path / "duplicate.json"
    path.write_text(content, encoding="utf-8")
    with pytest.raises(ValueError, match="duplicate JSON key"):
        governance.load_json(path)


def test_load_json_utf8_and_syntax_errors(tmp_path):
    path = tmp_path / "data.json"
    path.write_text('{"名称":"诊断工具","values":[0,null,true]}', encoding="utf-8")
    assert governance.load_json(path) == {"名称": "诊断工具", "values": [0, None, True]}
    path.write_text('{"incomplete":', encoding="utf-8")
    with pytest.raises(ValueError):
        governance.load_json(path)


def test_alias_target_requires_registered_owner(repo):
    tool = repo[1]["tools"][0]
    tool["files"][0]["role"] = "compatibility_wrapper"
    tool["aliases"] = [dict(path="tools/alpha.py", target="tools/missing.py")]
    rejected(repo, "alias target not registered")


def test_alias_requires_wrapper_role(repo):
    repo[1]["tools"][0]["aliases"] = [dict(path="tools/alpha.py", target="tools/alpha.py")]
    rejected(repo, "alias is not an owned compatibility wrapper")


@pytest.mark.parametrize("case", ["self", "cycle", "duplicate"])
def test_aliases_reject_self_cycles_and_duplicate_sources(repo, case):
    root, registry, _ = repo
    tool = registry["tools"][0]
    tool["files"][0]["role"] = "compatibility_wrapper"
    write(root, "tools/beta.py", "pass\n")
    registry["tools"].append(family("TOOL-BETA", "tools/beta.py", "compatibility_wrapper"))
    target = "tools/alpha.py" if case == "self" else "tools/beta.py"
    tool["aliases"] = [dict(path="tools/alpha.py", target=target)]
    if case == "cycle":
        registry["tools"][1]["aliases"] = [dict(path="tools/beta.py", target="tools/alpha.py")]
    elif case == "duplicate":
        tool["aliases"].append(dict(path="tools/alpha.py", target="tools/beta.py"))
    rejected(repo, "duplicate alias" if case == "duplicate" else "alias cycle")


def test_alias_chain_to_real_entrypoint_remains_valid(repo):
    root, registry, _ = repo
    tool = registry["tools"][0]
    tool["files"][0]["role"] = "compatibility_wrapper"
    tool["aliases"] = [dict(path="tools/alpha.py", target="tools/beta.py")]
    write(root, "tools/beta.py", "pass\n")
    write(root, "tools/final.py", "pass\n")
    middle = family("TOOL-BETA", "tools/beta.py", "compatibility_wrapper")
    middle["aliases"] = [dict(path="tools/beta.py", target="tools/final.py")]
    registry["tools"].extend([middle, family("TOOL-FINAL", "tools/final.py")])
    assert governance.check(*repo)["passed"]


@pytest.mark.parametrize("dependency", ["TOOL-UNKNOWN", "TOOL-ALPHA"])
def test_dependency_requires_distinct_registered_family(repo, dependency):
    repo[1]["tools"][0]["depends_on"] = [dependency]
    rejected(repo, "invalid dependency")


def qualify(root, registry):
    write(root, "tests/validation.txt", "validation fixture\n")
    write(root, "docs/handoff.txt", "handoff fixture\n")
    tool = registry["tools"][0]
    tool.update(lifecycle="SUPPORTED", operation="OFFLINE", dependency_review="COMPLETE",
                validation=["tests/validation.txt"], handoff_evidence="docs/handoff.txt")
    return tool


def test_supported_requires_explicit_evidence(repo):
    qualify(repo[0], repo[1])
    assert governance.check(*repo)["passed"]


@pytest.mark.parametrize("field", ["validation", "handoff_evidence"])
def test_supported_rejects_empty_proof_reference(repo, field):
    root, registry, _ = repo
    empty = "tests/empty-proof.txt" if field == "validation" else "docs/empty-proof.txt"
    write(root, empty, "")
    tool = qualify(root, registry)
    tool[field] = [empty] if field == "validation" else empty
    rejected(repo, f"empty {('validation' if field == 'validation' else 'handoff evidence')} reference")


@pytest.mark.parametrize("field,value", [("dependency_review", "PENDING"), ("validation", []),
    ("handoff_evidence", None), ("operation", "UNASSESSED")])
def test_supported_rejects_incomplete_qualification(repo, field, value):
    qualify(repo[0], repo[1])[field] = value
    rejected(repo, "unsupported qualification claim")


@pytest.mark.parametrize("field", ["validation", "handoff_evidence"])
@pytest.mark.parametrize("path", ["docs/absent.txt", "../outside.txt"])
def test_evidence_requires_safe_existing_file(repo, field, path):
    tool = qualify(repo[0], repo[1])
    tool[field] = [path] if field == "validation" else path
    rejected(repo, "missing/unsafe")


def test_static_inventory_never_executes_top_level_or_dynamic_loader(repo):
    root, _, _ = repo
    sentinel = root / "executed.txt"
    source = (f'from pathlib import Path\nPath({str(sentinel)!r}).write_text("executed")\n'
              'raise RuntimeError("top-level must not execute")\n'
              'if __name__ == "__main__":\n    pass\n')
    write(root, "tools/hostile.py", source)
    output = governance.inventory(root)
    assert not sentinel.exists()
    item = next(item for item in output["files"] if item["path"] == "tools/hostile.py")
    assert item["role"] == "entrypoint"
    assert item["sha256"] == hashlib.sha256((root / "tools/hostile.py").read_bytes()).hexdigest()


def test_dynamic_and_out_references_remain_hints_not_resolved_dependencies(repo):
    root, _, _ = repo
    write(root, "tools/loader.py", 'import importlib.util\n'
          'spec = importlib.util.spec_from_file_location("private", "out/helper.py")\n'
          'spec.loader.exec_module(module)\n'
          'destination = "out/HardwareAcceptance/20260920/report.json"\n')
    write(root, "out/helper.py", "raise RuntimeError('do not load')\n")
    item = next(item for item in governance.inventory(root)["files"] if item["path"] == "tools/loader.py")
    assert item["review_hints"]["dynamic_loader"] == [2]
    assert item["review_hints"]["out_reference"] == [2, 4]
    assert not item["local_dependency_candidates"]
    assert "dependencies" not in item and "resolved_dependencies" not in item
    assert isinstance(item["review_hints"]["out_reference"][0], int)


def test_import_candidates_and_identical_bytes_are_explicitly_provisional(repo):
    root, _, _ = repo
    write(root, "tools/lib.py", '"""Shared fixture."""\n')
    write(root, "tools/copy.py", '"""Shared fixture."""\n')
    write(root, "tools/importer.py", "import tools.lib\n")
    result = governance.inventory(root)
    item = next(item for item in result["files"] if item["path"] == "tools/importer.py")
    assert item["local_dependency_candidates"] == ["tools/lib.py"]
    assert ["tools/copy.py", "tools/lib.py"] in result["identical_content_groups"]
    assert any("Static candidates" in value for value in result["limitations"])
    assert any("interchangeable" in value for value in result["limitations"])
    assert result == governance.inventory(root)


def test_inventory_syntax_failure_is_evidence_not_execution(repo):
    root, _, _ = repo
    write(root, "tools/broken.py", "def broken(:\n")
    item = next(item for item in governance.inventory(root)["files"] if item["path"] == "tools/broken.py")
    assert "error" in item and item["role"] == "module"


@pytest.mark.parametrize("source,target", [
    ("from . import lib\n", "tools/pkg/lib.py"),
    ("from .. import lib\n", "tools/lib.py"),
    ("from .child import lib\n", "tools/pkg/child/lib.py"),
])
def test_relative_import_candidates_keep_correct_package_level(repo, source, target):
    root, _, _ = repo
    write(root, target, "raise RuntimeError('static only')\n")
    write(root, "tools/pkg/importer.py", source)
    item = next(item for item in governance.inventory(root)["files"]
                if item["path"] == "tools/pkg/importer.py")
    assert item["local_dependency_candidates"] == [target]


def test_binary_resource_is_hashed_without_text_decode(repo):
    root, registry, _ = repo
    path = root / "tools/calibration.bin"
    payload = bytes(range(256))
    path.write_bytes(payload)
    git(root, "add", "--", "tools/calibration.bin")
    registry["tools"][0]["files"].append(dict(path="tools/calibration.bin", role="resource"))
    item = next(item for item in governance.inventory(root)["files"]
                if item["path"] == "tools/calibration.bin")
    assert "error" not in item
    assert item["role"] == "resource" and item["review_hints"] == {}
    assert item["sha256"] == hashlib.sha256(payload).hexdigest()
    assert item["content_review"] == "opaque resource; hash only"
    assert governance.check(*repo)["passed"]


@pytest.mark.parametrize("suffix", [".py", ".md", ".ps1", ".cmd", ".json", ".txt", ".scpi", ".c"])
def test_inventory_source_text_still_requires_utf8(repo, suffix):
    root, _, _ = repo
    path = "tools/invalid" + suffix
    (root / path).write_bytes(b"\xff\xfe\x80")
    item = governance.file_inventory(root, path)
    assert "UnicodeDecodeError" in item["error"]
    assert "sha256" in item


def test_catalog_is_stable_sorted_and_does_not_mutate_registry(repo):
    registry = repo[1]
    library = family("TOOL-ZETA", "tools/zeta.py", "module")
    library["entrypoints"] = []
    registry["tools"].insert(0, library)
    before = copy.deepcopy(registry)
    output = governance.catalog(registry)
    assert registry == before
    assert output == governance.catalog(registry)
    registry["tools"].reverse()
    assert output == governance.catalog(registry)
    assert output.index("TOOL-ALPHA") < output.index("TOOL-ZETA")
    assert "Library / resources" in output and "UNASSESSED" in output
    assert output.endswith("<!-- END TOOL REGISTRY CATALOG -->\n")


def test_catalog_block_allows_manual_surrounding_notes(repo):
    root, registry, _ = repo
    write(root, "tools/README.md", "# 人工说明\n\n" + governance.catalog(registry) + "\n维护说明。\n")
    assert governance.catalog_errors(root, registry) == []


@pytest.mark.parametrize("case", ["no_markers", "missing_begin", "missing_end", "duplicate_begin",
    "duplicate_end", "reversed", "stale"])
def test_catalog_drift_is_rejected(repo, case):
    root, registry, _ = repo
    text = governance.catalog(registry)
    begin, end = "<!-- BEGIN TOOL REGISTRY CATALOG -->", "<!-- END TOOL REGISTRY CATALOG -->"
    if case == "no_markers":
        text = "# Manual documentation only\n"
    elif case == "missing_begin":
        text = text.replace(begin, "")
    elif case == "missing_end":
        text = text.replace(end, "")
    elif case == "duplicate_begin":
        text += begin
    elif case == "duplicate_end":
        text += end
    elif case == "reversed":
        text = end + "\n" + begin
    else:
        text = text.replace("TOOL-ALPHA", "TOOL-OLD")
    write(root, "tools/README.md", text)
    errors = governance.catalog_errors(root, registry)
    assert errors and ("differs" in errors[0] if case == "stale" else "markers" in errors[0])


def test_cli_optional_catalog_check_does_not_affect_default(repo, capsys):
    root, registry, schema = repo
    registry["tools"][0]["files"].extend([
        dict(path=governance.REGISTRY, role="resource"),
        dict(path=governance.SCHEMA, role="resource"),
        dict(path=governance.LAYOUT, role="resource"),
        dict(path="tools/README.md", role="documentation")])
    write(root, governance.REGISTRY, json.dumps(registry))
    write(root, governance.SCHEMA, json.dumps(schema))
    write(root, governance.LAYOUT, json.dumps(layout(allowed_root_files=sorted([
        "tools/README.md", "tools/alpha.py", governance.LAYOUT,
        governance.REGISTRY, governance.SCHEMA,
    ]))))
    write(root, "tools/README.md", "manual notes, catalog not yet generated\n")
    assert governance.main(["check", "--root", str(root)]) == 0
    assert json.loads(capsys.readouterr().out)["passed"]
    assert governance.main(["check", "--root", str(root), "--check-catalog"]) == 1
    assert not json.loads(capsys.readouterr().out)["passed"]
    write(root, "tools/README.md", "# 导航\n\n" + governance.catalog(registry))
    assert governance.main(["check", "--root", str(root), "--check-catalog"]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["passed"] and result["warnings"]


def test_actual_registry_and_catalog_are_in_sync(capsys):
    assert governance.main(["check", "--root", str(ROOT), "--check-catalog"]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["passed"] and not result["errors"]
    assert result["owned_files"] == result["visible_files"] > 0
