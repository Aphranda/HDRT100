from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_ota_queries_use_snapshot_boundary() -> None:
    text = read("middleware/scpi_port/src/scpi_ota_commands.c")
    assert "ota_ao_get_metadata(" not in text
    assert "ota_ao_get_metadata_snapshot(&metadata)" in text
    assert "ota_ao_get_vector(&vector)" in text


def test_mark_pending_mainline_uses_incremental_scan_and_async_flash() -> None:
    text = read("components/ota_manager/src/ota_metadata.c")
    assert "pota_bcb_scan_begin" in text
    assert "pota_bcb_scan_step" in text
    assert "pota_bcb_txn_begin_from_selection" in text
    assert "ota_metadata_flash_program_step" in text


def test_app_bcb_platform_exposes_step_callbacks() -> None:
    text = read("components/ota_manager/src/ota_metadata.c")
    assert ".program_page_step = ota_metadata_bcb_program_page_step" in text
    assert ".erase_lane_sector_step = ota_metadata_bcb_erase_sector_step" in text


def test_temporary_fault_trace_hooks_are_removed() -> None:
    sources = "\n".join(
        read(path)
        for path in (
            "components/ota_manager/src/ota_metadata.c",
            "third_party/portable_ota/src/pota_boot_control_store.c",
            "components/diagnostics/src/diagnostics.c",
        )
    )
    for symbol in (
        "diagnostics_fault_capture",
        "pota_bcb_trace_fn",
        "POTA_BCB_TRACE_BODY_VALIDATED",
        "OTA_TRACE_PHASE_HARDFAULT",
    ):
        assert symbol not in sources
