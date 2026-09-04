from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_logic_analyzer_contract_is_read_only_and_traceable() -> None:
    header = (
        ROOT / "components/sync_io/inc/sync_io_logic_analyzer.h"
    ).read_text(encoding="utf-8")

    for token in (
        "SYNC_IO_LOGIC_ANALYZER_MODE_RAW_SAMPLE",
        "SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP",
        "SYNC_IO_LOGIC_ANALYZER_MODE_TRIGGERED_CAPTURE",
        "SYNC_IO_LOGIC_ANALYZER_END_TIMEOUT",
        "SYNC_IO_LOGIC_ANALYZER_END_OVERFLOW",
        "SYNC_IO_LOGIC_ANALYZER_END_DMA_FAULT",
        "expected_profile_generation",
        "expected_persona_generation",
        "associated_tdma_cycle",
        "associated_tdma_persona_generation",
        "sequence_lock",
        "data_crc32",
        "SYNC_IO_LOGIC_ANALYZER_GATE_DMA_BOUNDS",
        "SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_MEMORY",
        "SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_FLASH",
        "SYNC_IO_LOGIC_ANALYZER_GATE_UNCONTROLLED_GPIO",
        "SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_CONTINUE",
        "SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_ROUND_END",
        "SYNC_IO_LOGIC_ANALYZER_DEBUG_CONTINUE_LIMIT",
        "last_gate_state",
        "last_gate_raw_value0",
        "resource_conflict_mask",
    ):
        assert token in header

    config = header.split("typedef struct {", 2)[2].split(
        "} sync_io_logic_analyzer_config_t;", 1
    )[0]
    for forbidden in (
        "gpio_write_mask",
        "output_mask",
        "drive_strength",
        "slew_rate",
    ):
        assert forbidden not in config
    assert "debug_continue_budget" in config


def test_logic_analyzer_validator_limits_sources_to_pad_masks() -> None:
    source = (
        ROOT / "components/sync_io/src/sync_io_logic_analyzer.c"
    ).read_text(encoding="utf-8")
    assert "SYNC_IO_PERSONA_ID_LOGIC_ANALYZER" in source
    assert "SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD" in source
    assert "descriptor->gpio_write_mask == 0u" in source
    assert "source_mask & ~descriptor->gpio_read_mask" in source
    assert "config->max_records > SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS" in source


def test_logic_analyzer_pio_program_is_full_bus_read_only() -> None:
    pio = (ROOT / "components/sync_io/src/sync_io.pio").read_text(
        encoding="utf-8"
    )
    program = pio.split(".program logic_analyzer_raw_sample", 1)[1].split(
        ".program", 1
    )[0]
    assert "in pins, 32" in program
    assert "set pins" not in program
    assert "out pins" not in program
    assert "logic_analyzer_raw_sample_program_init" in pio


def test_logic_analyzer_hardware_lifecycle_uses_persona_manager() -> None:
    source = (
        ROOT / "components/sync_io/src/sync_io_logic_analyzer.c"
    ).read_text(encoding="utf-8")
    header = (
        ROOT / "components/sync_io/inc/sync_io_logic_analyzer.h"
    ).read_text(encoding="utf-8")
    for token in (
        "sync_io_logic_analyzer_persona_begin",
        "sync_io_logic_analyzer_persona_end",
        "sync_io_persona_manager_claim",
        "sync_io_persona_manager_load",
        "sync_io_persona_manager_arm",
        "sync_io_persona_manager_start",
        "sync_io_persona_manager_release",
        "sync_io_logic_analyzer_persona_get_snapshot",
    ):
        assert token in source or token in header
    assert "SYSTem:TDMA" not in source
    assert "storage_manager" not in source


def test_analyzer_status_query_is_read_only_and_registered() -> None:
    header = (
        ROOT / "middleware/scpi_port/inc/scpi_realtime_io_commands.h"
    ).read_text(encoding="utf-8")
    source = (
        ROOT / "middleware/scpi_port/src/scpi_realtime_io_commands.c"
    ).read_text(encoding="utf-8")
    assert "REALtime:IO:ANALyzer:STATe?" in header
    assert "scpi_cmd_analyzer_state_q" in header
    assert "sync_io_logic_analyzer_get_status" in source
    body = source.split("scpi_result_t scpi_cmd_analyzer_state_q", 1)[1].split(
        "scpi_result_t scpi_cmd_clock_freq", 1
    )[0]
    assert "sync_io_logic_analyzer_hw_start" not in body
    assert "sync_io_logic_analyzer_hw_stop" not in body


