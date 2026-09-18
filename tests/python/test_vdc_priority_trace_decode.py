"""Independent native byte fixtures and corrupted/stale STOP-read transfers."""
import struct
import zlib

import pytest

from tools.vdc_priority_trace import vdc_priority_trace as trace


def native():
    words = [1, 2, 2, 2, 3, 1, 123, 42, 42, 200, 76, 3, 1, 2, 5, 0,
             1000, 1500, 1510, 9, 9, 1, 0, 4, 11, 12, 13, 14, 80, 1, 1, 8, 0, 3, 4, 250000000, 4]
    # Common record fields and bodies are built without decoder field mappings.
    match = struct.pack('<5IQQqqQQQQIIiI', 0, 1, 1000, 10, 11,
                        100, 102, 17, 30, 2000, 2010, 1900, 1903, 2, 4, 500, 0)
    applied = struct.pack('<5I4I3iI2q4Q', 1, 2, 1400, 20, 21,
                          10, 2, 4, 5, 500, 250, -250, 0, 1000, 1000,
                          1000000000, 1000000000, 1000001000, 1000001000)
    held = struct.pack('<5I4I3iI2q4Q', 2, 2, 1500, 30, 31,
                       20, 3, 5, 5, 250, 250, 0, 11, -100, 100,
                       1000000000, 1000000000, 999999900, 1000000100)
    payload = match + applied + held
    return struct.pack('<42I', 0x52545056, 1, 168, 100, zlib.crc32(payload), *words) + payload


def fix_crc(data):
    data = bytearray(data)
    struct.pack_into('<I', data, 16, zlib.crc32(data[168:]))
    return bytes(data)


def test_native_intervals_actual_apply_and_hold():
    result = trace.decode(native(), 123)
    assert result['status']['sample_interval_ms'] == 200
    assert result['records'][0]['residual_lo'] == 17
    assert result['records'][0]['residual_hi'] == 30
    assert result['records'][1]['outcome'] == 'applied'
    assert result['records'][1]['after_ppb'] == 250
    assert result['records'][2]['outcome'] == 'no_adjust'
    assert not result['physical_lock_qualified'] and not result['complete_window_proven']


def phase_native():
    """Two actual opposite phase steps; bytes independent of decoder names."""
    words = list(struct.unpack_from('<37I', native(), 20))
    words[0] = 4
    words[11:14] = [2, 0, 2]
    first = struct.pack('<5I6I4q3Q', 0, 4, 1000, 10, 11,
                        2, 3, 4, 5, 1, 0, 80, 80, -100, -80, 1000, 1080, 100)
    second = struct.pack('<5I6I4q3Q', 1, 4, 1200, 20, 21,
                         3, 4, 5, 6, 1, 0, -20, 60, 20, 40, 1080, 1060, 200)
    payload = first + second
    return struct.pack('<42I', 0x52545056, 4, 168, 100, zlib.crc32(payload), *words) + payload


def test_phase_records_preserve_actual_translation_and_ledger():
    result = trace.decode(phase_native(), 123)
    assert result['schema'] == 'VDC_TYPED_TRACE_DECODE_V4'
    assert [r['delta_ns'] for r in result['records']] == [80, -20]
    assert [r['outcome'] for r in result['records']] == ['phase_applied'] * 2
    assert result['records'][-1]['cumulative_ns'] == 60
    assert not result['complete_window_proven'] and not result['physical_lock_qualified']
    data, _ = trace.download_capture(query_for(phase_native()), 123)
    assert data == phase_native()


def midpoint_native(lo, hi, delta, schema=6):
    words = list(struct.unpack_from('<37I', native(), 20))
    words[0] = schema
    words[11:14] = [1, 0, 1]
    base = 2000000000
    payload = struct.pack('<5I6I4q3Q', 0, 4, 1000, 10, 11,
                          2, 3, 4, 5, 1, 0, delta, delta, lo, hi, base, base+delta, 100)
    return struct.pack('<42I', 0x52545056, schema, 168, 100, zlib.crc32(payload), *words)+payload


