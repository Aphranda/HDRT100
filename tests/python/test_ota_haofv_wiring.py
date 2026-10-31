import re
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


def _function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    end = text.index("\n}", start)
    return text[start:end]


def test_program_staging_buffer_is_not_block_sized_on_the_stack() -> None:
    """OTA-HAOFV-011: the program path must not keep a block-sized stack frame.

    The staging window is one flash page; whole pages program straight from the
    caller block.  A block-sized static workspace is not an option on this
    target (RAM is at its ceiling and heap reduction is evidence-gated).
    """
    core = read("third_party/portable_ota/src/pota_core.c")
    body = _function_body(core, "static pota_error_t pota_program_target(")
    assert "program_buffer[POTA_MAX_DATA_BLOCK_SIZE" not in body
    assert "uint8_t page_workspace[POTA_MAX_FLASH_PAGE_SIZE]" in body
    assert "pota_core_set_program_workspace" in core


def test_tdma_service_keeps_the_ring_alive_only_for_stream_sessions() -> None:
    """OTA-TDMA-002: static maintenance mode must not blanket-skip the TDMA service.

    A stream session is the static maintenance transport, so the ring has to
    keep running; legacy raw-data sessions keep the historical skip so the
    flash lockout poll stays tight.  core1 must never touch the flash driver.
    """
    body = _function_body(
        read("components/vdc_dpll_manager/src/vdc_dpll_manager.c"),
        "void tdma_component_core1_service(void)",
    )
    assert "ota_ao_is_active() && !ota_ao_tdma_maintenance_allowed()" in body
    assert "drv_flash_" not in body
    ao = read("components/ota_manager/src/ota_ao.c")
    assert "portable_ota_port_stream_is_active()" in ao
    assert "ota_ao_tdma_maintenance_allowed" in ao


def test_ota_task_stack_is_sized_from_the_measured_watermark() -> None:
    """OTA-HAOFV-011: the stack must stay above the measured 1428-word peak.

    A 1152-word attempt overflowed during BEGIN/erase; the value here is the
    measured high-water plus margin, not a guess from the removed block frame.
    """
    text = read("application/src/app_tasks.c")
    assert "OTA_EVENT_MAX_DATA_SIZE - OTA_EVENT_INLINE_DATA_SIZE" not in text
    match = re.search(r"#define APP_OTA_TASK_STACK_WORDS (\d+)u", text)
    assert match, "OTA task stack must be a measured constant"
    stack_words = int(match.group(1))
    assert stack_words >= 1700, "keep at least ~20% margin over 1428 words"
    assert "1428" in text, "record the measured watermark next to the constant"


def test_ota_data_consumes_block_before_rejections() -> None:
    """OTA-HAOFV-013: a rejected DATA must still consume its arbitrary block.

    The SCPI payload is part of the message.  Returning before the block is
    parsed leaves the bytes in the input buffer and desynchronizes the parser,
    so the next query reads payload as command text.
    """
    body = _function_body(
        read("middleware/scpi_port/src/scpi_ota_commands.c"),
        "scpi_result_t scpi_cmd_ota_data(scpi_t *context)",
    )
    parse = body.index("SCPI_ParamArbitraryBlock")
    run_gate = body.index("scpi_port_reject_if_run_forbidden")
    stream_gate = body.index("portable_ota_port_stream_is_active")
    assert parse < run_gate, "DATA must consume the block before the RUN gate"
    assert parse < stream_gate, "DATA must consume the block before the stream gate"
    # Both rejections stay in place; only their order relative to parsing moved.
    assert "return SCPI_RES_ERR;" in body
