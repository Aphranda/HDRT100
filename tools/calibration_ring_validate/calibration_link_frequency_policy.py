"""Shared frequency policy for P3 per-link validation tools."""

from __future__ import annotations


REQUIRED_FREQUENCY_LADDER_MHZ = (10, 25, 30)
LIMITED_RX_FREQUENCY_MHZ = 30
LIMITED_RX_FALLBACK_MHZ = 25


def validation_frequency_ladder(
        requested: list[int] | None) -> list[int]:
    """Require every P3 validation to exercise 30 MHz limited RX."""
    ladder = list(REQUIRED_FREQUENCY_LADDER_MHZ) if requested is None else requested
    if ladder != list(REQUIRED_FREQUENCY_LADDER_MHZ):
        raise ValueError("frequency ladder must be exactly 10,25,30 MHz")
    return ladder
