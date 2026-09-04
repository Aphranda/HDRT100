from __future__ import annotations

from tools.analyzer_no5_correlator.analyzer_no5_correlator import correlate


def test_correlate_aligns_common_timebase_and_preserves_sequence_domains() -> None:
    analyzer = {
        "source": "analyzer.bin",
        "header": {"session": 9, "metadata": {
            "capture_sequence": 7, "profile_generation": 3,
            "persona_generation": 4, "source_mask": 0x30,
            "hardware_tick_hz": 1_000_000_000,
            "timestamp_resolution_ns": 250_000}},
        "records": [
            {"hardware_tick": 1_000, "capture_sequence": 7,
             "record_sequence": 10},
            {"hardware_tick": 2_000, "capture_sequence": 7,
             "record_sequence": 11},
        ],
    }
    no5 = {
        "source": "NO5_SD_PIO0_RAW_WAVEFORM",
        "session_id": 22,
        "segments": [{"sample_period_ns": 1_000,
                       "timestamp_resolution_ns": 1,
                       "timestamp_source": 2}],
        "records": [{"sample_seq": 100, "matched_window_start_ns": 1_000},
                    {"sample_seq": 101, "matched_window_start_ns": 2_000}],
    }
    result = correlate(analyzer, no5)
    assert result["correlation"]["timebase_compatible"] is True
    assert result["correlation"]["timestamp_overlap"] is True
    assert result["correlation"]["pair_count"] == 2
    assert result["pairs"][0]["delta_ns"] == 0
    assert result["sequence_alignment"]["status"] == "separate_domains"
    assert result["evidence_boundaries"]["mutually_substitutable"] is False


def test_correlate_accepts_explicit_anchor_and_tolerance() -> None:
    analyzer = {"header": {"metadata": {"capture_sequence": 2,
                                           "hardware_tick_hz": 0}},
                "records": []}
    no5 = {"session_id": 5, "segments": [], "records": []}
    result = correlate(analyzer, no5, tolerance_ns=10,
                       sequence_anchor={"tdma_cycle": 123})
    assert result["correlation"]["pair_count"] == 0
    assert result["sequence_alignment"]["status"] == "explicit_anchor"
    assert result["sequence_alignment"]["anchor"]["tdma_cycle"] == 123
