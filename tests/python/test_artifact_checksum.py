from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.artifact_checksum.artifact_checksum import (
    ArtifactChecksumError,
    build_manifest,
    verify_manifest,
)


def test_artifact_checksum_round_trip(tmp_path: Path) -> None:
    artifact = tmp_path / "DHRT100_FACTORY.uf2"
    artifact.write_bytes(b"factory-artifact")
    manifest_path = tmp_path / "checksum.json"
    manifest = build_manifest(
        root=tmp_path,
        artifact=artifact,
        label="v1-factory",
        source_commit="315dc6f",
    )
    manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
    verified = verify_manifest(root=tmp_path, manifest_path=manifest_path)
    assert verified["verified_sha256"] == manifest["sha256"]
    assert verified["verified_size"] == len(b"factory-artifact")


def test_artifact_checksum_rejects_drift(tmp_path: Path) -> None:
    artifact = tmp_path / "artifact.uf2"
    artifact.write_bytes(b"before")
    manifest = build_manifest(root=tmp_path, artifact=artifact, label="test", source_commit="abc")
    manifest_path = tmp_path / "checksum.json"
    manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
    artifact.write_bytes(b"after")
    with pytest.raises(ArtifactChecksumError, match="drifted"):
        verify_manifest(root=tmp_path, manifest_path=manifest_path)
