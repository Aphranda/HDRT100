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
    assert schedule.tdma_software_margin_cycles == 30_000
    assert schedule.phases[0].wcet_cycles >= 91_400
    assert schedule.phases[0].window_cycles > schedule.phases[0].wcet_cycles
    assert schedule.phases[0].name == "TDMA"
    assert schedule.phases[-1].name == "GUARD"


def test_observed_wcet_regression_fails_closed() -> None:
    configured = load_schedule()
    dpll_wcet = next(
        phase.wcet_cycles for phase in configured.phases
        if phase.name == "DPLL")
    schedule = load_schedule(observed={"DPLL": dpll_wcet + 1})
    assert validate_schedule(schedule) == [
        "DPLL: observed runtime exceeds WCET"
    ]


def test_phase_overlap_is_rejected() -> None:
    schedule = load_schedule()
    phases = list(schedule.phases)
    phases[1] = replace(phases[1], start_cycle=phases[1].start_cycle - 1)
    errors = validate_schedule(replace(schedule, phases=tuple(phases)))
    assert any("does not equal previous end" in error for error in errors)


def test_tdma_software_margin_is_part_of_compile_budget() -> None:
    schedule = load_schedule()
    phases = list(schedule.phases)
    phases[0] = replace(phases[0], wcet_cycles=91_399)
    errors = validate_schedule(replace(schedule, phases=tuple(phases)))
    assert "wire serialization plus software margin exceeds TDMA WCET" in errors


def test_renderers_keep_cycles_as_source_of_truth() -> None:
    schedule = load_schedule()
    markdown = render_markdown(schedule)
    svg = render_svg(schedule)
    assert "start_cycle" in markdown
    assert "derived window" in markdown
    assert "250000 clk_sys cycles" in svg
    assert "TDMA" in svg
