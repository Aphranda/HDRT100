"""Host-side contracts for the DPLL SD capture orchestration tool."""

from __future__ import annotations

from pathlib import Path

import pytest

from tools.dpll_observation_capture.dpll_observation_capture import (
    parse_save,
    parse_board,
)
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def test_trace_save_response_is_kept_as_composite_tuple() -> None:
    response = '"QUEUED",17,"/traces/run/no5_20260829.bin",1435'
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:SAVE", response
    )
    assert parse_save(response) == (17, "/traces/run/no5_20260829.bin", 1435)


def test_trace_arm_and_stop_keep_ok_tuple() -> None:
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:ARM", '"OK",0,8000'
    )
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:STOP", '"OK",8000,0'
    )


def test_trace_save_response_accepts_abbreviated_header() -> None:
    assert scpi_response_matches_command(
        "SYST:SYNC:VDC:DPLL:TRACE:SAVE",
        "QUEUED,3,\"/traces/run/no1.bin\",0",
    )


def test_trace_save_response_rejects_non_capture_tuple() -> None:
    assert not scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:SAVE", '"OK",3,"/traces/run/no1.bin"'
    )
    with pytest.raises(ValueError):
        parse_save('"OK",3,"/traces/run/no1.bin"')


def test_board_parser_keeps_physical_no_identity() -> None:
    assert parse_board(" no5 = COM25 ").name == "NO5"
    with pytest.raises(ValueError):
        parse_board("slot5=COM25")


def test_capture_tool_is_explicitly_off_realtime_path() -> None:
    source = Path(
        "tools/dpll_observation_capture/dpll_observation_capture.py"
    ).read_text(encoding="utf-8")
    assert "realtime_path_untouched" in source
    assert "StorageAO" in source
