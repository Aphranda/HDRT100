from dataclasses import replace

from tools.tdma_ring_monitor.tdma_process_image_budget import (
    load_budget,
    render_markdown,
    validate_budget,
)


def test_mandatory_first_layout_fills_node_body() -> None:
    budget = load_budget()
    assert budget.node_count == 8
    assert budget.node_bytes == 32
    assert budget.body_bytes == 24
    assert budget.mandatory_body_bytes == 23
    assert budget.optional_body_capacity == 1
    assert budget.optional_body_bytes == 1
    assert budget.runtime_free_bytes == 0
    assert budget.node_image_bytes == 256
    assert budget.dpll_observation_bytes == 4
    assert budget.process_image_bytes == 260
    assert validate_budget(budget) == []


def test_optional_load_cannot_exceed_residual_capacity() -> None:
    budget = load_budget()
    overcommitted = replace(
        budget,
        optional_body_bytes=budget.optional_body_capacity + 1,
        runtime_free_bytes=-1,
    )
    errors = validate_budget(overcommitted)
    assert "optional body exceeds residual capacity" in errors


def test_runtime_opportunistic_capacity_is_rejected() -> None:
    budget = replace(load_budget(), optional_body_bytes=0,
                     runtime_free_bytes=1)
    assert "configured layout must leave no runtime-free bytes" in (
        validate_budget(budget))


def test_budget_report_exposes_priority_and_capacity() -> None:
    report = render_markdown(load_budget())
    assert "vdc_dpll | mandatory" in report
    assert "diagnostic | optional" in report
    assert "runtime-free: 0 B" in report
    assert "DPLL observation trailer: 4 B" in report