@pytest.mark.parametrize('lo,hi,delta', [(-8,200,-96),(-200,8,96),(0,192,-96),
    (-192,0,96),(-5,-2,3),(2,5,-3),(-(1<<63),-(1<<63),1000000000),
    ((1<<63)-1,(1<<63)-1,-1000000000)])
def test_midpoint_versioned_control_and_ram_transfer(lo, hi, delta):
    raw = midpoint_native(lo, hi, delta)
    decoded = trace.decode(raw, 123)
    assert decoded['records'][0]['delta_ns'] == delta
    assert trace.download_capture(query_for(raw), 123)[0] == raw


@pytest.mark.parametrize('lo,hi,delta', [(-8,200,96),(-8,200,-95),(0,192,-1),
    (-192,0,1),(-5,-2,4),(-100,100,1)])
def test_midpoint_rejects_crc_valid_wrong_estimate(lo, hi, delta):
    with pytest.raises(ValueError, match='midpoint'):
        trace.decode(midpoint_native(lo, hi, delta), 123)


def test_midpoint_cannot_be_relabelled_as_historical_phase():
    with pytest.raises(ValueError, match='direction'):
        trace.decode(midpoint_native(-8, 200, -96, schema=4), 123)
    old = bytearray(phase_native())
    struct.pack_into('<I', old, 4, 6)
    struct.pack_into('<I', old, 20, 6)
    with pytest.raises(ValueError, match='midpoint'):
        trace.decode(old, 123)


@pytest.mark.parametrize('offset,fmt,value,reason', [
    (168+24, 'I', 2, 'identity'), (168+32, 'I', 4, 'identity'),
    (168+36, 'I', 0, 'identity'), (168+40, 'I', 1, 'identity'),
    (168+44, 'q', 79, 'translation'), (168+84, 'Q', 1081, 'translation'),
    (168+60, 'q', -50, 'Reversed'), (168+68, 'q', 0, 'direction'),
    (168+68, 'q', -79, 'overshoots'), (268+52, 'q', 61, 'ledger'),
])
def test_phase_rejects_crc_valid_false_model_or_control_evidence(offset, fmt, value, reason):
    data = bytearray(phase_native())
    struct.pack_into('<' + fmt, data, offset, value)
    with pytest.raises(ValueError, match=reason):
        trace.decode(fix_crc(data), 123)


def test_phase_kind_cannot_silently_enter_legacy_follower_schema():
    data = bytearray(phase_native())
    struct.pack_into('<I', data, 4, 1)
    struct.pack_into('<I', data, 20, 1)
    with pytest.raises(ValueError, match='kind'):
        trace.decode(data, 123)


@pytest.mark.parametrize('before,after,fmt', [(20,24,'I'), (28,32,'I'), (76,84,'Q')])
def test_phase_same_epoch_rejects_locally_valid_but_unexplained_model_jump(before, after, fmt):
    data = bytearray(phase_native())
    for offset in (before, after):
        value = struct.unpack_from('<'+fmt, data, 268+offset)[0]
        struct.pack_into('<'+fmt, data, 268+offset, value+10)
    with pytest.raises(ValueError, match='Unexplained model'):
        trace.decode(fix_crc(data), 123)


@pytest.mark.parametrize('offset,value,reason', [
    (0, 0, 'format'), (4, 99, 'format'), (8, 164, 'format'), (12, 96, 'format'),
    (20 + 2*4, 4, 'acknowledged'), (20 + 4*4, 2, 'acknowledged'),
    (20 + 6*4, 124, 'ID'), (20 + 10*4, 77, 'capacity'),
    (20 + 12*4, 2, 'counts'), (168, 1, 'index'), (172, 9, 'kind'),
    (168 + 36, 18, 'Residual'), (268 + 32, 6, 'DCO'),
    (268 + 40, 249, 'DCO'), (268 + 52, 999, 'arithmetic'),
])
def test_reject_incoherent_even_with_valid_payload_crc(offset, value, reason):
    data = bytearray(native())
    struct.pack_into('<I', data, offset, value)
    with pytest.raises(ValueError, match=reason):
        trace.decode(fix_crc(data), 123)


