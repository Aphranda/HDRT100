from dataclasses import replace

from tools.tdma_ring_monitor.tdma_cycle_schedule import (
    load_schedule,
    render_markdown,
    render_svg,
    validate_schedule,
)


def test_repository_cycle_schedule_is_disjoint_and_bounded() -> None:
    schedule = load_schedule()
    assert validate_schedule(schedule) == []
    assert schedule.cycle_cycles == 250_000
    assert schedule.spi_cycles_per_bit == 25
    assert schedule.wire_max_cycles == 61_400
    assert schedule.phases[0].name == "TDMA"
    assert schedule.phases[-1].name == "GUARD"


def test_observed_wcet_regression_fails_closed() -> None:
    schedule = load_schedule(observed={"DPLL": 10_001})
    assert validate_schedule(schedule) == [
        "DPLL: observed runtime exceeds WCET"
    ]


def test_phase_overlap_is_rejected() -> None:
    schedule = load_schedule()
    phases = list(schedule.phases)
    phases[1] = replace(phases[1], start_cycle=phases[1].start_cycle - 1)
    errors = validate_schedule(replace(schedule, phases=tuple(phases)))
    assert any("does not equal previous end" in error for error in errors)


def test_renderers_keep_cycles_as_source_of_truth() -> None:
    schedule = load_schedule()
    markdown = render_markdown(schedule)
    svg = render_svg(schedule)
    assert "start_cycle" in markdown
    assert "derived window" in markdown
    assert "250000 clk_sys cycles" in svg
    assert "TDMA" in svg
