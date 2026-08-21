from argparse import Namespace

from tools.calibration_ring_validate.calibration_link_p3 import (
    P3_FLAGS_REQUIRED,
    apply_frequency_policy,
    evaluate_pair,
    parse_p3_status,
    timing_metrics,
    validation_frequency_ladder,
)


def make_snapshot(role: int, edge_mask: int) -> dict[str, int]:
    return {
        "state": 2,
        "role": role,
        "flags": P3_FLAGS_REQUIRED,
        "reject_reason": 0,
        "baud_hz": 25_000_000,
        "epoch": 7,
        "sample_period_ns": 4,
        "pulse_count": 32,
        "requested_words": 256,
        "produced_words": 256,
        "edge_mask": edge_mask,
        "dma_overrun_count": 0,
        "pio_stall_count": 0,
        "clock_high_ns": 20,
        "clock_low_ns": 20,
        "data_high_ns": 20,
        "t1_ns": 100,
        "t2_ns": 200,
        "t3_ns": 204,
        "t4_ns": 144,
        "result_valid": 1,
    }


def test_parse_p3_status_reconstructs_u64() -> None:
    fields = [2, 1, 15, 0, 10_000_000, 9, 4, 32, 256, 256, 9,
              0, 0, 50, 50, 48, 1, 2, 3, 4, 5, 6, 7, 8, 1]
    parsed = parse_p3_status(",".join(str(value) for value in fields))
    assert parsed["t1_ns"] == 1 | (2 << 32)
    assert parsed["t4_ns"] == 7 | (8 << 32)


def test_timing_metrics_checks_frequency_and_duty() -> None:
    metrics = timing_metrics(
        {"clock_high_ns": 20, "clock_low_ns": 20, "data_high_ns": 20,
         "sample_period_ns": 4}, 25_000_000, 5.0, 10.0)
    assert metrics["actual_hz"] == 25_000_000
    assert metrics["duty_percent"] == 50.0
    assert metrics["frequency_ok"] and metrics["duty_ok"]
    assert metrics["data_high_ok"]


def test_timing_metrics_rejects_short_return_data_pulse() -> None:
    metrics = timing_metrics(
        {"clock_high_ns": 16, "clock_low_ns": 17, "data_high_ns": 8,
         "sample_period_ns": 4}, 30_000_000, 5.0, 10.0)
    assert metrics["frequency_ok"] and metrics["duty_ok"]
    assert not metrics["data_high_ok"]


def test_evaluate_pair_subtracts_residence() -> None:
    initiator = make_snapshot(1, 0x09)
    responder = make_snapshot(2, 0x06)
    args = Namespace(frequency_tolerance_percent=5.0,
                     duty_tolerance_percent=10.0)
    result = evaluate_pair(initiator, responder, 25_000_000, args)
    assert result["source_rtt_ns"] == 44
    assert result["residence_ns"] == 4
    assert result["path_sum_ns"] == 40
    assert result["delay_estimate_ns"] == 20.0
    assert result["passed"]


def test_30mhz_is_limited_rx_and_falls_back_without_failing_stable() -> None:
    ladder = [
        {"frequency_mhz": 10, "passed": True},
        {"frequency_mhz": 25, "passed": True},
        {"frequency_mhz": 30, "passed": False},
    ]
    policy = apply_frequency_policy(ladder)
    assert policy["stable_profiles_passed"]
    assert policy["highest_stable_frequency_mhz"] == 25
    assert not policy["limited_rx_all_trials_passed"]
    assert ladder[2]["operational_status"] == "FALLBACK_25MHZ"
    assert not ladder[2]["required_for_stable"]
    assert policy["limited_rx_executed"]
    assert policy["limited_rx_operational_status"] == "FALLBACK_25MHZ"


def test_every_validation_requires_30mhz_limited_rx() -> None:
    assert validation_frequency_ladder(None) == [10, 25, 30]
    assert validation_frequency_ladder([10, 25, 30]) == [10, 25, 30]
    try:
        validation_frequency_ladder([10, 25])
    except ValueError as exc:
        assert "exactly 10,25,30" in str(exc)
    else:
        raise AssertionError("validation without 30 MHz must be rejected")


def test_highest_stable_frequency_requires_all_links_to_pass() -> None:
    ladder = [
        {"frequency_mhz": 10, "passed": True},
        {"frequency_mhz": 25, "passed": True},
        {"frequency_mhz": 30, "passed": True},
        {"frequency_mhz": 10, "passed": True},
        {"frequency_mhz": 25, "passed": False},
        {"frequency_mhz": 30, "passed": True},
    ]
    policy = apply_frequency_policy(ladder)
    assert not policy["stable_profiles_passed"]
    assert policy["highest_stable_frequency_mhz"] == 10
    assert policy["limited_rx_all_trials_passed"]