@pytest.mark.parametrize('data', [b'', native()[:167], native()[:-1], native()+b'\0', native()[:-1]+b'\xff'])
def test_reject_truncation_trailing_bytes_and_payload_crc(data):
    with pytest.raises(ValueError):
        trace.decode(data, 123)


def query_for(data, transform=lambda raw, offset: raw):
    def query(command):
        header, args = command.split(' ', 1)
        assert header == 'SYSTem:VDC:PRIORity:TRACe:READ?'
        capture_id, offset, size = map(int, args.split(','))
        assert capture_id == 123 and 0 < size <= 128
        raw = f'{offset},{size},{len(data)},{zlib.crc32(data)},"{data[offset:offset+size].hex()}"'
        return transform(raw, offset)
    return query


@pytest.mark.parametrize('page_size', [1, 7, 64, 128])
def test_cross_header_record_boundaries_and_full_file_crc(page_size):
    data, pages = trace.download_capture(query_for(native()), 123, page_size)
    assert data == native() and pages[0]['command'].endswith('123,0,4')
    assert len(pages) > 1


def test_reject_stale_page_and_mid_download_metadata_change():
    stale = lambda raw, offset: '0,' + raw.split(',', 1)[1] if offset else raw
    def changed(raw, offset):
        fields = raw.split(',')
        if offset:
            fields[3] = str(int(fields[3]) ^ 1)
        return ','.join(fields)
    for transform in (stale, changed):
        with pytest.raises(ValueError):
            trace.download_capture(query_for(native(), transform), 123)


def test_reject_corrupted_page_with_stable_advertised_crc():
    def corrupt(raw, offset):
        if offset == 4:
            raw = raw[:-3] + ('ff' if raw[-3:-1] != 'ff' else '00') + '"'
        return raw
    with pytest.raises(ValueError, match='Whole-file'):
        trace.download_capture(query_for(native(), corrupt), 123)


@pytest.mark.parametrize('raw', ['<timeout>', '0,1,168,1,"zz"', '1,4,168,1,"00000000"',
                                '0,4,168,-1,"00000000"', '0,4,168,1,"00"'])
def test_bad_page_responses(raw):
    with pytest.raises(ValueError):
        trace.parse_page(raw, 0, 4)


def test_bad_request_does_not_query():
    for capture_id, size in [(0, 128), (2**32, 128), (123, 0), (123, 129)]:
        with pytest.raises(ValueError):
            trace.download_capture(lambda q: pytest.fail('invalid request sent'), capture_id, size)


def origin_native(schema, count):
    """Independent integer-clock oracle; public bytes, no decoder field maps.

    The ninth and 64th bridges tighten the retained interval. Bridge 65
    tightens just its own event after the schema 3 pool is full; event 66
    proves that this additional constraint was not retained.
    """
    capacity = {2: 8, 3: 64}[schema]
    hz = 250_000_000
    retained, records = [], []
    first_local = 10_000_000_000
    anchor_raw = first_local // 4
    for index in range(count):
        local = first_local + index * 1_000_000
        raw = local // 4
        offset = {8: 150, 63: 175, 64: 200}.get(index, 100)
        before, after = raw + offset, raw + offset + 1
        constraint = (local - 4 * after, local + 1000 - 4 * before)
        low = max(pair[0] for pair in retained + [constraint])
        high = min(pair[1] for pair in retained + [constraint])
        encoded = 4 * raw + low
        width = 4 * (raw + 1) + high - 1 - encoded
        records.append(struct.pack('<5I7QiiIQI', index, 3, index + 1, 7, hz,
            raw, raw + 1, before, after, local, 0, 0, 0, 0, 5, encoded, width))
        if len(retained) < capacity:
            retained.append(constraint)
    words = [schema, 2, 2, 2, 3, 4 if count == 76 else 1, 123, 42, 42, 0,
             76, count, 0, 0, 0, 0, 1000, 1100, 1110, 9, 9, 0, 0, 4,
             11, 12, 13, 14, 0, 1, 1, 8, 0, 3, 4, hz, 4]
    extension = bytes(96)
    if count:
        last_offset = {8: 150, 63: 175, 64: 200}.get(count - 1, 100)
        last_local = first_local + (count - 1) * 1_000_000
        extension = struct.pack('<12I4Q2q', 1, 1, 1, count, 2, 2 * count,
            count, 1 if count == 1 else 0, len(retained), 1, hz, 7,
            anchor_raw, first_local, last_local // 4 + last_offset + 1, last_local,
            max(pair[0] for pair in retained) * hz,
            min(pair[1] for pair in retained) * hz)
    payload = b''.join(records)
    return struct.pack('<42I', 0x52545056, schema, 264, 100,
                       zlib.crc32(payload), *words) + extension + payload


