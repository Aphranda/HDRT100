from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_no5_ring_entry_points_are_fail_closed() -> None:
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(
        encoding="utf-8"
    )
    guard = "board_identity_get_no() == 5u"
    assert source.count(guard) >= 4
    assert "DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED" in source
    assert "board_identity_get_no() != 5u" in source


def test_no5_core1_path_skips_tdma_owner_but_keeps_observer_phase() -> None:
    source = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    marker = "static void app_realtime_tdma_phase(void)"
    start = source.index(marker)
    end = source.index("static void app_realtime_vdc_phase", start)
    phase = source[start:end]
    assert "board_identity_get_no() != 5u" in phase
    assert "tdma_component_core1_service();" in phase
    assert "sync_io_logic_analyzer_service_core1(8u);" in phase


def test_tdma_observer_has_one_owner_phase_call() -> None:
    source = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    tdma = source.split("static void app_realtime_tdma_phase(void)", 1)[1].split(
        "static void app_realtime_vdc_phase", 1)[0]
    vdc = source.split("static void app_realtime_vdc_phase(void)", 1)[1].split(
        "static void app_realtime_dpll_phase", 1)[0]
    dpll = source.split("static void app_realtime_dpll_phase(void)", 1)[1].split(
        "static void app_realtime_calibration_phase", 1)[0]
    assert tdma.count("tdma_runtime_owner_service_observer();") == 1
    assert vdc.count("tdma_runtime_owner_service_observer();") == 0
    assert dpll.count("tdma_runtime_owner_service_observer();") == 0
    assert tdma.index("tdma_component_core1_service();") < tdma.index(
        "tdma_runtime_owner_service_observer();"
    )


def test_t2_runtime_is_mounted_only_in_sync_trigger_phase() -> None:
    source = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    trigger = source.split("static void app_realtime_sync_trigger_phase(void)", 1)[1].split(
        "static void app_realtime_trigger_measure_phase", 1
    )[0]
    assert trigger.count("sync_trigger_service();") == 1
    for marker, end in (
        ("static void app_realtime_tdma_phase(void)", "static void app_realtime_vdc_phase"),
        ("static void app_realtime_vdc_phase(void)", "static void app_realtime_dpll_phase"),
        ("static void app_realtime_dpll_phase(void)", "static void app_realtime_calibration_phase"),
    ):
        phase = source.split(marker, 1)[1].split(end, 1)[0]
        assert "trigger_t2_runtime" not in phase
        assert "sync_trigger_service();" not in phase


def test_ota_4096_contract_remains_unchanged() -> None:
    for relative in (
        "middleware/scpi_port/src/scpi_port.c",
        "middleware/usbtmc_scpi_port/src/usbtmc_scpi_port.c",
        "components/ota_manager/inc/ota_event.h",
    ):
        source = (ROOT / relative).read_text(encoding="utf-8")
        assert "PROJECT_OTA_MAX_DATA_BLOCK_SIZE" in source
        assert "4096u" in source
