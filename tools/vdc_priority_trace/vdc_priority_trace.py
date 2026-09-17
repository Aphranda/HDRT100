"""Decode a frozen typed VDC trace and verify bounded STOP-only RAM transfers.

Native layout: components/vdc_dpll_manager/inc/vdc_priority_trace.h.
This module never starts boards or polls a running loop. Its optional query
callback is supplied by a caller that has already established all-board STOP.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
from typing import Callable
import zlib

MAGIC = 0x52545056
SCHEMA = 1
ORIGIN_SCHEMA = 3
# Immutable wire semantics: historical captures must not inherit today's cap.
ORIGIN_SCHEMA_CAPACITIES = {2: 8, 3: 64}
RECORD_BYTES = 100
MAX_RECORDS = 76
READ_MAX_BYTES = 128
STATUS_FIELDS = (
    'schema request_seq ack_seq command state reason capture_id session generation '
    'sample_interval_ms capacity record_count match_count decision_count skipped_count dropped_count '
    'first_ms last_ms freeze_ms ring_config_seq ring_applied_seq local_slot reference_slot node_count '
    'schedule_crc32 profile_crc32 path_table_crc32 path_crc32 delay_ns clock_epoch clock_run '
    'arm_epoch_lo arm_epoch_hi observer_epoch rx_epoch tick_hz mode'
).split()
PREFIX = struct.Struct('<5I')
STATUS = struct.Struct('<' + 'I' * len(STATUS_FIELDS))
HEADER_BYTES = PREFIX.size + STATUS.size
ORIGIN_HEADER_BYTES = HEADER_BYTES + 96
ORIGIN_EXTENSION = struct.Struct('<12I4Q2q')
ORIGIN_EXTENSION_FIELDS = ('role_generation source_epoch first_source_identity last_source_identity '
    'first_published_version last_published_version last_event_sequence last_reset_reason '
    'cache_count cache_epoch cache_tick_hz cache_model_token anchor_raw anchor_local_ns '
    'last_raw_after last_local_ns offset_lo offset_hi_open').split()
ORIGIN_RECORD = struct.Struct('<5I7QiiIQI')
ORIGIN_FIELDS = ('index kind event_sequence model_token tick_hz raw_lo raw_hi bridge_before bridge_after '
    'bridge_local_ns base_local_ns base_output_ns rate_ppb phase_ns dco_seq encoded_lo encoded_width').split()
COMMON = struct.Struct('<5I')
MATCH = struct.Struct('<QQqqQQQQIIiI')
DECISION = struct.Struct('<IIIIiiiIqqQQQQ')
MATCH_FIELDS = 'raw_lo raw_hi residual_lo residual_hi local_lo local_hi remote_lo remote_hi model_token dco_seq actual_ppb match_reason'.split()
DECISION_FIELDS = 'baseline_sequence model_token before_seq after_seq before_ppb after_ppb delta_ppb follow_reason error_lo_ppb error_hi_ppb expected_delta_lo expected_delta_hi local_delta_lo local_delta_hi'.split()
READ = 'SYSTem:VDC:PRIORity:TRACe:READ?'


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def parse_status(response: str) -> dict[str, int]:
    fields = next(csv.reader([response]))
    require(len(fields) == len(STATUS_FIELDS), 'Wrong status word count')
    values = [int(word) for word in fields]
    require(all(0 <= value <= 0xffffffff for value in values), 'Status word out of range')
    return dict(zip(STATUS_FIELDS, values))


def decode(data: bytes, expected_capture_id: int | None = None) -> dict:
    require(len(data) >= HEADER_BYTES, 'Truncated native header')
    magic, schema, header_bytes, record_bytes, payload_crc = PREFIX.unpack_from(data)
    expected_header = {SCHEMA: HEADER_BYTES, **dict.fromkeys(ORIGIN_SCHEMA_CAPACITIES, ORIGIN_HEADER_BYTES)}.get(schema)
    require((magic, header_bytes, record_bytes) ==
            (MAGIC, expected_header, RECORD_BYTES), 'Unknown native trace format')
    require(len(data) >= header_bytes, 'Truncated native header')
    status = dict(zip(STATUS_FIELDS, STATUS.unpack_from(data, PREFIX.size)))
    require(status['schema'] == schema and status['capture_id'] != 0, 'Invalid capture identity')
    require(expected_capture_id is None or status['capture_id'] == expected_capture_id, 'Capture ID changed')
    require(status['state'] == 3 and status['request_seq'] == status['ack_seq'] != 0,
            'Capture is not acknowledged and frozen')
    require(status['command'] in (1, 2), 'Frozen capture has unexpected command')
    count = status['record_count']
    require(status['capacity'] == MAX_RECORDS and count <= status['capacity'], 'Invalid record capacity')
    if schema == SCHEMA:
        require(count == status['match_count'] + status['decision_count'], 'Record counts disagree')
    else:
        require(status['match_count'] == status['decision_count'] == status['sample_interval_ms'] == 0, 'Origin counters mislabelled')
    require(len(data) == header_bytes + count * RECORD_BYTES, 'Truncated or trailing native payload')
    require(zlib.crc32(data[header_bytes:]) == payload_crc, 'Payload CRC mismatch')
    if schema in ORIGIN_SCHEMA_CAPACITIES:
        return decode_origin(data, status, header_bytes)
    records = []
    counts = {1: 0, 2: 0}
    for index in range(count):
        offset = HEADER_BYTES + index * RECORD_BYTES
        row = dict(zip('index kind uptime_ms event_sequence carrier_sequence'.split(), COMMON.unpack_from(data, offset)))
        require(row['index'] == index and row['kind'] in counts, 'Invalid record index or kind')
        counts[row['kind']] += 1
        if row['kind'] == 1:
            row.update(zip(MATCH_FIELDS, MATCH.unpack_from(data, offset + COMMON.size)))
            for low, high in [('raw_lo', 'raw_hi'), ('local_lo', 'local_hi'),
                              ('remote_lo', 'remote_hi'), ('residual_lo', 'residual_hi')]:
                require(row[low] <= row[high], 'Reversed match interval')
            delay = status['delay_ns']
            require(row['residual_lo'] == row['local_lo'] - row['remote_hi'] - delay and
                    row['residual_hi'] == row['local_hi'] - row['remote_lo'] - delay,
                    'Residual does not match local/remote intervals and installed delay')
            require(row['model_token'] != 0 and row['dco_seq'] != 0, 'Missing match model identity')
        else:
            row.update(zip(DECISION_FIELDS, DECISION.unpack_from(data, offset + COMMON.size)))
            e0, e1 = row['expected_delta_lo'], row['expected_delta_hi']
            l0, l1 = row['local_delta_lo'], row['local_delta_hi']
            require(0 < e0 <= e1 and 0 < l0 <= l1, 'Invalid decision intervals')
            lo = l0 * 10**9 // e1 - 10**9
            hi = -(-l1 * 10**9 // e0) - 10**9
            require((lo, hi) == (row['error_lo_ppb'], row['error_hi_ppb']), 'Frequency interval arithmetic mismatch')
            before, after, delta = row['before_seq'], row['after_seq'], row['delta_ppb']
            require(before != 0 and row['model_token'] != 0, 'Missing decision model identity')
            if after == before + 1:
                require(delta != 0 and row['after_ppb'] == row['before_ppb'] + delta, 'Inconsistent actual DCO apply')
                row['outcome'] = 'applied'
            else:
                require(after == before and row['after_ppb'] == row['before_ppb'], 'Unexplained DCO change')
                row['outcome'] = 'no_adjust' if delta == 0 else 'rejected'
        records.append(row)
    require((counts[1], counts[2]) == (status['match_count'], status['decision_count']), 'Record kinds disagree with header')
    return dict(schema='VDC_TYPED_TRACE_DECODE_V1', status=status, records=records,
                bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), file_crc32=zlib.crc32(data),
                complete_window_proven=False, physical_lock_qualified=False)



def decode_origin(data: bytes, status: dict, header_bytes: int) -> dict:
    """Replay all committed bridge inputs, with rational offset bounds.

    This proves the recorded event-model computation. It does not establish
    the clock lifetime assumptions or physical output-edge precision.
    """
    from fractions import Fraction
    capacity = ORIGIN_SCHEMA_CAPACITIES.get(status['schema'])
    require(capacity is not None, 'Unknown origin trace schema')
    ext = dict(zip(ORIGIN_EXTENSION_FIELDS, ORIGIN_EXTENSION.unpack_from(data, HEADER_BYTES)))
    records, cache, epoch, prior = [], None, 0, None
    def floor(value):
        return value.numerator // value.denominator
    def ceil(value):
        return -((-value.numerator) // value.denominator)
    for index in range(status['record_count']):
        r = dict(zip(ORIGIN_FIELDS, ORIGIN_RECORD.unpack_from(data, header_bytes + index * RECORD_BYTES)))
        require(r['index'] == index and r['kind'] == 3, 'Invalid origin record index or kind')
        h, token = r['tick_hz'], r['model_token']
        e0, e1, b0, b1, u = (r[k] for k in ('raw_lo', 'raw_hi', 'bridge_before', 'bridge_after', 'bridge_local_ns'))
        require(0 < h <= 500_000_000 and h == status['tick_hz'] and token > 0 and r['dco_seq'] > 0, 'Invalid origin clock/model')
        require(e0 <= e1 <= b0 <= b1 and b1-e0 <= 2*h and u % 1000 == 0 and
                u <= 2**64-1000, 'Invalid origin bridge interval')
        require(r['rate_ppb'] > -10**9 and 0 < r['encoded_width'] <= 0xffffffff, 'Invalid encoded interval')
        if prior is not None:
            require(r['event_sequence'] > prior['event_sequence'] and e0 >= prior['raw_lo'] and
                    e1 >= prior['raw_hi'] and b0 >= prior['bridge_after'] and u >= prior['bridge_local_ns'],
                    'Origin event or bridge rollback')
            if token == prior['model_token']:
                require(all(r[k] == prior[k] for k in ('base_local_ns','base_output_ns','rate_ppb','phase_ns','dco_seq')),
                        'Origin model changed without token')
        reason = 1 if cache is None else (2 if token != cache['model'] else (3 if b1-cache['raw'] > 2*h else 0))
        if reason:
            epoch += 1
            cache = dict(raw=e0, local=u, model=token, count=0, low=None, high=None)
        low = Fraction(u) - Fraction(b1*10**9, h)
        high = Fraction(u+1000) - Fraction(b0*10**9, h)
        single = (floor(low+Fraction(e0*10**9, h)), ceil(high+Fraction(e1*10**9, h))-1)
        if cache['count']:
            low, high = max(low, cache['low']), min(high, cache['high'])
        require(low < high, 'Contradictory origin clock constraints')
        refined = (floor(low+Fraction(e0*10**9, h)), ceil(high+Fraction(e1*10**9, h))-1)
        def output(local):
            require(0 <= local < 2**64, 'Origin local coordinate overflow')
            delta = local-r['base_local_ns']
            require(delta >= 0, 'Origin projection before DCO base')
            rate = delta*abs(r['rate_ppb'])//10**9
            value = r['base_output_ns']+delta+(rate if r['rate_ppb'] >= 0 else -rate)+r['phase_ns']
            require(0 <= value < 2**64, 'Origin output overflow')
            return value
        original = tuple(map(output, single))
        mapped = tuple(map(output, refined))
        actual = (max(original[0], mapped[0]), min(original[1], mapped[1]))
        require(actual == (r['encoded_lo'], r['encoded_lo']+r['encoded_width']), 'Origin encoded interval differs from replay')
        if cache['count'] < capacity:
            cache.update(low=low, high=high, count=cache['count']+1)
        r.update(original_lo=original[0], original_hi=original[1], original_width=original[1]-original[0],
                 saved_width=original[1]-original[0]-r['encoded_width'], replay_epoch=epoch, reset_reason=reason)
        records.append(r)
        prior = r
    if records:
        last = records[-1]
        require(ext['role_generation'] > 0 and ext['source_epoch'] > 0, 'Missing origin binding')
        expected = dict(last_event_sequence=last['event_sequence'], last_reset_reason=last['reset_reason'],
            cache_count=cache['count'], cache_epoch=epoch, cache_tick_hz=last['tick_hz'], cache_model_token=cache['model'],
            anchor_raw=cache['raw'], anchor_local_ns=cache['local'], last_raw_after=last['bridge_after'],
            last_local_ns=last['bridge_local_ns'])
        for key in ('low', 'high'):
            scaled = (cache[key] - cache['local'] + Fraction(cache['raw']*10**9, last['tick_hz']))*last['tick_hz']
            require(scaled.denominator == 1, 'Nonintegral scaled cache')
            expected['offset_lo' if key == 'low' else 'offset_hi_open'] = int(scaled)
        require(all(ext[k] == v for k, v in expected.items()), 'Origin final cache differs from replay')
    else:
        require(not any(ext.values()), 'Empty origin capture contains cache')
    return dict(schema=f"VDC_ORIGIN_TRACE_DECODE_V{status['schema']}", status=status, origin=ext, records=records,
                mapping_retention_capacity=capacity,
                bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), file_crc32=zlib.crc32(data),
                replay_matches_encoded=True, complete_window_proven=False, physical_lock_qualified=False)


def parse_page(response: str, offset: int, size: int) -> tuple[int, int, bytes]:
    fields = next(csv.reader([response]))
    require(len(fields) == 5, 'Wrong RAM page field count')
    actual_offset, actual_size, total, crc = map(int, fields[:4])
    require(all(0 <= value <= 0xffffffff for value in (actual_offset, actual_size, total, crc)),
            'RAM page integer out of range')
    require((actual_offset, actual_size) == (offset, size), 'Stale or mismatched RAM page')
    require(len(fields[4]) == size * 2, 'Wrong RAM page hex length')
    payload = bytes.fromhex(fields[4])
    require(len(payload) == size and total >= offset + size, 'RAM page exceeds total')
    return total, crc, payload


def download_capture(query: Callable[[str], str], capture_id: int,
                     page_size: int = READ_MAX_BYTES) -> tuple[bytes, list[dict]]:
    require(0 < capture_id <= 0xffffffff, 'Invalid capture ID')
    require(0 < page_size <= READ_MAX_BYTES, 'Invalid RAM page size')
    pieces, pages = [], []
    offset, size, total, crc = 0, 4, None, None
    while True:
        command = f'{READ} {capture_id},{offset},{size}'
        raw = query(command)
        page_total, page_crc, payload = parse_page(raw, offset, size)
        require(HEADER_BYTES <= page_total <= ORIGIN_HEADER_BYTES + MAX_RECORDS * RECORD_BYTES, 'Invalid native total')
        if total is None:
            total, crc = page_total, page_crc
        require((page_total, page_crc) == (total, crc), 'Capture changed between RAM pages')
        pieces.append(payload)
        pages.append(dict(command=command, response=raw))
        offset += size
        if offset == total:
            break
        size = min(page_size, total - offset)
    data = b''.join(pieces)
    require(zlib.crc32(data) == crc, 'Whole-file RAM transfer CRC mismatch')
    decode(data, expected_capture_id=capture_id)
    return data, pages


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--capture-id', type=int)
    args = parser.parse_args()
    result = decode(args.input.read_bytes(), args.capture_id)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(records=len(result['records']), capture_id=result['status']['capture_id'], out=str(args.out))))


if __name__ == '__main__':
    main()
