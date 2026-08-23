from __future__ import annotations

from pathlib import Path

import pytest

from tools.flash_scratch_validate.flash_scratch_validate import (
    CONFIRM_TOKEN,
    parse_flash_map,
    parse_jedec,
    parse_validation,
    v2_scratch_contract,
)


def test_parse_validation_requires_all_evidence_fields():
    result = parse_validation("1,1,0x12345678,0x12345678,1,1,1")
    assert result["expected_hash"] == 0x12345678
    assert result["readback_hash"] == 0x12345678
    assert result["hash_match"] == 1
    assert result["erased_ok"] == 1


def test_parse_validation_rejects_truncated_response():
    with pytest.raises(ValueError, match="fields"):
        parse_validation("1,1,1")


def test_confirmation_token_is_explicit_and_stable():
    assert CONFIRM_TOKEN == int("53435254", 16)


def test_parse_jedec_exposes_bottom_driver_source_and_capacity():
    result = parse_jedec("1,\"JEDEC_RDID_9F\",15679512,239,64,24,16777216,16777216,1")
    assert result["source"] == "JEDEC_RDID_9F"
    assert result["raw_id"] == 0xEF4018
    assert result["capacity_bytes"] == 16 * 1024 * 1024
    assert result["capacity_matches_geometry"] == 1


def test_parse_flash_map_and_generated_v2_scratch_contract_match():
    contract = v2_scratch_contract()
    result = parse_flash_map(
        '2,"target_not_deployed",14,12,11796480,1048576,4096,3,3,3,0'
    )
    assert result["map_version"] == contract["map_version"]
    assert result["partition_id"] == contract["partition_id"]
    assert result["offset"] == contract["offset"]
    assert result["size"] == contract["size"]


def test_jedec_scpi_is_sourced_from_bottom_flash_driver():
    root = Path(__file__).resolve().parents[2]
    driver = (root / "drivers/mcu/flash/src/drv_flash.c").read_text(encoding="utf-8")
    scpi = (
        root / "middleware/scpi_port/src/scpi_system_diagnostics_commands.c"
    ).read_text(encoding="utf-8")
    commands = (
        root / "middleware/scpi_port/inc/scpi_system_diagnostics_commands.h"
    ).read_text(encoding="utf-8")
    assert "flash_do_cmd(tx, rx, sizeof(tx))" in driver
    assert "drv_flash_read_jedec_id(&jedec)" in scpi
    assert "SYSTem:DIAGnostic:FLASh:JEDEC?" in commands


def test_validation_scratch_does_not_replay_production_durable_completion():
    root = Path(__file__).resolve().parents[2]
    owner = (
        root / "components/flash_transaction/src/flash_transaction_ao.c"
    ).read_text(encoding="utf-8")
    assert "effective.requester != FLASH_TRANSACTION_REQUESTER_VALIDATION" in owner
