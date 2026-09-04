from __future__ import annotations

from pathlib import Path

from tools.analyzer_trace_batch_index.analyzer_trace_batch_index import (
    build_index,
    write_csv,
)
from tools.analyzer_trace_decode.analyzer_trace_decode import (
    HEADER,
    MAGIC,
    RECORD,
    crc32,
)


def _segment(path: Path, session: int, records: list[tuple[int, int]]) -> None:
    payload = b"".join(RECORD.pack(tick, 4, sequence, 1, 0, 0, 0)
                        for tick, sequence in records)
    path.write_bytes(HEADER.pack(MAGIC, 1, HEADER.size, session,
                                 len(records), 2, crc32(payload)) + payload)


def test_batch_index_materializes_gaps_and_crc(tmp_path: Path) -> None:
    first = tmp_path / "a.bin"
    second = tmp_path / "b.bin"
    _segment(first, 8, [(10, 1), (20, 2)])
    _segment(second, 8, [(30, 4)])
    result = build_index([second, first])
    assert result["schema"] == "HAOFV_SYNC_IO_ANALYZER_BATCH_INDEX_V1"
    assert result["segment_count"] == 2
    assert result["sessions"][0]["dropped_records"] == 4
    intervals = result["segments"][1]["drop_intervals"]
    assert any(row["reason"] == "cross_segment_sequence_gap" and
               row["missing_count"] == 1 for row in intervals)
    assert all(all(row["checks"].values()) for row in result["segments"])


def test_batch_index_csv_is_bounded(tmp_path: Path) -> None:
    path = tmp_path / "one.bin"
    _segment(path, 1, [(1, 1)])
    result = build_index([path])
    output = tmp_path / "index.csv"
    write_csv(output, result)
    assert output.read_text(encoding="utf-8").splitlines()[0].startswith("path,")
