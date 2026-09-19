import hashlib
import json
from concurrent.futures import Future
from threading import Event

import numpy as np
import pytest

from tools.vdc_priority_trace.vdc_scope_evidence import (
    EvidenceWriter,
    analyze_memory,
    capture_raw,
    channel_delay_s,
    record_channel_delays,
)


def test_slow_disk_is_bounded_and_snapshot_is_immutable(tmp_path, monkeypatch):
    entered, release = Event(), Event()
    write = EvidenceWriter._write

    def slow(folder, report, blocks):
        entered.set()
        assert release.wait(5)
        write(folder, report, blocks)

    monkeypatch.setattr(EvidenceWriter, '_write', staticmethod(slow))
    writer = EvidenceWriter(capacity=1)
    report = dict(channels=[dict(channel=1)])
    try:
        writer.submit(tmp_path, report, {'raw.block': b'original'})
        assert entered.wait(2)
        report['channels'][0]['channel'] = 99
        with pytest.raises(RuntimeError, match='queue full'):
            writer.submit(tmp_path, report, {})
        assert not (tmp_path/'scope.json').exists()
    finally:
        release.set()
        writer.close()
    saved = json.loads((tmp_path/'scope.json').read_text())
    assert saved['channels'][0]['channel'] == 1
    assert saved['persistence']['completed']
    assert (tmp_path/'raw.block').read_bytes() == b'original'


def test_write_failure_cannot_be_reported_as_complete(tmp_path, monkeypatch):
    def fail(*args):
        raise OSError('disk full')

    monkeypatch.setattr(EvidenceWriter, '_write', staticmethod(fail))
    writer = EvidenceWriter()
    writer.submit(tmp_path, {}, {})
    with pytest.raises(RuntimeError, match='disk full'):
        writer.close()
    assert not (tmp_path/'scope.json').exists()


def test_existing_raw_is_never_overwritten(tmp_path):
    (tmp_path/'raw.block').write_bytes(b'old')
    writer = EvidenceWriter()
    writer.submit(tmp_path, {}, {'raw.block': b'new'})
    with pytest.raises(RuntimeError, match='FileExistsError'):
        writer.close()
    assert (tmp_path/'raw.block').read_bytes() == b'old'


def test_prior_failure_stays_failed_but_current_capture_is_preserved(tmp_path):
    writer = EvidenceWriter()
    failed = Future()
    failed.set_exception(OSError('disk full'))
    writer.pending.append(failed)
    with pytest.raises(RuntimeError, match='disk full'):
        writer.check()
    with pytest.raises(RuntimeError, match='disk full'):
        writer.submit(tmp_path, {}, {'current.block': b'current'})
    with pytest.raises(RuntimeError, match='disk full'):
        writer.close()
    assert (tmp_path/'current.block').read_bytes() == b'current'


def waveform_fixture():
    blocks, rows = {}, []
    for channel, shift in enumerate((0, 5, -10, 20), 1):
        codes = np.full(650000, 128, dtype=np.uint8)
        for index in (100000+shift, 600000+shift):
            codes[index:index+1000] = 178
        raw = codes.tobytes()
        name = f'ch{channel}.block'
        blocks[name] = raw
        rows.append(dict(channel=channel, preamble='0,2,650000,1,2e-9,-.0002,0,.1,0,128',
                         blocks=[dict(path=name, start=1, stop=650000,
                                      sha256=hashlib.sha256(raw).hexdigest())]))
    return dict(channels=rows), blocks


def test_memory_phase_uses_frozen_four_channels_and_two_real_edges():
    report, blocks = waveform_fixture()
    phase = analyze_memory(report, blocks, lambda data, count: data)
    for actual, expected in zip(phase['relative_ns'], (10, -20, 40)):
        assert actual == pytest.approx([expected, expected])
    assert phase['within_50ns']


@pytest.mark.parametrize('fault', ['no_edge', 'checksum', 'channel_missing'])
def test_invalid_waveforms_are_not_accepted(fault):
    report, blocks = waveform_fixture()
    if fault == 'channel_missing':
        report['channels'].pop()
    elif fault == 'checksum':
        blocks['ch2.block'] = b'bad'
    else:
        raw = bytes([128])*650000
        blocks['ch2.block'] = raw
        report['channels'][1]['blocks'][0]['sha256'] = hashlib.sha256(raw).hexdigest()
    with pytest.raises(ValueError):
        analyze_memory(report, blocks, lambda data, count: data)


