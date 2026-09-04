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
