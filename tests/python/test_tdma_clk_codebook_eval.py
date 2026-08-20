from tools.tdma_ring_monitor.tdma_clk_codebook_eval import (
    encode,
    evaluate,
    first_primitive_mask,
    galois_period,
    msequence,
    run_lengths,
)


def test_maximal_sequences_have_expected_period():
    for width in (5, 6, 7, 8):
        mask = first_primitive_mask(width)
        assert galois_period(width, mask) == (1 << width) - 1
        bits, selected = msequence(width, mask)
        assert selected == mask
        assert len(bits) == (1 << width) - 1


def test_manchester_run_length_is_bounded():
    bits, _ = msequence(7)
    waveform = encode(bits, "manchester", 5)
    lengths = run_lengths(waveform)
    assert min(lengths) == 5
    assert max(lengths) == 10


def test_manchester_sharpens_adjacent_sample_discrimination():
    bits, mask = msequence(7)
    nrz = evaluate("mseq127", bits, "nrz", 4, 5, 52, 7, mask)
    manchester = evaluate(
        "mseq127", bits, "manchester", 4, 5, 52, 7, mask)
    assert manchester.sample_ns == 4
    assert manchester.half_chip_ns == 20
    assert manchester.adjacent_shift_distance > nrz.adjacent_shift_distance
    assert manchester.min_wrong_lag_distance > nrz.min_wrong_lag_distance


def test_lag_search_reports_nonzero_margin():
    bits, mask = msequence(6)
    result = evaluate(
        "mseq63", bits, "differential_manchester", 4, 10, 52, 6, mask)
    assert result.search_radius_samples == 13
    assert result.min_wrong_lag_distance > 0
    assert abs(result.min_wrong_lag_samples) <= 13
