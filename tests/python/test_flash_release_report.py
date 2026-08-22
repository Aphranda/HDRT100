from __future__ import annotations

import json
from pathlib import Path

from tools.flash_map import flash_release_report


def make_artifacts(build: Path) -> None:
    build.mkdir(parents=True)
    for _, map_name, dis_name, _ in flash_release_report.ARTIFACTS:
        (build / map_name).write_text("map", encoding="utf-8")
        (build / dis_name).write_text("dis", encoding="utf-8")


def test_collect_report_records_all_owner_profiles(tmp_path: Path, monkeypatch) -> None:
    build = tmp_path / "build"
    make_artifacts(build)
    calls: list[str] = []

    def fake_validate(map_text: str, dis_text: str, profile: str = "app") -> list[str]:
        assert map_text == "map"
        assert dis_text == "dis"
        calls.append(profile)
        return []

    monkeypatch.setattr(flash_release_report, "validate_link_contract", fake_validate)
    report = flash_release_report.collect_report(tmp_path, build)

    assert report["schema"] == 1
    assert report["product"] == "DHRT100"
    assert report["ok"] is True
    assert calls == ["app", "app", "boot"]
    assert all(entry["map_sha256"] for entry in report["entries"])


def test_collect_report_fails_closed_on_missing_artifact(tmp_path: Path) -> None:
    report = flash_release_report.collect_report(tmp_path, tmp_path / "missing")

    assert report["ok"] is False
    assert all(entry["failures"] for entry in report["entries"])
    json.dumps(report)
