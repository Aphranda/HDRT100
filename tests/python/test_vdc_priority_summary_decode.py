"""Independent summary wire fixtures, loss flags and CRC-valid corruptions."""
import struct
import zlib

import pytest

from tools.vdc_priority_trace import vdc_priority_trace as trace
from test_vdc_priority_summary import summary_executable, summary_origin_executable  # noqa: F401
from test_vdc_priority_trace import trace_executable, execute  # noqa: F401
from test_vdc_priority_origin_trace import origin_executable  # noqa: F401


def record(index=0, start=100, end=250000100, origin=False):
    # Positional wire fixture deliberately independent of decoder field names.
    return [index, 0, start, end, 250000, 25000000, 1+index*2, 2+index*2,
            100, 200, 1000, 2, 0, 0, 0, 0 if origin else 1, 0, 0,
            0 if origin else -100, 0 if origin else 180, 172, 3, 3, -20, 40, 0, 0]


def native(rows=None, schema=7, overrides=None):
    rows = [record(origin=schema in (8, 10))] if rows is None else rows
    words = [schema, 2, 2, 2, 3, 1, 123, 42, 42, 1000, 76, len(rows),
             sum(r[11] for r in rows), sum(r[15] for r in rows), 0, 0,
             1000, 61000, 61001, 9, 9, 1, 0, 4, 11, 12, 13, 14, 80,
             1, 1, 8, 0, 3, 4, 250000000, 4]
    for index, value in (overrides or {}).items():
        words[index] = value
    payload = b''.join(struct.pack('<HHQQIIIIIIHHHHHHHHqqIIIiiHH', *r) for r in rows)
    return struct.pack('<42I', 0x52545056, schema, 168, 100, zlib.crc32(payload), *words)+payload


@pytest.mark.parametrize('schema', [7, 8])
def test_sixty_bins_cross_timer_low_word_and_uptime_rollover(schema):
    start = (1 << 32)-100
    rows = [record(i, start+i*250000000, start+(i+1)*250000000, schema == 8) for i in range(60)]
    decoded = trace.decode(native(rows, schema, {16: 0xfffffff0, 17: 59984}), 123)
    assert len(decoded['records']) == 60
    assert decoded['records'][0]['observed_start_raw'] < 1 << 32
    assert decoded['records'][0]['observed_end_raw'] > 1 << 32
    assert decoded['status']['match_count'] == 120
    assert decoded['records'][0]['residual_extrema_valid'] == (schema == 7)
    assert not decoded['complete_window_proven'] and not decoded['physical_lock_qualified']


