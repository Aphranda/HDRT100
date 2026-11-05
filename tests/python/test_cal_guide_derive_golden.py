"""Golden vectors for the R2b board-side derivation (C port oracle).

NOT firmware. These are the exact numbers the C implementation of
``calibration_stage_derive`` must reproduce for the training profiles, so the
Python host tools and the board cannot drift silently. The values were computed
from ``tools/calibration_ring_validate/trn03_matrix.pio_facts`` and
``calibration_phase`` on 2026-11-05 and are frozen here.

Covered fields: pio_persona, clkdiv_q16, clk_sys_hz, instruction_period_ns,
bit_cycles (profile/persona derived) plus link_base_delay_ns and
phase_delay_samples (offset/base derived).
"""

from __future__ import annotations

from tools.calibration_ring_validate.calibration_phase import (
    link_base_delay_ns,
    phase_delay_samples,
)
from tools.calibration_ring_validate.trn03_matrix import pio_facts

GOLDEN_PIO = {
    7: {
        "pio_persona": 1,
        "clkdiv_q16": 273152,
        "clk_sys_hz": 250000000,
        "instruction_period_ns": 17,
        "bit_cycles": 6,
        "baud_hz": 10000000,
        "cycle_period_ns": 1000000,
    },
    8: {
        "pio_persona": 1,
        "clkdiv_q16": 109312,
        "clk_sys_hz": 250000000,
        "instruction_period_ns": 7,
        "bit_cycles": 6,
        "baud_hz": 25000000,
        "cycle_period_ns": 1000000,
    },
    9: {
        "pio_persona": 1,
        "clkdiv_q16": 91136,
        "clk_sys_hz": 250000000,
        "instruction_period_ns": 6,
        "bit_cycles": 6,
        "baud_hz": 30000000,
        "cycle_period_ns": 1000000,
    },
    20: {
        "pio_persona": 1,
        "clkdiv_q16": 273152,
        "clk_sys_hz": 250000000,
        "instruction_period_ns": 17,
        "bit_cycles": 6,
        "baud_hz": 10000000,
        "cycle_period_ns": 375000,
    },
    21: {
        "pio_persona": 1,
        "clkdiv_q16": 273152,
        "clk_sys_hz": 250000000,
        "instruction_period_ns": 17,
        "bit_cycles": 6,
        "baud_hz": 10000000,
        "cycle_period_ns": 500000,
    },
    22: {
        "pio_persona": 1,
        "clkdiv_q16": 273152,
        "clk_sys_hz": 250000000,
        "instruction_period_ns": 17,
        "bit_cycles": 6,
        "baud_hz": 10000000,
        "cycle_period_ns": 750000,
    },
}

GOLDEN_BASE_NS = {160: 80, 162: 81, 164: 82}


def test_pio_facts_match_the_frozen_golden_vectors() -> None:
    for level, expected in GOLDEN_PIO.items():
        assert pio_facts(level) == expected, level


def test_link_base_delay_golden_vectors() -> None:
    for link_delay_ns, expected in GOLDEN_BASE_NS.items():
        assert link_base_delay_ns(link_delay_ns) == expected


def test_phase_delay_samples_golden_vectors() -> None:
    assert phase_delay_samples(
        link_base_ns=80, sample_period_ns=4, node_offset_samples=0) == 20
    assert phase_delay_samples(
        link_base_ns=80, sample_period_ns=4, node_offset_samples=1) == 21
    assert phase_delay_samples(
        link_base_ns=80, sample_period_ns=4, node_offset_samples=-1) == 19
    assert phase_delay_samples(
        link_base_ns=81, sample_period_ns=4, node_offset_samples=0) == 20
