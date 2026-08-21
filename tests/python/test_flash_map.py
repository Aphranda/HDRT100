import copy
import json
from pathlib import Path

import pytest

from tools.flash_map.flash_map import (
    FlashMapError,
    load_and_validate,
    render_header,
    validate_map,
)


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "config" / "flash_map_v2.json"


def load_source():
    return json.loads(SOURCE.read_text(encoding="utf-8"))


def test_flash_map_v2_golden_source_is_complete():
    data = load_and_validate(SOURCE)
    assert data["map_version"] == 2
    assert data["deployment_state"] == "target_not_deployed"
    assert data["geometry"]["total_size"] == 16 * 1024 * 1024
    assert data["partitions"][-1]["offset"] + data["partitions"][-1]["size"] == data["geometry"]["total_size"]


def test_flash_map_rejects_overlap():
    source = load_source()
    source["partitions"][1]["offset"] = "0x030000"
    with pytest.raises(FlashMapError, match="overlaps"):
        validate_map(source)


def test_flash_map_rejects_partition_overflow():
    source = load_source()
    source["partitions"][-1]["size"] = "0xFFFFF000"
    with pytest.raises(FlashMapError, match="exceeds geometry"):
        validate_map(source)


def test_flash_map_rejects_wrong_geometry():
    source = load_source()
    source["geometry"]["total_size"] = "0x00800000"
    with pytest.raises(FlashMapError, match="exceeds geometry"):
        validate_map(source)


def test_flash_map_rejects_bad_executable_permission():
    source = load_source()
    product_nvs = next(item for item in source["partitions"] if item["id"] == "PRODUCT_NVS")
    product_nvs["permissions"]["app"].append("execute")
    with pytest.raises(FlashMapError, match="executable flag disagrees"):
        validate_map(source)


def test_flash_map_rejects_unequal_app_slots():
    source = load_source()
    app_a = next(item for item in source["partitions"] if item["id"] == "APP_A")
    app_b = next(item for item in source["partitions"] if item["id"] == "APP_B")
    app_a["size"] = "0x1FF000"
    app_b["offset"] = "0x27F000"
    app_b["size"] = "0x201000"
    with pytest.raises(FlashMapError, match="APP_A and APP_B"):
        validate_map(source)


def test_flash_map_validation_does_not_mutate_source():
    source = load_source()
    before = copy.deepcopy(source)
    validate_map(source)
    assert source == before


def test_generated_header_exposes_single_partition_table():
    data = load_and_validate(SOURCE)
    header = render_header(data, Path("config/flash_map_v2.json"))
    assert '#define FLASH_MAP_DEPLOYMENT_STATE "target_not_deployed"' in header
    assert "#define FLASH_MAP_PARTITION_TABLE(X)" in header
    for partition in data["partitions"]:
        prefix = f"FLASH_MAP_{partition['id']}"
        assert f"#define {prefix}_EXECUTABLE" in header
        assert f"X({partition['id']}, {prefix}_ID" in header
