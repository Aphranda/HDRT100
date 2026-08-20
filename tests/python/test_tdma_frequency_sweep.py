from tools.tdma_ring_monitor.tdma_frequency_sweep import (
    Profile,
    parse_catalog,
    render_summary_markdown,
    score_board,
)


def clean_row(**overrides):
    row = {
        "up_running": 1,
        "down_running": 1,
        "baud_hz": 25_000_000,
        "expected_baud_hz": 25_000_000,
        "adapter_rx_bad_delta": 0,
        "phys_rx_bad_delta": 0,
        "stall_delta": 0,
        "tx_timeout_delta": 0,
        "ring_overrun_delta": 0,
        "adapter_tx_rate": 500.0,
        "adapter_rx_rate": 500.0,
        "expected_loop_rate": 500.0,
    }
    row.update(overrides)
    return row


def test_parse_catalog_uses_firmware_field_order():
    profiles = parse_catalog(
        "2,0,10000000,2000000,4096,0,123,1,25000000,1000000,4096,0,456")
    assert profiles == [
        Profile(0, 10_000_000, 2_000_000, 4096, 0, 123),
        Profile(1, 25_000_000, 1_000_000, 4096, 0, 456),
    ]


def test_clean_profile_scores_a():
    assert score_board(clean_row()) == (100, "A")


def test_overrun_and_slow_rate_reduce_score():
    score, grade = score_board(clean_row(
        ring_overrun_delta=1,
        adapter_tx_rate=250.0,
        adapter_rx_rate=250.0,
    ))
    assert score == 72
    assert grade == "C"


def test_broken_link_is_zero():
    assert score_board(clean_row(down_running=0)) == (0, "F")


def test_summary_markdown_contains_dynamic_rate():
    text = render_summary_markdown([{
        "level": 7,
        "frequency_hz": 25_000_000,
        "cycle_period_ns": 1_000_000,
        "expected_loop_rate": 500.0,
        "score": 70,
        "grade": "C",
        "passed": False,
    }])
    assert "25 MHz" in text
    assert "500/s" in text
    assert "| 70 | C | FAIL |" in text
