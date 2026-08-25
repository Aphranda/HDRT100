from tools.calibration_ring_validate.calibration_link_plan import (
    make_links,
    validation_frequency_ladder,
)
from tools.tdma_ring_monitor.tdma_pio_timing_check import audit


def test_make_links_wraps_ring() -> None:
    links = make_links(["A", "B", "C", "D"])
    assert [(row["source"], row["destination"]) for row in links] == [
        ("A", "B"), ("B", "C"), ("C", "D"), ("D", "A")
    ]


def test_reflection_report_has_balanced_ladder() -> None:
    report = audit(
        [10_000_000, 25_000_000, 30_000_000],
        expected_duty_percent=50.0,
        frequency_tolerance_percent=1.0,
        duty_tolerance_percent=1.0,
    )
    rows = report["reflection_calibration"]["burst_profiles"]
    assert [row["target_hz"] for row in rows] == [10_000_000, 25_000_000, 30_000_000]
    assert all(row["duty_percent"] == 50.0 for row in rows)
    assert all(row["frequency_ok"] and row["duty_ok"] for row in rows)


def test_preflight_requires_complete_limited_rx_ladder() -> None:
    assert validation_frequency_ladder(None) == [10, 25, 30]
    try:
        validation_frequency_ladder([25, 30])
    except ValueError as exc:
        assert "exactly 10,25,30" in str(exc)
    else:
        raise AssertionError("incomplete P3 ladder must be rejected")