# The instrument reports six decimals, so a readable delay has 1 us resolution;
# values below that legitimately read back as 0.000000.
TCAL_SECONDS = {1: 0.0, 2: 2.0e-6, 3: -1.0e-6}          # CH4 has no readable value
CAPTURE_PREAMBLE = '0,2,650000,1,2e-9,-.0002,0,.1,0,128'


def capture_payload(*, edges=(100000, 600000)) -> bytes:
    """Two real rising edges per channel, matching the verified waveform shape."""
    codes = np.full(650000, 128, dtype=np.uint8)
    for index in edges:
        codes[index:index + 1000] = 178
    return codes.tobytes()


class FakeRawScope:
    """Scope double for capture_raw: one frozen acquisition, four channels."""

    def __init__(self, *, payload=None):
        self.quiet = False
        self.points = 1000000
        self.report = dict(armed_ns=1, channels=[], commands=[])
        self._raw_blocks = {}
        self.payload = payload if payload is not None else capture_payload()
        self.writes = []
        self.source = 'CHAN1'
        self.scope = self

    def write(self, command):
        self.writes.append(command)
        if command.startswith(':WAV:SOUR '):
            self.source = command.split()[-1]

    def read_raw(self):
        return b'#8' + f'{len(self.payload):08d}'.encode() + self.payload


class FakeRawReader:
    class Scope:
        @staticmethod
        def query(scope, command):
            if command == ':TRIG:STAT?':
                return 'STOP'
            if command == ':ACQ:SRAT?':
                return '5.000000E+8'
            if command == ':ACQ:MDEP?':
                return '1000000'
            if command == ':WAV:SOUR?':
                return scope.source
            if command == ':WAV:MODE?':
                return 'RAW'
            if command == ':WAV:FORM?':
                return 'BYTE'
            if command == ':WAV:STAR?':
                return '1'
            if command == ':WAV:STOP?':
                return '650000'
            if command == ':WAV:PRE?':
                return CAPTURE_PREAMBLE
            if command == ':SYST:ERR?':
                return '0,"No error"'
            if command.startswith(':CHAN') and command.endswith(':TCAL?'):
                channel = int(command[5])
                if channel not in TCAL_SECONDS:
                    raise TimeoutError('unsupported')
                return f'{TCAL_SECONDS[channel]:.6f}'
            raise AssertionError(command)

    @staticmethod
    def decode_block(raw, count):
        return raw[raw.index(b'#') + 2 + 8:][:count]


def test_capture_raw_records_instrument_channel_delay(tmp_path):
    scope = FakeRawScope()
    capture_raw(scope, FakeRawReader)
    assert scope.report['capture_complete'] is True
    assert [row['channel'] for row in scope.report['channels']] == [1, 2, 3, 4]
    assert scope.report['channel_delays_s'] == {
        '1': 0.0, '2': 2.0e-6, '3': -1.0e-6, '4': None}
    # Relative phases are CH2..CH4 against CH1, so the skew is recorded that way.
    assert scope.report['channel_delays_relative_s']['2'] == pytest.approx(2.0e-6)
    assert scope.report['channel_delays_relative_s']['3'] == pytest.approx(-1.0e-6)
    assert scope.report['channel_delays_relative_s']['4'] is None
    assert scope.report['channel_delays_relative_s']['1'] == pytest.approx(0.0)
    # Persistence and the verified edge math stay untouched by this addition.
    phase = analyze_memory(scope.report, scope._raw_blocks, FakeRawReader.decode_block)
    assert phase['relative_ns'] == [[0.0, 0.0]] * 3


def test_channel_delay_absent_or_unreadable_is_not_zeroed():
    assert channel_delay_s(lambda command: 'oops', 2) is None
    assert channel_delay_s(lambda command: (_ for _ in ()).throw(OSError()), 2) is None
    report = dict(channels=[dict(channel=1, channel_delay_s=None),
                            dict(channel=2, channel_delay_s=1.0e-9)])
    record_channel_delays(report)
    assert report['channel_delays_relative_s'] == {'1': None, '2': None}
