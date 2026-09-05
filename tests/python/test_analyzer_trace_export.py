from __future__ import annotations

from collections import deque
from argparse import Namespace

import pytest

import tools.analyzer_trace_export.analyzer_trace_export as exporter
from tools.analyzer_trace_export.analyzer_trace_export import (
    discover_analyzer_segments,
    download_file,
    parse_board,
    parse_catalog_page,
    parse_runtime_status,
    runtime_stopped,
)
from tools.scpi_common.scpi_serial import read_scpi_response


def test_parse_board_and_runtime_stopped() -> None:
    assert parse_board("no1=com5").name == "NO1"
    values = [0] * 40
    values[38] = 7
    values[39] = 7
    status = parse_runtime_status(",".join(str(value) for value in values))
    assert runtime_stopped(status) is True
    values[4] = 1
    assert runtime_stopped(parse_runtime_status(
        ",".join(str(value) for value in values))) is False


def test_catalog_pagination_selects_analyzer_files() -> None:
    responses = deque([
        '"OK","/traces/run",0,2,2,0,0,'
        '"fault.bin,9,FILE;analyzer_00000002.bin,56,FILE;"',
        '"OK","/traces/run",2,2,0,1,0,'
        '"analyzer_00000010.bin,88,FILE;notes,0,DIR;"',
    ])

    segments, pages = discover_analyzer_segments(
        lambda _: responses.popleft(), page_limit=2)
    assert len(pages) == 2
    assert [row["session_from_name"] for row in segments] == [2, 10]
    assert segments[-1]["remote_path"] == "/traces/run/analyzer_00000010.bin"


def test_catalog_truncation_retries_with_smaller_page() -> None:
    commands: list[str] = []
    responses = deque([
        '"OK","/traces/run",0,2,2,0,1,'
        '"analyzer_00000001.bin,56,FILE;analyzer_00000002.bin,56,FILE"',
        '"OK","/traces/run",0,2,0,1,0,'
        '"analyzer_00000001.bin,56,FILE;analyzer_00000002.bin,56,FILE;"',
    ])

    def query(command: str) -> str:
        commands.append(command)
        return responses.popleft()

    segments, pages = discover_analyzer_segments(query, page_limit=4)
    assert len(segments) == 2
    assert pages[0]["retry_with_page_limit"] == 2
    assert commands[0].endswith(",0,4")
    assert commands[1].endswith(",0,2")


def test_catalog_preserves_directory_order_across_uptime_reset() -> None:
    response = (
        '"OK","/traces/run",0,2,0,1,0,'
        '"analyzer_00001000.bin,56,FILE;analyzer_00000002.bin,56,FILE;"')
    segments, _ = discover_analyzer_segments(lambda _: response, page_limit=2)
    assert [row["session_from_name"] for row in segments] == [1000, 2]


def test_catalog_retries_transient_wrong_path_response() -> None:
    responses = deque([
        '"OK","/",0,0,0,1,0,"EMPTY"',
        '"OK","/traces/run",0,1,0,1,0,'
        '"analyzer_00000003.bin,56,FILE;"',
    ])
    commands: list[str] = []

    def query(command: str) -> str:
        commands.append(command)
        return responses.popleft()

    segments, pages = discover_analyzer_segments(query, page_limit=4)
    assert len(segments) == 1
    assert len(pages[0]["raw_attempts"]) == 2
    assert all(command.startswith("MMEM:CAT:PAGE?") for command in commands)


def test_catalog_rejects_nonprogress_page() -> None:
    response = ('"OK","/traces/run",4,1,4,0,0,'
                '"analyzer_00000001.bin,56,FILE;"')
    with pytest.raises(ValueError, match="no progress"):
        parse_catalog_page(response, "/traces/run", 4)


def test_download_file_checks_size_hash_and_eof() -> None:
    payload = b"abcdefgh"
    responses = deque([
        f'"OK",1,0,4,4,8,0,123,0,"{payload[:4].hex()}"',
        f'"OK",2,4,4,4,8,1,123,0,"{payload[4:].hex()}"',
    ])
    data, pages = download_file(
        lambda _: responses.popleft(), "/traces/run/analyzer_1.bin",
        chunk_size=4, expected_size=8)
    assert data == payload
    assert len(pages) == 2
    assert pages[-1]["eof"] is True


def test_download_file_rejects_catalog_size_mismatch() -> None:
    response = '"OK",1,0,4,4,4,1,123,0,"01020304"'
    with pytest.raises(ValueError, match="catalog size"):
        download_file(lambda _: response, "/traces/run/analyzer_1.bin",
                      chunk_size=4, expected_size=8)


class _ReadSerial:
    def __init__(self, payload: bytes) -> None:
        self.payload = bytearray(payload)

    def read(self, size: int) -> bytes:
        value = bytes(self.payload[:size])
        del self.payload[:size]
        return value


def test_mmem_catalog_reader_preserves_composite_ok_tuple() -> None:
    response = b'"OK","/traces/run",0,0,0,1,0,"EMPTY"\r\n'
    assert read_scpi_response(
        _ReadSerial(response),
        'MMEMory:CATalog:PAGE? "/traces/run",0,4',
        1.0,
        require_match=True,
    ).startswith('"OK",')


def test_run_stops_all_boards_before_any_export(monkeypatch, tmp_path) -> None:
    events: list[str] = []

    def runtime(*, running: bool, sequence: int) -> str:
        values = [0] * 40
        values[0] = int(running)
        values[4] = int(running)
        values[5] = int(running)
        values[8] = int(running)
        values[38] = sequence
        values[39] = sequence
        return ",".join(str(value) for value in values)

    status = {
        "NO1": deque([runtime(running=True, sequence=1),
                      runtime(running=False, sequence=2)]),
        "NO2": deque([runtime(running=True, sequence=1),
                      runtime(running=False, sequence=2)]),
    }

    def serial_query(board, _args):
        def query(command: str) -> str:
            events.append(f"{board.name}:{command}")
            if command == "*IDN?":
                return f"GTS,DHRT100,{board.name},0.1.0"
            if command == "SYSTem:TDMA:RING:STATus?":
                return status[board.name].popleft()
            if command == "SYSTem:TDMA:RING:STOP":
                return "OK"
            raise AssertionError(command)
        return query

    def stopped_export(board, _args, _query, preflight):
        events.append(f"{board.name}:EXPORT")
        return {"board": board.name, "passed": True, **preflight}

    monkeypatch.setattr(exporter, "_serial_query", serial_query)
    monkeypatch.setattr(exporter, "export_stopped_board", stopped_export)
    args = Namespace(
        board=["NO1=COM5", "NO2=COM4"], out_dir=tmp_path,
        stop_ring=True, latest_per_board=1, page_limit=16, max_pages=1,
        chunk_size=128, baud=115200, timeout=1.0, settle=0.0,
        stop_timeout=1.0,
    )
    result = exporter.run(args)
    assert result["passed"] is True
    stop_positions = [index for index, item in enumerate(events)
                      if item.endswith("RING:STOP")]
    export_positions = [index for index, item in enumerate(events)
                        if item.endswith(":EXPORT")]
    assert len(stop_positions) == 2
    assert max(stop_positions) < min(export_positions)
