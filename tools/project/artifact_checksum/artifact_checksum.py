#!/usr/bin/env python3
"""Generate and verify SHA-256 manifests for immutable firmware artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


class ArtifactChecksumError(ValueError):
    """Raised when an artifact checksum manifest is invalid or drifts."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise ArtifactChecksumError(f"cannot read artifact {path}: {exc}") from exc
    return digest.hexdigest()


def _git_revision(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def build_manifest(*, root: Path, artifact: Path, label: str, source_commit: str) -> dict[str, Any]:
    resolved = artifact if artifact.is_absolute() else root / artifact
    if not resolved.is_file():
        raise ArtifactChecksumError(f"artifact not found: {resolved}")
    try:
        size = resolved.stat().st_size
    except OSError as exc:
        raise ArtifactChecksumError(f"cannot stat artifact {resolved}: {exc}") from exc
    try:
        relative = resolved.relative_to(root).as_posix()
    except ValueError:
        relative = str(resolved)
    return {
        "schema": 1,
        "product": "DHRT100",
        "label": label,
        "artifact": relative,
        "size": size,
        "sha256": _sha256(resolved),
        "source_commit": source_commit,
        "generated_commit": _git_revision(root),
    }


def verify_manifest(*, root: Path, manifest_path: Path, artifact: Path | None = None) -> dict[str, Any]:
    path = manifest_path if manifest_path.is_absolute() else root / manifest_path
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ArtifactChecksumError(f"cannot read checksum manifest {path}: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("schema") != 1:
        raise ArtifactChecksumError("checksum manifest schema is invalid")
    expected_hash = manifest.get("sha256")
    expected_size = manifest.get("size")
    if not isinstance(expected_hash, str) or len(expected_hash) != 64:
        raise ArtifactChecksumError("checksum manifest sha256 is invalid")
    if not isinstance(expected_size, int) or expected_size < 0:
        raise ArtifactChecksumError("checksum manifest size is invalid")
    manifest_artifact = manifest.get("artifact")
    if not isinstance(manifest_artifact, str):
        raise ArtifactChecksumError("checksum manifest artifact is invalid")
    resolved = artifact if artifact is not None else Path(manifest_artifact)
    if not resolved.is_absolute():
        resolved = root / resolved
    actual_size = resolved.stat().st_size if resolved.is_file() else -1
    actual_hash = _sha256(resolved) if actual_size >= 0 else ""
    if actual_size != expected_size or actual_hash.lower() != expected_hash.lower():
        raise ArtifactChecksumError(
            f"artifact checksum drifted: expected size={expected_size} sha256={expected_hash}, "
            f"got size={actual_size} sha256={actual_hash}"
        )
    result = dict(manifest)
    result["verified_artifact"] = str(resolved)
    result["verified_size"] = actual_size
    result["verified_sha256"] = actual_hash
    return result


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    generate = sub.add_parser("generate", help="generate a checksum manifest")
    generate.add_argument("--root", type=Path, default=Path.cwd())
    generate.add_argument("--artifact", required=True, type=Path)
    generate.add_argument("--label", default="factory-recovery")
    generate.add_argument("--source-commit", required=True)
    generate.add_argument("--output", required=True, type=Path)
    verify = sub.add_parser("verify", help="verify an artifact against a manifest")
    verify.add_argument("--root", type=Path, default=Path.cwd())
    verify.add_argument("--manifest", required=True, type=Path)
    verify.add_argument("--artifact", type=Path)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    root = args.root.resolve()
    try:
        if args.command == "generate":
            manifest = build_manifest(
                root=root,
                artifact=args.artifact,
                label=args.label,
                source_commit=args.source_commit,
            )
            output = args.output if args.output.is_absolute() else root / args.output
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"artifact_checksum=GENERATED output={output} sha256={manifest['sha256']}")
        else:
            manifest = verify_manifest(root=root, manifest_path=args.manifest, artifact=args.artifact)
            print(
                f"artifact_checksum=VERIFIED artifact={manifest['verified_artifact']} "
                f"size={manifest['verified_size']} sha256={manifest['verified_sha256']}"
            )
        return 0
    except (ArtifactChecksumError, OSError) as exc:
        print(f"artifact_checksum=FAILED detail={exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
