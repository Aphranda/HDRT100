from tools.calibration_ring_validate.calibration_phase import (
    MAX_CALIBRATED_OFFSET_SAMPLES,
    OFFSET_MATRIX_SCHEMA,
    PHASE_TRAINING_SCHEMA,
    PHASE_TRAINING_STAGES,
    build_observed_offset_matrix,
    build_offset_rows,
    build_phase_training_contract,
    classify_forward_sweep,
    classify_offset_window,
    link_base_delay_ns,
    measure_forward_scK,
    validate_generation,
)


def test_mark_sck_and_data_share_one_phase_training_contract() -> None:
    plans = [
        build_phase_training_contract(
            signal=signal,
            link_delay_ns_by_link=[80, 82, 80, 82],
            node_offset_samples=[1, -1, 0, 1],
            sample_period_ns=4,
            capture_origin=origin,
        )
        for signal, origin in (
            ("MARK", "rx_csn_pio_edge"),
            ("SCK", "rx_sck_pio_edge"),
            ("DATA", "rx_csn_pio_edge"),
        )
    ]
    assert {plan["schema"] for plan in plans} == {PHASE_TRAINING_SCHEMA}
    assert {tuple(plan["stage_order"]) for plan in plans} == {
        PHASE_TRAINING_STAGES}
    assert {plan["formula"] for plan in plans} == {
        "round((link_delay_ns / path_delay_baseline_divisor) / "
        "sample_period_ns) + node_offset_samples"}
    assert [row["link_base_delay_ns"] for row in plans[0]["links"]] == [
        40, 41, 40, 41]


def test_every_signal_uses_the_same_full_cartesian_node_matrix() -> None:
    rows = build_offset_rows(
        node_count=4, values_by_node=[[-1, 0, 1]] * 4,
        sample_period_ns=4)
    assert len(rows) == 81
    assert rows[0]["offset_sample_counts_by_node"] == [-1, -1, -1, -1]
    assert rows[-1]["offset_ns_by_node"] == [4, 4, 4, 4]

    matrix = build_observed_offset_matrix(
        signal="DATA", values_by_node=[[0, 1], [-1, -1], [0, 0], [1, 1]],
        sample_period_ns=4)
    assert matrix["schema"] == OFFSET_MATRIX_SCHEMA
    assert matrix["recommended_offset_sample_counts_by_node"] == [0, -1, 0, 1]
    assert matrix["full_matrix_row_count"] == 2


def test_observed_data_matrix_allows_rebased_final_offset() -> None:
    matrix = build_observed_offset_matrix(
        signal="DATA", values_by_node=[[16], [17], [17], [16]],
        sample_period_ns=4,
        min_offset_samples=-MAX_CALIBRATED_OFFSET_SAMPLES,
        max_offset_samples=MAX_CALIBRATED_OFFSET_SAMPLES)
    assert matrix["recommended_offset_sample_counts_by_node"] == [16, 17, 17, 16]
    assert matrix["full_matrix_row_count"] == 1


def test_phase_contract_requires_one_destination_per_node() -> None:
    try:
        build_phase_training_contract(
            signal="DATA", link_delay_ns_by_link=[80, 80],
            node_offset_samples=[0, 0], sample_period_ns=4,
            destination_node_by_link=[1, 1])
    except ValueError as exc:
        assert "every destination Node" in str(exc)
    else:
        raise AssertionError("duplicate destination mapping must be rejected")


def test_phase_contract_supports_per_link_third_path_baseline() -> None:
    assert [link_base_delay_ns(value, 3) for value in [82, 80, 80, 82]] == [
        27, 27, 27, 27]
    plan = build_phase_training_contract(
        signal="SCK", link_delay_ns_by_link=[82, 80, 80, 82],
        node_offset_samples=[0, 0, 0, 0], sample_period_ns=4,
        path_delay_baseline_divisor=3)
    assert plan["path_delay_baseline_divisor"] == 3
    assert [row["link_base_delay_ns"] for row in plan["links"]] == [
        27, 27, 27, 27]


def test_generation_validation_rejects_zero_and_u32_overflow() -> None:
    assert validate_generation(1) == 1
    assert validate_generation(0xFFFFFFFF) == 0xFFFFFFFF
    for value in (0, -1, 0x100000000):
        try:
            validate_generation(value)
        except ValueError as exc:
            assert "within 1..4294967295" in str(exc)
        else:
            raise AssertionError("out-of-range generation must be rejected")


