from tools.tdma_ring_monitor.tdma_load_budget import (
    calculate_budget,
    render_markdown,
)


def test_25mhz_1ms_eight_node_store_forward_fits() -> None:
    budget = calculate_budget(25_000_000, 1000.0, 8)
    assert budget.wire_bytes == 292
    assert round(budget.serialization_us, 2) == 93.44
    assert round(budget.link_utilization_percent, 3) == 9.344
    assert round(budget.store_forward_round_trip_us, 2) == 747.52
    assert round(budget.store_forward_per_node_margin_us, 2) == 31.56
    assert budget.link_budget_pass
    assert budget.store_forward_pass


def test_25mhz_100us_requires_cut_through_and_more_guard() -> None:
    budget = calculate_budget(25_000_000, 100.0, 8)
    assert round(budget.link_utilization_percent, 2) == 93.44
    assert budget.required_spi_hz_at_target == 29_200_000
    assert not budget.link_budget_pass
    assert not budget.store_forward_pass
    assert round(budget.cut_through_per_forward_node_budget_us, 3) == 0.937


def test_25mhz_10us_is_below_one_frame_serialization() -> None:
    budget = calculate_budget(25_000_000, 10.0, 2)
    assert not budget.link_budget_pass
    assert not budget.store_forward_pass
    assert budget.cut_through_total_margin_us < 0.0


def test_50mhz_100us_has_link_guard_but_not_eight_node_store_forward() -> None:
    budget = calculate_budget(50_000_000, 100.0, 8)
    assert budget.link_budget_pass
    assert not budget.store_forward_pass
    assert round(budget.cut_through_per_forward_node_budget_us, 3) == 7.611


def test_markdown_contains_gate_results() -> None:
    text = render_markdown([
        calculate_budget(25_000_000, 1000.0, 2),
        calculate_budget(25_000_000, 10.0, 2),
    ])
    assert "292 B" in text
    assert "PASS" in text
    assert "FAIL" in text


def test_recovery_budget_is_two_buffer_single_frame_and_has_headroom() -> None:
    budget = calculate_budget(25_000_000, 1000.0, 4)
    assert budget.normal_reserved_bytes_per_cycle == 712
    assert budget.recovery_reserved_bytes_per_cycle == 128
    assert budget.planned_reserved_bytes_per_cycle == 840
    assert budget.usable_cycle_capacity_bytes == 896
    assert budget.planned_headroom_bytes == 56
    assert budget.recovery_buffer_count == 2
    assert budget.recovery_max_frames_per_cycle == 1
