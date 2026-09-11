"""Finite capture evidence must reject corruption and mixed acquisition files."""
import struct
import zlib

import pytest
from tools.analyzer_burst import analyzer_burst

from tools.analyzer_burst.analyzer_burst import (
    FACT_FIELDS, HEADER, MAGIC, assemble_capture, decode_segment,
)


def segment(words=(0x01083105,), *, index=0, first=0, **changes):
    facts = dict.fromkeys(FACT_FIELDS, 0)
    facts.update(schema=1, state=2, capture_sequence=7, end_reason=1,
                 timing_valid=1, requested_words=1, captured_words=1,
                 clk_sys_hz=250_000_000, clkdiv_256=256, sample_cycles=2,
                 samples_per_word=5, sample_bits=6, pin_base=24,
                 source_mask=63 << 24, trigger_pin=26, persona_generation=1,
                 profile_identity=sum(pin << (5 * i) for i, pin in
                                      enumerate((25, 26, 24, 27, 28, 29))),
                 capture_tag=19)
    build = changes.pop("build", b"20260911123456")
    facts.update(changes)
    payload = struct.pack(f"<{len(words)}I", *words)
    return HEADER.pack(MAGIC, 1, HEADER.size, facts["capture_tag"], index, first,
                       len(words), zlib.crc32(payload),
                       *(facts[k] for k in FACT_FIELDS), build) + payload


def test_known_sample_order_and_timebase():
    word = sum(value << shift for value, shift in zip((1, 2, 3, 4, 5), (24, 18, 12, 6, 0)))
    result = assemble_capture([decode_segment(segment((word,)))])
    assert result["levels"] == [1, 2, 3, 4, 5]
    assert result["sample_period_ns"] == 8
    assert result["timing_valid"] is True
    assert result["b0_pipeline_accepted"] is False


@pytest.mark.parametrize("position", [HEADER.size, HEADER.size + 3])
def test_payload_bit_corruption(position):
    data = bytearray(segment())
    data[position] ^= 1
    with pytest.raises(ValueError, match="CRC"):
        decode_segment(data)


@pytest.mark.parametrize("length", [0, HEADER.size - 1, HEADER.size, HEADER.size + 3])
def test_truncated_files(length):
    with pytest.raises(ValueError):
        decode_segment(segment()[:length])


@pytest.mark.parametrize("changes", [
    {"clk_sys_hz": 0}, {"clkdiv_256": 0}, {"clkdiv_256": 257},
    {"trigger_pin": 0xffffffff}, {"trigger_pin": 25}, {"source_mask": 0},
    {"profile_identity": 0}, {"capture_tag": 0}, {"persona_generation": 0},
    {"state": 3}, {"timing_valid": 2}, {"end_reason": 0},
    {"sample_cycles": 1}, {"samples_per_word": 6}, {"build": b"unbound"},
    {"requested_words": 0}, {"requested_words": 8193},
])
def test_invalid_metadata(changes):
    with pytest.raises(ValueError):
        decode_segment(segment(**changes))


def test_padding_is_not_a_sixth_sample():
    with pytest.raises(ValueError, match="padding"):
        decode_segment(segment((1 << 30,)))


def test_missing_duplicate_and_unordered_segments():
    first = decode_segment(segment(requested_words=2, captured_words=2))
    second = decode_segment(segment(index=1, first=1, requested_words=2, captured_words=2))
    assert assemble_capture([second, first])["sample_count"] == 10
    for rows in ([], [first], [second], [first, first], [first, second, second]):
        with pytest.raises(ValueError):
            assemble_capture(rows)


@pytest.mark.parametrize("changes", [
    {"capture_tag": 20}, {"capture_sequence": 8}, {"build": b"20260911123457"},
    {"clk_sys_hz": 200_000_000},
])
def test_mixed_capture_identity(changes):
    first = decode_segment(segment(requested_words=2, captured_words=2))
    second = decode_segment(segment(index=1, first=1, requested_words=2, captured_words=2, **changes))
    with pytest.raises(ValueError, match="mixed capture"):
        assemble_capture([first, second])


@pytest.mark.parametrize("changes", [
    {"end_reason": 3}, {"end_reason": 4}, {"timing_valid": 0},
    {"pio_fdebug": 1}, {"dma_ctrl": 1 << 29}, {"dma_ctrl": 1 << 30},
    {"dma_remaining": 1}, {"manager_error": 1}, {"conflict_mask": 1},
])
def test_faults_cannot_be_promoted_to_timing_pass(changes):
    result = assemble_capture([decode_segment(segment(**changes))])
    assert result["timing_valid"] is False
    assert result["levels"]  # keep raw samples for diagnosis


def test_empty_timeout_is_retained_without_timing_claim():
    result = assemble_capture([decode_segment(segment((), captured_words=0,
        dma_remaining=1, timing_valid=0, end_reason=3))])
    assert result["sample_count"] == 0
    assert result["timing_valid"] is False


def test_download_uses_epoch_filenames_and_checks_published_identity(tmp_path, monkeypatch):
    data = segment()
    facts = decode_segment(data)["capture"]
    paths = []
    def read(query, path, expected_size):
        assert expected_size == len(data)
        paths.append(path)
        return data, []
    monkeypatch.setattr(analyzer_burst, "download_file", read)
    result, downloads = analyzer_burst.download_capture(None, tmp_path,
        {**facts, "state": 3}, "20260911123456")
    assert paths == ["/traces/run/burst_00000019_00000007_0000.bin"]
    assert result["sample_count"] == 5 and len(downloads) == 1
    with pytest.raises(ValueError, match="identity mismatch"):
        analyzer_burst.download_capture(None, tmp_path,
            {**facts, "state": 3, "capture_tag": 20}, "20260911123456")