def _sweep(delays_by_target: dict[int, float]) -> list[dict[str, object]]:
    return [
        {"target_cycles": target,
         "rise_delay_ns": delay,
         "fall_delay_ns": delay}
        for target, delay in sorted(delays_by_target.items())
    ]


def test_forward_sweep_tracks_the_calibrated_row() -> None:
    result = classify_forward_sweep(
        _sweep({8: 32.0, 9: 36.0, 10: 40.0, 11: 44.0, 12: 48.0}),
        half_period_ns=50.0)
    assert result["branch"] == "A"
    assert result["target_ceiling_cycles"] == 12
    assert result["ceiling_phase_ns"] == 48
    assert result["ceiling_margin_ns"] == 2
    assert all(point["tracked"] in (None, True) for point in result["points"])


def test_forward_sweep_saturates_above_ten_cycles() -> None:
    result = classify_forward_sweep(
        _sweep({8: 32.0, 9: 36.0, 10: 40.0, 11: 40.0, 12: 40.0}))
    assert result["branch"] == "B"
    assert result["target_ceiling_cycles"] == 10
    assert result["tracked_targets"] == [8, 9, 10]
    assert result["points"][-1]["tracked"] is False


def test_forward_sweep_jump_is_not_tracking() -> None:
    result = classify_forward_sweep(
        _sweep({8: 32.0, 9: 36.0, 10: 40.0, 11: 48.0}))
    assert result["branch"] == "B"
    assert result["target_ceiling_cycles"] == 10


def test_forward_sweep_below_ten_cycles_needs_retime() -> None:
    result = classify_forward_sweep(
        _sweep({8: 32.0, 9: 36.0, 10: 36.0, 11: 36.0}))
    assert result["branch"] == "C"
    assert result["target_ceiling_cycles"] == 9


def test_forward_sweep_rejects_incomplete_input() -> None:
    for points in ([], _sweep({10: 40.0})):
        try:
            classify_forward_sweep(points)
        except ValueError as exc:
            assert "two target points" in str(exc)
        else:
            raise AssertionError("a single point cannot classify a sweep")
    duplicated = _sweep({10: 40.0, 11: 44.0}) + [
        {"target_cycles": 11, "rise_delay_ns": 44.0, "fall_delay_ns": 44.0}]
    try:
        classify_forward_sweep(duplicated)
    except ValueError as exc:
        assert "unique" in str(exc)
    else:
        raise AssertionError("duplicate targets must be rejected")

def _forward_capture(length: int, rise_at: int, fall_at: int,
                     rise_delay: int, fall_delay: int
                     ) -> tuple[list[int], list[int]]:
    rx = [0] * length
    tx = [0] * length
    for index in range(rise_at, fall_at):
        rx[index] = 1
    for index in range(rise_at + rise_delay, fall_at + fall_delay):
        tx[index] = 1
    return rx, tx


def test_forward_capture_measures_symmetric_edges() -> None:
    rx, tx = _forward_capture(40, 2, 14, 11, 11)
    measured = measure_forward_scK(rx, tx, bit_period_ns=100.0)
    assert measured["rise_delay_ns"] == 44
    assert measured["fall_delay_ns"] == 44
    assert measured["edge_skew_ns"] == 0
    assert measured["high_ns"] == 48
    assert measured["duty_percent"] == 48.0
    assert measured["rx_edges"] == 2
    assert measured["tx_edges"] == 2


def test_forward_capture_reports_duty_skew() -> None:
    rx, tx = _forward_capture(40, 2, 14, 11, 10)
    measured = measure_forward_scK(rx, tx, bit_period_ns=100.0)
    assert measured["rise_delay_ns"] == 44
    assert measured["fall_delay_ns"] == 40
    assert measured["edge_skew_ns"] == -4
    assert measured["high_ns"] == 44
    assert measured["duty_percent"] == 44.0


def test_forward_capture_feeds_the_sweep_classifier() -> None:
    points = []
    for target, delay in ((8, 8), (9, 9), (10, 10), (11, 10)):
        rx, tx = _forward_capture(60, 2, 15, delay, delay)
        measured = measure_forward_scK(rx, tx)
        points.append({"target_cycles": target,
                       "rise_delay_ns": measured["rise_delay_ns"],
                       "fall_delay_ns": measured["fall_delay_ns"]})
    result = classify_forward_sweep(points)
    assert result["branch"] == "B"
    assert result["target_ceiling_cycles"] == 10