def test_empty_service_gap_and_zero_length_terminal_remain_explicit():
    first = record()
    gap = [1, 2|4, 250000100, 1000000100, 750000000, 999999900,
           0, 0, 0, 0, 1, 0, 2, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1|4]
    tail = [2, 1|2|256, 1000000100, 1000000100, 0, 999999900,
            0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    decoded = trace.decode(native([first, gap, tail]))
    assert decoded['records'][1]['coverage_incomplete']
    assert not decoded['records'][1]['success_extrema_valid']
    assert decoded['records'][1]['max_success_gap_ticks'] > 750000000
    assert decoded['records'][2]['flag_names'] == ['PARTIAL', 'NO_SUCCESS', 'TERMINAL']


@pytest.mark.parametrize('flag', [8, 16, 64])
def test_source_counter_uncertainty_and_clock_failure_are_not_discarded(flag):
    row = record()
    row[1] = flag
    assert trace.decode(native([row]))['records'][0]['coverage_incomplete']


def test_exact_maximum_is_not_overflow_but_flagged_overflow_is_retained():
    row = record()
    row[10] = 65535
    assert not trace.decode(native([row]))['records'][0]['coverage_incomplete']
    row[1] = 32
    assert trace.decode(native([row]))['records'][0]['coverage_incomplete']


def test_failed_counter_observation_does_not_require_large_time_gap_or_reset():
    row = record(origin=True)
    row[1] = 4
    decoded = trace.decode(native([row], 8))['records'][0]
    assert decoded['coverage_incomplete']
    assert decoded['max_service_gap_ticks'] < 250000000
    assert decoded['rejected_count'] == 0
    assert decoded['flag_names'] == ['SERVICE_GAP']


@pytest.mark.parametrize('field,value,message', [
    (0, 1, 'bin index'), (1, 512, 'Unknown summary'), (1, 2, 'success flag'),
    (1, 32, 'saturation'), (1, 128, 'unbound'), (1, 256, 'terminal'),
    (2, 250000101, 'Reversed'), (3, 200, 'Short nonpartial'),
    (4, 250000000, 'service gap'), (6, 0, 'identity'),
    (8, 201, 'offset'), (9, 250000001, 'offset'),
    (11, 3, 'sequence span'), (12, 1, 'outcome counters'),
    (13, 1, 'outcome counters'), (16, 2, 'exceeds decisions'),
    (18, 181, 'extrema'), (21, 0, 'identity'),
    (23, 41, 'extrema'), (23, -1000000000, 'extrema'),
    (25, 3, 'model changes'), (26, 64, 'Unknown summary'),
])
def test_reject_crc_valid_impossible_summary(field, value, message):
    row = record()
    row[field] = value
    with pytest.raises(ValueError, match=message):
        trace.decode(native([row]))


@pytest.mark.parametrize('field', [13, 14, 15, 16, 17, 18, 19])
def test_origin_rejects_follower_only_fields(field):
    row = record(origin=True)
    row[field] = 1
    if field == 13:
        row[26] = 4
    if field == 16:
        row[15] = 1
    with pytest.raises(ValueError, match='Origin summary'):
        trace.decode(native([row], 8))


def test_origin_rejection_counts_and_empty_unbound_capture():
    row = record(origin=True)
    row[12], row[26] = 3, 32
    assert trace.decode(native([row], 8))['records'][0]['outcome_names'] == ['TX_REJECT']
    empty = [0, 1|2|128|256, 100, 101, 0, 1]+[0]*21
    assert not trace.decode(native([empty]))['records'][0]['residual_extrema_valid']
    empty[18] = 10
    with pytest.raises(ValueError, match='Empty summary'):
        trace.decode(native([empty]))


def test_order_terminal_and_event_rollback():
    one, two = record(), record(1, 250000100, 500000100)
    two[2] += 1
    with pytest.raises(ValueError, match='Discontinuous'):
        trace.decode(native([one, two]))
    two[2] -= 1
    two[6:8] = [1, 2]
    with pytest.raises(ValueError, match='event rollback'):
        trace.decode(native([one, two]))
    one[1] = 1|256
    with pytest.raises(ValueError, match='terminal'):
        trace.decode(native([one, two]))


def test_clock_invalid_cannot_excuse_overlapping_summary_bins():
    one = record(end=1050000000)
    one[1] = 4|64
    two = record(1, 1025000000, 1275000000)
    two[1] = 1|64|256
    with pytest.raises(ValueError, match='Discontinuous summary observation time'):
        trace.decode(native([one, two]))


@pytest.mark.parametrize('index,value,message', [
    (0, 8, 'identity'), (2, 3, 'acknowledged'), (7, 0, 'binding identity'),
    (9, 200, 'interval'), (10, 75, 'capacity'), (12, 3, 'totals'),
    (13, 0, 'totals'), (14, 1, 'skipped'), (35, 0, 'clock'),
])
def test_summary_header_semantics(index, value, message):
    with pytest.raises(ValueError, match=message):
        trace.decode(native(overrides={index: value}), 123)


def test_crc_length_capture_identity_and_old_schema_relabelling():
    data = native()
    with pytest.raises(ValueError, match='Capture ID'):
        trace.decode(data, 124)
    with pytest.raises(ValueError, match='payload'):
        trace.decode(data[:-1])
    corrupt = bytearray(data)
    corrupt[-1] ^= 1
    with pytest.raises(ValueError, match='CRC'):
        trace.decode(bytes(corrupt))
    with pytest.raises(ValueError, match='Record counts'):
        trace.decode(native(schema=6))
    with pytest.raises(ValueError, match='Unknown native'):
        trace.decode(native(schema=11))


@pytest.mark.parametrize('schema', [9, 10])
@pytest.mark.parametrize('interval_ms', range(1000, 10001, 1000))
def test_explicit_intervals_keep_record_layout_and_origin_semantics(schema, interval_ms):
    ticks = 250000000 * interval_ms // 1000
    row = record(end=100+ticks, origin=schema == 10)
    data = native([row], schema, {9: interval_ms})
    result = trace.decode(data, 123)
    assert len(data) == 168+100
    assert result['status']['schema'] == schema
    assert result['status']['sample_interval_ms'] == interval_ms
    assert result['records'][0]['flag_names'] == []
    assert result['records'][0]['residual_extrema_valid'] == (schema == 9)


@pytest.mark.parametrize('schema', [7, 8])
def test_legacy_schema_cannot_be_relabelled_as_ten_second_summary(schema):
    with pytest.raises(ValueError, match='legacy summary interval'):
        trace.decode(native(schema=schema, overrides={9: 10000}))


@pytest.mark.parametrize('interval_ms', [0, 999, 1001, 1500, 9999, 10001, 11000])
@pytest.mark.parametrize('schema', [9, 10])
def test_explicit_summary_rejects_invalid_interval(interval_ms, schema):
    with pytest.raises(ValueError, match='Summary interval'):
        trace.decode(native(schema=schema, overrides={9: interval_ms}))


def test_explicit_interval_ticks_must_fit_uint32_even_without_records():
    for rows in ([], [record()]):
        with pytest.raises(ValueError, match='interval ticks'):
            trace.decode(native(rows, 9, {9: 10000, 35: 500000000}))
    ticks = 429496729 * 10
    row = record(end=100+ticks)
    assert trace.decode(native([row], 9, {9: 10000, 35: 429496729}))['records']


@pytest.mark.parametrize('schema', [9, 10])
def test_ten_second_interval_retains_one_second_service_gap_threshold(schema):
    row = record(end=2500000100, origin=schema == 10)
    row[4] = 250000000-1
    assert not trace.decode(native([row], schema, {9: 10000}))['records'][0]['coverage_incomplete']
    row[4] += 1
    with pytest.raises(ValueError, match='service gap'):
        trace.decode(native([row], schema, {9: 10000}))
    row[1] = 4
    assert trace.decode(native([row], schema, {9: 10000}))['records'][0]['coverage_incomplete']


def test_interval_span_gap_boundary_and_partial_tail():
    row = record(end=2750000099)
    assert trace.decode(native([row], 9, {9: 10000}))['records']
    row[3] += 1
    with pytest.raises(ValueError, match='service gap'):
        trace.decode(native([row], 9, {9: 10000}))
    row[1] = 4
    assert trace.decode(native([row], 9, {9: 10000}))['records'][0]['coverage_incomplete']
    row[1], row[3] = 1 | 256, 1000
    assert trace.decode(native([row], 9, {9: 10000}))['records'][0]['flag_names'] == ['PARTIAL', 'TERMINAL']
    row[1] = 0
    with pytest.raises(ValueError, match='Short nonpartial'):
        trace.decode(native([row], 9, {9: 10000}))


@pytest.mark.parametrize('schema', [9, 10])
def test_extended_summary_retains_saturation_and_rejects_invented_flag(schema):
    row = record(end=2500000100, origin=schema == 10)
    row[1] = 32
    with pytest.raises(ValueError, match='saturation'):
        trace.decode(native([row], schema, {9: 10000}))
    row[5] = 0xffffffff
    decoded = trace.decode(native([row], schema, {9: 10000}))['records'][0]
    assert decoded['coverage_incomplete'] and 'FIELD_SATURATED' in decoded['flag_names']
    assert decoded['max_success_gap_ticks'] == 0xffffffff


@pytest.mark.parametrize('schema', [9, 10])
def test_six_hundred_seconds_with_raw_wraps_keep_sixty_bounded_bins(schema):
    ticks, start = 2500000000, (1 << 32)-100
    rows = [record(i, start+i*ticks, start+(i+1)*ticks, schema == 10) for i in range(60)]
    result = trace.decode(native(rows, schema, {9: 10000}))
    assert len(result['records']) == 60
    assert result['records'][-1]['observed_end_raw']-start == 600*250000000
    assert not any(row['coverage_incomplete'] for row in result['records'])


@pytest.mark.parametrize('field', [13, 14, 15, 16, 17, 18, 19])
def test_extended_origin_rejects_follower_fields(field):
    row = record(end=2500000100, origin=True)
    row[field] = 1
    if field == 13:
        row[26] = 4
    if field == 16:
        row[15] = 1
    with pytest.raises(ValueError, match='Origin summary'):
        trace.decode(native([row], 10, {9: 10000}))


@pytest.mark.parametrize('case', ['empty', 'tail', 'gap', 'success', 'reject', 'counter',
    'session', 'capacity', 'raw_rollback', 'uptime_wrap', 'held', 'cancel', 'stop_gap',
    'sixty', 'ownership', 'clock_epoch', 'clock_boundary', 'clock_success', 'clock_initial_invalid'])
def test_decode_actual_producer_bins(summary_executable, case):
    decoded = trace.decode(execute(summary_executable, case), 1)
    assert decoded['status']['schema'] == 7
    assert not decoded['physical_lock_qualified'] and not decoded['complete_window_proven']
    if case == 'sixty':
        assert len(decoded['records']) == 61
        assert decoded['status']['match_count'] == 600
        assert decoded['records'][-1]['observed_end_raw']-decoded['records'][0]['observed_start_raw'] == 15000000000
    if case == 'clock_epoch':
        assert decoded['status']['reason'] == 3
        assert decoded['records'][-1]['coverage_incomplete']
    if case in ('clock_boundary', 'clock_success'):
        assert decoded['status']['reason'] == 3 and len(decoded['records']) == 1
        row = decoded['records'][0]
        assert row['observed_end_raw'] == 1050000000
        assert row['coverage_incomplete'] and row['success_count'] == 0
        assert 'CLOCK_INVALID' in row['flag_names'] and 'TERMINAL' in row['flag_names']
    if case == 'clock_initial_invalid':
        assert decoded['status']['reason'] == 3 and decoded['records'] == []


@pytest.mark.parametrize('case', ['origin_flow', 'origin_reject', 'origin_binding', 'origin_busy'])
def test_decode_actual_origin_bins(summary_origin_executable, case):
    decoded = trace.decode(execute(summary_origin_executable, case), 1)
    assert decoded['status']['schema'] == 8
    assert not any(r['residual_extrema_valid'] for r in decoded['records'])
    if case == 'origin_busy':
        row = decoded['records'][-1]
        assert row['coverage_incomplete'] and 'SERVICE_GAP' in row['flag_names']
        assert row['rejected_count'] == 0 and 'COUNTER_RESET' not in row['flag_names']
