from tools.tdma_ring_monitor.tdma_pio_timing_check import (
    audit,
    calculate_timing,
    calculate_burst_timing,
    parse_sideset_loop_cycles,
)


def test_parse_sideset_loop_counts_delay_slots() -> None:
    body = """
bitloop:
    out pins, 1 side 0 [1]
    nop side 1 [1]
    jmp x-- bitloop side 0 [2]
"""
    assert parse_sideset_loop_cycles(body, "bitloop") == (2, 5)


def test_calculate_timing_detects_declared_cycle_mismatch() -> None:
    result = calculate_timing(
        250_000_000, 10_000_000, declared_cycles=6,
        high_cycles=2, low_cycles=5, expected_duty_percent=50.0,
        frequency_tolerance_percent=1.0, duty_tolerance_percent=1.0)
    assert 8_500_000 < result.actual_hz < 8_600_000
    assert result.duty_percent == 100.0 * 2.0 / 7.0
    assert result.frequency_ok is False
    assert result.duty_ok is False


def test_repository_pio_timing_gate_fails_on_current_mismatch() -> None:
    report = audit([10_000_000, 25_000_000, 30_000_000], 50.0, 1.0, 1.0)
    assert report["divider_declared_cycles_per_bit"] == 6
    assert report["pio_actual_cycles_per_bit"] == 6
    assert report["pio_high_cycles"] == 3
    assert report["pio_low_cycles"] == 3
    assert report["coded_persona_baud_dependent"] is False
    assert report["passed"] is True


def test_reflection_burst_is_four_cycles_and_balanced() -> None:
    result = calculate_burst_timing(
        250_000_000, 10_000_000, declared_cycles=4,
        high_cycles=2, low_cycles=2, expected_duty_percent=50.0,
        frequency_tolerance_percent=1.0, duty_tolerance_percent=1.0)
    assert result.actual_hz == 10_000_000.0
    assert result.high_ns == result.low_ns == 50.0
    assert result.duty_percent == 50.0
    assert result.frequency_ok is True
    assert result.duty_ok is True


def test_repository_report_contains_reflection_calibration_gate() -> None:
    report = audit([10_000_000, 25_000_000, 30_000_000], 50.0, 1.0, 1.0)
    reflection = report["reflection_calibration"]
    assert reflection["burst_cycles_per_period"] == 4
    assert reflection["burst_high_cycles"] == 2
    assert reflection["burst_low_cycles"] == 2
    assert reflection["forward_edge_regeneration"] is True
    assert reflection["forward_local_divider"] == 1.0
    assert all(row["duty_percent"] == 50.0
               for row in reflection["burst_profiles"])