def test_forward_capture_rejects_inverted_or_incomplete_probe() -> None:
    rx, tx = _forward_capture(40, 2, 14, 11, 11)
    inverted = [1 - level for level in tx]
    try:
        measure_forward_scK(rx, inverted)
    except ValueError as exc:
        assert "precedes" in str(exc) or "missing" in str(exc)
    else:
        raise AssertionError("an inverted probe must not measure")
    truncated = [0] * 40
    for index in range(13, 25):
        truncated[index] = 1
    truncated[25:] = [0] * 15
    try:
        measure_forward_scK(rx, truncated[:26])
    except ValueError as exc:
        assert "same capture length" in str(exc)
    else:
        raise AssertionError("mismatched capture length must be rejected")


def test_forward_capture_validates_levels_and_period() -> None:
    rx, tx = _forward_capture(40, 2, 14, 11, 11)
    dirty = list(rx)
    dirty[3] = 2
    try:
        measure_forward_scK(dirty, tx)
    except ValueError as exc:
        assert "0 or 1" in str(exc)
    else:
        raise AssertionError("non-binary levels must be rejected")
    assert "duty_percent" not in measure_forward_scK(rx, tx)

def _window(passing: set[int], measured: set[int], repeats: int = 8
            ) -> list[dict[str, object]]:
    return [
        {"offset_sample_count": offset,
         "accepted_count": repeats if offset in passing else 0,
         "repeats": repeats}
        for offset in sorted(measured)
    ]


def test_offset_window_reports_centred_usable_eye() -> None:
    result = classify_offset_window(
        _window({0, 1, 2}, {-1, 0, 1, 2, 3}))
    assert result["verdict"] == "usable"
    assert (result["eye_left_sample_count"],
            result["eye_right_sample_count"]) == (0, 2)
    assert result["width_samples"] == 3
    assert result["center_sample_count"] == 1.0
    assert result["center_representable"] is True
    assert result["chosen_offset_sample_count"] == 1
    assert result["chosen_margin_samples"] == 1
    assert result["coverage_complete"] is True


def test_offset_window_even_width_has_no_representable_centre() -> None:
    result = classify_offset_window(
        _window({1, 2}, {0, 1, 2, 3}))
    assert result["verdict"] == "usable"
    assert result["width_samples"] == 2
    assert result["center_sample_count"] == 1.5
    assert result["center_representable"] is False
    assert result["chosen_margin_samples"] == 0


def test_offset_window_flags_narrow_and_empty_eyes() -> None:
    narrow = classify_offset_window(_window({1}, {0, 1, 2}))
    assert narrow["verdict"] == "too_narrow"
    assert narrow["width_samples"] == 1
    empty = classify_offset_window(_window(set(), {0, 1, 2}))
    assert empty["verdict"] == "no_window"
    assert empty["chosen_offset_sample_count"] is None


def test_offset_window_rejects_disjoint_passing_regions() -> None:
    result = classify_offset_window(_window({0, 1, 3}, {0, 1, 2, 3, 4}))
    assert result["verdict"] == "non_contiguous"
    assert result["window_runs"] == [[0, 1], [3]]
    # The widest run still anchors the placement, but the verdict blocks it.
    assert result["eye_left_sample_count"] == 0
    assert result["width_samples"] == 2


def test_offset_window_requires_failing_neighbours() -> None:
    result = classify_offset_window(_window({0, 1, 2}, {0, 1, 2}))
    assert result["verdict"] == "coverage_incomplete"
    assert result["coverage_complete"] is False
    assert result["width_samples"] == 3


def test_offset_window_validates_input() -> None:
    for points in ([],):
        try:
            classify_offset_window(points)
        except ValueError as exc:
            assert "at least one" in str(exc)
        else:
            raise AssertionError("an empty sweep must be rejected")
    duplicated = _window({0}, {0}) + _window({0}, {0})
    try:
        classify_offset_window(duplicated)
    except ValueError as exc:
        assert "unique" in str(exc)
    else:
        raise AssertionError("duplicate offsets must be rejected")
    bad = [{"offset_sample_count": 0, "accepted_count": 9, "repeats": 8}]
    try:
        classify_offset_window(bad)
    except ValueError as exc:
        assert "invalid accepted" in str(exc)
    else:
        raise AssertionError("accepted > repeats must be rejected")