from __future__ import annotations

from tools.analyzer_tdma_hil.analyzer_tdma_hil import compare


def sample(values: dict[str, int]) -> dict:
    fields = {
        "ring_adapter_rx_bad_count": 0,
        "ring_adapter_rx_transport_bad_count": 0,
        "ring_adapter_rx_schedule_bad_count": 0,
        "ring_adapter_rx_profile_bad_count": 0,
        "ring_adapter_last_error": 0,
        "ring_adapter_rx_count": 10,
        "ring_up_tx_sequence": 20,
        "ring_down_rx_sequence": 30,
        "ring_up_running": 1,
        "ring_down_running": 1,
    }
    fields.update(values)
    return {"raw": "", "fields": fields}


def test_compare_passes_when_ring_progresses_without_errors() -> None:
    result = compare(sample({}), sample({
        "ring_adapter_rx_count": 20,
        "ring_up_tx_sequence": 21,
        "ring_down_rx_sequence": 31,
    }), "1", "1", 2.0)
    assert result["passed"] is True
    assert result["checks"]["no_analyzer_induced_bad_growth"] is True


def test_compare_rejects_bad_counter_growth() -> None:
    result = compare(sample({}), sample({
        "ring_adapter_rx_count": 20,
        "ring_adapter_rx_bad_count": 3,
    }), "1", "1", 2.0)
    assert result["passed"] is False
    assert result["checks"]["no_analyzer_induced_bad_growth"] is False