@pytest.mark.parametrize('schema,capacity', [(2, 8), (3, 64)])
@pytest.mark.parametrize('count', [0, 1, 7, 8, 9, 10, 63, 64, 65, 66, 76])
def test_origin_versioned_retention_boundaries_and_frozen_full(schema, capacity, count):
    raw = origin_native(schema, count)
    result = trace.decode(raw, 123)
    assert result['schema'] == f'VDC_ORIGIN_TRACE_DECODE_V{schema}'
    assert result['mapping_retention_capacity'] == capacity
    assert result['origin']['cache_count'] == min(count, capacity)
    assert len(result['records']) == count and result['replay_matches_encoded']
    assert not result['physical_lock_qualified']
    if count == 76:
        assert result['status']['reason'] == 4
    if count >= 10:
        # Only schema 3 remembers the ninth constraint for the tenth event.
        tenth = result['records'][9]
        assert tenth['encoded_lo'] + tenth['encoded_width'] == (
            tenth['raw_hi'] * 4 + (400 if schema == 3 else 600) - 1)
    if count >= 66:
        row = result['records'][65]
        assert row['encoded_lo'] + row['encoded_width'] == (
            row['raw_hi'] * 4 + (300 if schema == 3 else 600) - 1)


@pytest.mark.parametrize('source,target', [(2, 3), (3, 2)])
@pytest.mark.parametrize('count', [10, 65, 76])
def test_origin_mislabelled_retention_rejected_with_valid_payload_crc(source, target, count):
    raw = bytearray(origin_native(source, count))
    struct.pack_into('<I', raw, 4, target)
    struct.pack_into('<I', raw, 20, target)
    struct.pack_into('<I', raw, 16, zlib.crc32(raw[264:]))
    with pytest.raises(ValueError, match='differs from replay'):
        trace.decode(bytes(raw), 123)


@pytest.mark.parametrize('schema', [0, 4, 99, 0xffffffff])
def test_origin_unknown_versions_rejected_even_when_empty(schema):
    raw = bytearray(origin_native(3, 0))
    struct.pack_into('<I', raw, 4, schema)
    struct.pack_into('<I', raw, 20, schema)
    with pytest.raises(ValueError, match='format'):
        trace.decode(bytes(raw), 123)


@pytest.mark.parametrize('schema', [2, 3])
def test_origin_prefix_status_version_mismatch(schema):
    raw = bytearray(origin_native(schema, 1))
    struct.pack_into('<I', raw, 20, 5 - schema)
    with pytest.raises(ValueError, match='identity'):
        trace.decode(bytes(raw), 123)


@pytest.mark.parametrize('schema', [2, 3])
def test_origin_versioned_stop_download(schema):
    raw = origin_native(schema, 76)
    actual, pages = trace.download_capture(query_for(raw), 123, 128)
    assert actual == raw and len(pages) > 1
