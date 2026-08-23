from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_v2_candidate_requires_direct_ab_default():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    marker = 'elseif(PROJECT_FLASH_DEPLOYMENT_MAP STREQUAL "v2_candidate")'
    block = cmake.split(marker, 1)[1].split("elseif(", 1)[0]

    assert "PROJECT_OTA_DEFAULT_BOOT_MODE STREQUAL \"DIRECT_AB\"" in block
    assert "v2_candidate only supports PROJECT_OTA_DEFAULT_BOOT_MODE=DIRECT_AB" in block


def test_v2_portable_port_does_not_map_to_copy_mode():
    source = (ROOT / "middleware/portable_ota_port/src/portable_ota_core_port.c").read_text(
        encoding="utf-8"
    )
    marker = "static pota_boot_mode_t portable_core_boot_mode_from_metadata"
    block = source.split(marker, 1)[1].split("static pota_slot_t", 1)[0]

    assert "return POTA_BOOT_MODE_DIRECT_AB;" in block
    assert "return POTA_BOOT_MODE_COPY_TO_ACTIVE;" in block
    assert block.index("return POTA_BOOT_MODE_DIRECT_AB;") < block.index(
        "return POTA_BOOT_MODE_COPY_TO_ACTIVE;"
    )


def test_v2_bootloader_copy_path_is_compile_guarded():
    source = (ROOT / "bootloader/src/bootloader_main.c").read_text(encoding="utf-8")

    assert "#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2" in source
    assert "static bool bootloader_apply_pending_image" in source
    assert "metadata.boot_mode != (uint32_t)OTA_BOOT_MODE_DIRECT_AB" in source


def test_v2_fault_injection_does_not_register_writable_mode_command():
    header = (ROOT / "middleware/scpi_port/inc/scpi_ota_commands.h").read_text(
        encoding="utf-8"
    )
    source = (ROOT / "middleware/scpi_port/src/scpi_ota_commands.c").read_text(
        encoding="utf-8"
    )

    assert "#if !defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2" in header
    assert "SYSTem:OTA:MODE\", .callback = scpi_cmd_ota_mode" in header
    assert "PROJECT_ENABLE_OTA_FAULT_INJECTION &&" in source
    assert "!defined(PROJECT_FLASH_DEPLOYMENT_V2) || !PROJECT_FLASH_DEPLOYMENT_V2" in source


def test_v2_diagnostic_projection_does_not_advertise_legacy_mode_target():
    source = (ROOT / "middleware/scpi_port/src/scpi_ota_commands.c").read_text(
        encoding="utf-8"
    )
    assert 'return "LEGACY_UNSUPPORTED";' in source
    assert "return (uint32_t)OTA_SLOT_NONE;" in source
    assert "OTA_BOOT_CAP_DIRECT_AB" in source


def test_v2_mode_history_classifies_legacy_and_unknown_values_explicitly():
    source = (ROOT / "middleware/scpi_port/src/scpi_ota_commands.c").read_text(
        encoding="utf-8"
    )
    mode_block = source.split(
        "static const char *scpi_ota_boot_mode_to_string", 1
    )[1]
    v2_block = mode_block.split("#else", 1)[0]
    assert 'return "LEGACY_UNSUPPORTED";' in v2_block
    assert 'return "UNSUPPORTED";' in v2_block
    assert 'return "COPY_TO_ACTIVE";' not in v2_block
