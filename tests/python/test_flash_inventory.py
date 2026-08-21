import json
from pathlib import Path

import pytest

from tools.flash_map.flash_inventory import InventoryError, load_inventory, validate_inventory


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "config" / "flash_raw_call_allowlist.json"


def base_inventory():
    return {
        "inventory_version": 1,
        "layout": {
            "active_map_version": 1,
            "compat_layout_size": "0x00400000",
            "physical_geometry_size": "0x01000000",
            "upper_unallocated_size": "0x00C00000",
            "migration_state": "v2_target_not_deployed",
        },
        "raw_callers": [],
        "legacy_address_dependencies": [],
    }


def test_repository_raw_flash_inventory_matches_source():
    report = validate_inventory(ROOT, load_inventory(INVENTORY))
    assert report["layout"]["active_map_version"] == 1
    assert report["layout"]["migration_state"] == "v2_target_not_deployed"
    assert len(report["raw_callers"]) == 6


def test_inventory_rejects_unregistered_raw_caller(tmp_path):
    source = tmp_path / "application" / "new_writer.c"
    source.parent.mkdir(parents=True)
    source.write_text("void f(void) { drv_flash_erase(0, 4096); }\n", encoding="utf-8")
    with pytest.raises(InventoryError, match="unexpected=.*new_writer"):
        validate_inventory(tmp_path, base_inventory())


def test_inventory_rejects_operation_drift(tmp_path):
    source = tmp_path / "application" / "writer.c"
    source.parent.mkdir(parents=True)
    source.write_text("void f(void) { drv_flash_program(0, 0, 0); }\n", encoding="utf-8")
    inventory = base_inventory()
    inventory["raw_callers"] = [{
        "file": "application/writer.c",
        "owner": "FlashTransactionAO",
        "contexts": ["app"],
        "core": "core0",
        "modes": ["idle"],
        "partitions": ["scratch"],
        "operations": ["erase"],
        "write_frequency": "validation_only",
        "power_cut_semantics": "restore_after_test",
        "target_api": "FlashTransactionAO",
    }]
    with pytest.raises(InventoryError, match="operation drift"):
        validate_inventory(tmp_path, inventory)


def test_inventory_rejects_app_raw_write_outside_transaction_owner(tmp_path):
    source = tmp_path / "application" / "writer.c"
    source.parent.mkdir(parents=True)
    source.write_text("void f(void) { drv_flash_program(0, 0, 256); }\n", encoding="utf-8")
    inventory = base_inventory()
    inventory["raw_callers"] = [{
        "file": "application/writer.c",
        "owner": "ProductConfigAO",
        "contexts": ["app"],
        "core": "core0",
        "modes": ["idle"],
        "partitions": ["product_nvs"],
        "operations": ["program"],
        "write_frequency": "operator_configuration_change",
        "power_cut_semantics": "rewrite",
        "target_api": "FlashNVS",
    }]
    with pytest.raises(InventoryError, match="App raw write"):
        validate_inventory(tmp_path, inventory)


def test_inventory_file_is_valid_json():
    data = json.loads(INVENTORY.read_text(encoding="utf-8"))
    assert data["inventory_version"] == 1