def test_analyzer_control_is_core1_mailbox_and_scpi_intent_only() -> None:
    header = (
        ROOT / "components/sync_io/inc/sync_io_logic_analyzer.h"
    ).read_text(encoding="utf-8")
    source = (
        ROOT / "components/sync_io/src/sync_io_logic_analyzer.c"
    ).read_text(encoding="utf-8")
    scpi_header = (
        ROOT / "middleware/scpi_port/inc/scpi_realtime_io_commands.h"
    ).read_text(encoding="utf-8")
    scpi_source = (
        ROOT / "middleware/scpi_port/src/scpi_realtime_io_commands.c"
    ).read_text(encoding="utf-8")
    app_source = (
        ROOT / "application/src/app.c"
    ).read_text(encoding="utf-8")
    for token in (
        "sync_io_logic_analyzer_request_arm",
        "sync_io_logic_analyzer_request_stop",
        "sync_io_logic_analyzer_service_core1",
        "SYNC_IO_LOGIC_ANALYZER_COMMAND_RESULT_BUSY",
        "request_sequence",
        "handled_sequence",
    ):
        assert token in header or token in source
    assert "REALtime:IO:ANALyzer:ARM" in scpi_header
    assert "REALtime:IO:ANALyzer:EDGE:ARM" in scpi_header
    assert "REALtime:IO:ANALyzer:STOP" in scpi_header
    assert "sync_io_logic_analyzer_request_arm" in scpi_source
    assert "sync_io_logic_analyzer_request_stop" in scpi_source
    assert "SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP" in scpi_source
    assert "sync_io_logic_analyzer_service_core1(8u)" in app_source
    tdma_phase = app_source.split(
        "static void app_realtime_tdma_phase", 1
    )[1].split("static void app_realtime_vdc_phase", 1)[0]
    assert tdma_phase.index("tdma_component_core1_service") < tdma_phase.index(
        "sync_io_logic_analyzer_service_core1"
    )
    arm_body = scpi_source.split(
        "scpi_result_t scpi_cmd_analyzer_arm", 1
    )[1].split("scpi_result_t scpi_cmd_analyzer_stop", 1)[0]
    assert "sync_io_logic_analyzer_hw_start" not in arm_body
    assert "sync_io_logic_analyzer_hw_stop" not in arm_body


def test_analyzer_storage_drain_stays_on_core0_after_stop() -> None:
    source = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    assert "sync_io_logic_analyzer_drain_core0" in source
    assert "storage_manager_begin_evidence_write" in source
    assert "app_diag_service" in source


def test_analyzer_stop_publishes_shadow_and_blocks_reentry_until_drain() -> None:
    source = (ROOT / "components/sync_io/src/sync_io_logic_analyzer.c").read_text(
        encoding="utf-8")
    assert "shadow_capture" in source
    assert "shadow_ready" in source
    assert "sync_io_logic_analyzer_publish_shadow" in source
    assert "shadow_ready, __ATOMIC_ACQUIRE" in source
    assert "shadow_ready, 0u, __ATOMIC_RELEASE" in source


def test_edge_timestamp_backend_emits_only_level_changes() -> None:
    source = Path("components/sync_io/src/sync_io_logic_analyzer.c").read_text(
        encoding="utf-8")
    assert "SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP" in source
    assert "previous_level_valid" in source
    assert "const uint32_t edge = s_hw.previous_level ^ level" in source
    assert "if (edge == 0u)" in source
    assert "edge_mask = edge" in source


def test_edge_hil_arms_explicit_edge_command_and_checks_mode() -> None:
    source = Path("tools/analyzer_tdma_hil/analyzer_tdma_hil.py").read_text(
        encoding="utf-8")
    assert "REALtime:IO:ANALyzer:EDGE:ARM" in source
    assert '"edge_mode_active"' in source
