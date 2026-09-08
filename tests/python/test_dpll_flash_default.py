from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _function(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


def test_product_config_seeds_conservative_profile_without_startup_write() -> None:
    header = (ROOT / "components" / "product_config" / "inc" /
              "product_config.h").read_text(encoding="utf-8")
    source = (ROOT / "components" / "product_config" / "src" /
              "product_config.c").read_text(encoding="utf-8")

    assert "PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16              16384" in header
    assert "PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16                256" in header
    assert "PRODUCT_CONFIG_DPLL_DEFAULT_SANITY_FREQ_LIMIT_PPB 10000u" in header
    init = _function(source, "bool product_config_init(void)",
                     "bool product_config_get_usb_mode")
    assert "product_config_dpll_profile_is_valid" in init
    assert "product_config_record_set_dpll_profile(&s_product_config" in init
    assert "product_config_set_dpll_servo_profile" not in init
    assert "product_config_store" not in init
    assert "FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE" in source


def test_dpll_starts_and_restores_only_flash_profile() -> None:
    source = (ROOT / "components" / "vdc_dpll_manager" / "src" /
              "vdc_dpll_manager.c").read_text(encoding="utf-8")

    init = _function(source, "bool vdc_dpll_manager_init(void)",
                     "void vdc_dpll_manager_set_vdc_ready")
    assert "product_config_get_dpll_servo_profile(&persisted_profile)" in init
    assert "if (!product_config_get_dpll_servo_profile(&persisted_profile))" in init
    assert "return false;" in init

    default = _function(
        source, "bool vdc_dpll_manager_request_default_debug_servo_tune",
        "bool vdc_dpll_manager_store_debug_servo_profile")
    assert "product_config_get_dpll_servo_profile(&persisted_profile)" in default
    assert "profile.kp_q16 = persisted_profile.kp_q16;" in default

    store = _function(source, "bool vdc_dpll_manager_store_debug_servo_profile",
                      "void vdc_dpll_manager_get_debug_servo_tune_status")
    assert "vdc_dpll_manager_get_debug_servo_tune_status(&status);" in store
    assert "product_config_set_dpll_servo_profile(&profile)" in store


def test_store_scpi_command_returns_ok_after_persisting_staged_profile() -> None:
    header = (ROOT / "middleware" / "scpi_port" / "inc" /
              "scpi_sync_commands.h").read_text(encoding="utf-8")
    source = (ROOT / "middleware" / "scpi_port" / "src" /
              "scpi_sync_commands.c").read_text(encoding="utf-8")

    assert '"SYSTem:SYNC:VDC:DPLL:STORe"' in header
    store = _function(source, "scpi_result_t scpi_cmd_sync_vdc_dpll_store",
                      "scpi_result_t scpi_cmd_sync_vdc_dpll_filter_q")
    assert "vdc_dpll_manager_store_debug_servo_profile()" in store
    assert 'SCPI_ResultText(context, "OK")' in store
