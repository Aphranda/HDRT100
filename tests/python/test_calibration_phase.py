from tools.calibration_ring_validate.calibration_phase import (
    OFFSET_MATRIX_SCHEMA,
    PHASE_TRAINING_SCHEMA,
    PHASE_TRAINING_STAGES,
    build_observed_offset_matrix,
    build_offset_rows,
    build_phase_training_contract,
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
        "round((link_delay_ns / 2) / sample_period_ns) + node_offset_samples"}
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
