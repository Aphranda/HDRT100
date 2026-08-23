from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


FAULTS = (
    "SLOT_EMPTY",
    "SLOT_RANGE_INVALID",
    "VECTOR_INVALID",
    "IMAGE_CRC_INVALID",
    "IMAGE_HASH_INVALID",
    "SIGNATURE_INVALID",
    "COMPATIBILITY_INVALID",
    "RECOVERY_UNAVAILABLE",
)


def test_direct_ab_fault_matrix_has_boot_reason_projection_for_every_stage() -> None:
    metadata = (ROOT / "components/ota_manager/inc/ota_metadata.h").read_text(
        encoding="utf-8"
    )
    strings = (ROOT / "third_party/portable_ota/src/pota_strings.c").read_text(
        encoding="utf-8"
    )
    boot = (ROOT / "bootloader/src/bootloader_main.c").read_text(encoding="utf-8")
    recovery = (ROOT / "bootloader/src/recovery_main.c").read_text(encoding="utf-8")

    for fault in FAULTS:
        assert f"OTA_BOOT_RESULT_{fault}" in metadata
        assert f'"{fault}"' in strings
    assert "bootloader_manifest_error_to_result" in boot
    assert "IMAGE_HASH_INVALID" in boot
    assert "RECOVERY_UNAVAILABLE" in boot
    assert "SYST:RECOVERY:AB:STATUS?" in recovery
    assert "pota_package_parse_header" in recovery
    assert "portable_ota_crypto_verify_manifest" in recovery
    assert "portable_ota_crypto_sha256_flash" in recovery


def test_recovery_ab_status_is_read_only_and_reports_both_slots() -> None:
    recovery = (ROOT / "bootloader/src/recovery_main.c").read_text(encoding="utf-8")
    assert "SYST:RECOVERY:AB:STATUS?" in recovery
    assert "recovery_get_bcb_metadata" in recovery
    assert "recovery_validate_slot(\n        &metadata, OTA_SLOT_A" in recovery
    assert "recovery_validate_slot(\n        &metadata, OTA_SLOT_B" in recovery
    assert "boot_flash_service_" not in recovery
    assert "drv_flash_program" not in recovery
