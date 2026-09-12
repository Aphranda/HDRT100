"""Decode versioned whole-phase TDMA profiles; nested stages are inclusive."""
import csv


STAGES_V1 = (
    "phys_service", "owner_service", "refmem_publish", "training_gate", "analyzer",
    "accounting", "adapter", "rx_capture", "rx_parse", "overlay_prepare", "overlay_boundary",
)
STAGES_V2 = STAGES_V1 + ("rx_acquire", "rx_packet_copy", "rx_clock", "rx_latch")
STAGES_BY_VERSION = {
    1: STAGES_V1,
    2: STAGES_V2,
    3: STAGES_V2 + ("rx_dma_observe", "rx_locate", "rx_header_check", "rx_ring_copy"),
}
FIELDS = (
    "version", "clock_hz", "reset_generation", "phase_count", "stage_count",
    "peak", "sequence", "start_ticks", "total_ticks", "invalid_count",
)


def parse_service_timing(raw: str) -> dict | None:
    """Reject unknown schemas instead of silently assigning the wrong labels."""
    if raw.strip().strip('"') == "UNAVAILABLE":
        return None
    values = list(map(int, next(csv.reader([raw]))))
    if len(values) < len(FIELDS):
        raise ValueError("Truncated TDMA profile header")
    stages = STAGES_BY_VERSION.get(values[0])
    if stages is None:
        raise ValueError(f"Unsupported TDMA profile version: {values[0]}")
    if values[4] != len(stages) or len(values) != len(FIELDS) + 2 * len(stages):
        raise ValueError("TDMA profile stage count or length mismatch")
    result = dict(zip(FIELDS, values[:len(FIELDS)]))
    result["stages"] = {
        name: {"ticks": values[len(FIELDS) + 2*i], "calls": values[len(FIELDS) + 2*i + 1]}
        for i, name in enumerate(stages)
    }
    return result
