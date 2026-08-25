from tools.calibration_ring_validate.calibration_clk_codebook_eval import (
    CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20,
    CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_24,
    CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32,
    CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
    crc8_atm,
    encode,
    evaluate,
    first_primitive_mask,
    galois_period,
    marker_header,
    marker_raw_waveform,
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


def test_candidate_marker_golden_vector():
    raw, vector = marker_raw_waveform(
        version=CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        codebook_id=CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_20,
        epoch=0x5A,
        source_node=3,
        polarity=0,
    )
    assert marker_header(0, 0, 0x5A, 3, 0) == 0x05A6
    assert vector.header == 0x05A6
    assert vector.header_inverse == 0xFA59
    assert vector.header_crc8 == crc8_atm(bytes.fromhex("05 A6 FA 59"))
    assert vector.logical_bits == 321
    assert vector.raw_samples == 3210
    assert vector.raw_words == 101
    assert vector.timing_origin_sample == 530
    assert vector.timing_samples == 2550
    assert len(raw) == vector.raw_samples
    assert vector.raw_sample_fnv1a32 == 0xD89E1248


def test_candidate_marker_fallback_keeps_raw_sample_domain():
    fast, fast_vector = marker_raw_waveform(codebook_id=0)
    robust, robust_vector = marker_raw_waveform(codebook_id=1)
    assert robust_vector.half_chip_samples == 2 * fast_vector.half_chip_samples
    assert len(robust) == 2 * len(fast)
    assert robust_vector.logical_bits == fast_vector.logical_bits


def test_candidate_marker_intermediate_half_chips_fit_header_codebook_bits():
    raw24, vector24 = marker_raw_waveform(
        codebook_id=CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_24)
    raw32, vector32 = marker_raw_waveform(
        codebook_id=CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32)
    assert vector24.half_chip_samples == 6
    assert vector32.half_chip_samples == 8
    assert len(raw24) == 321 * 12
    assert len(raw32) == 321 * 16
    assert (vector24.header >> 12) & 0x3 == 2
    assert (vector32.header >> 12) & 0x3 == 3
