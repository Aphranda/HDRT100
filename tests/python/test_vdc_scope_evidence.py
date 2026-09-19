import hashlib
import json
from concurrent.futures import Future
from threading import Event

import numpy as np
import pytest

from tools.vdc_priority_trace.vdc_scope_evidence import EvidenceWriter, analyze_memory


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
