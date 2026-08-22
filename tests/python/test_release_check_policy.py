import json

from tools.release_check.release_check import check_forbidden_strings, check_preset


def test_release_policy_requires_rtos_and_multicore(tmp_path):
    presets = {
        "configurePresets": [{
            "name": "product",
            "cacheVariables": {
                "PROJECT_ENABLE_OTA_FAULT_INJECTION": "OFF",
                "PROJECT_ENABLE_UART_STDIO": "OFF",
                "PROJECT_OTA_DEFAULT_BOOT_MODE": "DIRECT_AB",
                "PROJECT_FLASH_DEPLOYMENT_MAP": "v1_compat",
                "PROJECT_USE_FREERTOS": "ON",
                "PROJECT_USE_MULTICORE": "ON",
            },
        }],
    }
    (tmp_path / "CMakePresets.json").write_text(
        json.dumps(presets), encoding="utf-8")
    failures = []
    check_preset(tmp_path, "product", failures)
    assert failures == []


def test_release_policy_rejects_single_core(tmp_path):
    presets = {
        "configurePresets": [{
            "name": "product",
            "cacheVariables": {
                "PROJECT_ENABLE_OTA_FAULT_INJECTION": "OFF",
                "PROJECT_ENABLE_UART_STDIO": "OFF",
                "PROJECT_OTA_DEFAULT_BOOT_MODE": "DIRECT_AB",
                "PROJECT_FLASH_DEPLOYMENT_MAP": "v1_compat",
                "PROJECT_USE_FREERTOS": "ON",
                "PROJECT_USE_MULTICORE": "OFF",
            },
        }],
    }
    (tmp_path / "CMakePresets.json").write_text(
        json.dumps(presets), encoding="utf-8")
    failures = []
    check_preset(tmp_path, "product", failures)
    assert failures == ["product must set PROJECT_USE_MULTICORE=ON"]


def test_release_policy_rejects_unselected_flash_map(tmp_path):
    presets = {
        "configurePresets": [{
            "name": "product",
            "cacheVariables": {
                "PROJECT_ENABLE_OTA_FAULT_INJECTION": "OFF",
                "PROJECT_ENABLE_UART_STDIO": "OFF",
                "PROJECT_OTA_DEFAULT_BOOT_MODE": "DIRECT_AB",
                "PROJECT_USE_FREERTOS": "ON",
                "PROJECT_USE_MULTICORE": "ON",
            },
        }],
    }
    (tmp_path / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
    failures = []
    check_preset(tmp_path, "product", failures)
    assert failures == ["product must set PROJECT_FLASH_DEPLOYMENT_MAP=v1_compat"]


def test_release_artifacts_reject_validation_bootsel_command(tmp_path):
    build = tmp_path / "build"
    build.mkdir()
    (build / "DHRT100.bin").write_bytes(b"release")
    (build / "DHRT100.elf").write_bytes(b"SYSTem:BOOT:BOOTSel")
    (build / "DHRT100_BOOT.elf").write_bytes(b"boot")
    failures = []

    check_forbidden_strings(tmp_path, build, failures)

    assert len(failures) == 1
    assert "forbidden validation command string" in failures[0]
