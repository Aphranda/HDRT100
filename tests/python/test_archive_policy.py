"""Guard the archived V1 build path out of default and normal presets."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_default_cmake_map_is_v2():
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "set(PROJECT_FLASH_DEPLOYMENT_MAP v2_candidate" in source
    assert "PROJECT_V1_RECOVERY_BUILD OFF" in source
    assert "set PROJECT_V1_RECOVERY_BUILD=ON" in source


def test_normal_presets_do_not_select_archived_v1():
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    for preset in presets.get("configurePresets", []):
        cache = preset.get("cacheVariables", {})
        assert cache.get("PROJECT_FLASH_DEPLOYMENT_MAP") != "v1_compat", (
            f"{preset.get('name')} must not enable archived v1_compat")
        assert cache.get("PROJECT_FACTORY_MIGRATION_BUILD") == "ON", (
            f"{preset.get('name')} must build the V2 factory candidate")


def test_v1_recovery_requires_explicit_opt_in():
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    marker = 'if(PROJECT_FLASH_DEPLOYMENT_MAP STREQUAL "v1_compat")'
    block = source.split(marker, 1)[1].split("elseif(", 1)[0]
    assert "PROJECT_V1_RECOVERY_BUILD" in block
    assert "v1_compat is archived" in block
