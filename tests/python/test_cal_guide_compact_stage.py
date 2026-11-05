"""Design model for the compact stage payload (R2b).

NOT firmware. It fixes the minimal "measured only" field set that must cross
the wire for M5 and checks it stays inside the existing 0x55 fragment cap.
Mirrors ``docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`` 9.13.

Measured words per link:
- marker_offset_sample_count
- sck_offset_sample_count
- data_offset_sample_count
- origin_capture_offset_sample_count
- link_base_delay_ns
- forward_residence_cycles

Everything else (persona/derived phases/budget) is computed on the board from
the profile and persona, with a host/board golden cross-check.
"""

from __future__ import annotations

COMPACT_LINK_WORDS = 6
WORD_BYTES = 4
HEADER_BYTES = 8
FRAGMENT_PAYLOAD_MAX = 256
FRAGMENT_DATA_PER_FRAME = 32
FRAME_S = 1.2


def compact_bytes(links: int) -> int:
    return HEADER_BYTES + links * COMPACT_LINK_WORDS * WORD_BYTES


def frames(links: int) -> int:
    return -(-compact_bytes(links) // FRAGMENT_DATA_PER_FRAME)


def delivery_s(links: int) -> float:
    return frames(links) * FRAME_S


def test_four_link_compact_stage_fits_the_existing_fragment_cap() -> None:
    assert compact_bytes(4) == 104
    assert compact_bytes(4) <= FRAGMENT_PAYLOAD_MAX
    assert frames(4) == 4
    assert delivery_s(4) == 4.8


def test_six_nodes_still_fit() -> None:
    assert compact_bytes(6) == 152
    assert compact_bytes(6) <= FRAGMENT_PAYLOAD_MAX


def test_full_stage_does_not_fit_and_needs_r2a() -> None:
    full_stage_bytes = 544
    assert full_stage_bytes > FRAGMENT_PAYLOAD_MAX


def test_each_link_carries_exactly_six_measured_words() -> None:
    measured = {
        "marker_offset_sample_count",
        "sck_offset_sample_count",
        "data_offset_sample_count",
        "origin_capture_offset_sample_count",
        "link_base_delay_ns",
        "forward_residence_cycles",
    }
    assert len(measured) == COMPACT_LINK_WORDS
