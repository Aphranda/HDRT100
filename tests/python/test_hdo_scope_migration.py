"""Device-free migration checks for the tracked HDO scope leaf modules."""
import json
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace

import pytest

from tools.common.hdo_scope import reader
from tools.domains.vdc.scope_capture import sparse


class Clock:
    def __init__(self):
        self.now = 10.0
        self.sleeps = []

    def monotonic(self):
        return self.now

    def monotonic_ns(self):
        return int(self.now * 1e9)

    def sleep(self, duration):
        self.sleeps.append(duration)
        self.now += max(duration, 0.00001)


@pytest.fixture
def clock(monkeypatch):
    value = Clock()
    api = SimpleNamespace(monotonic=value.monotonic,
                          monotonic_ns=value.monotonic_ns, sleep=value.sleep)
    monkeypatch.setattr(reader, 'time', api)
    monkeypatch.setattr(sparse, 'time', api)
    return value


class Instrument:
    """Minimal stateful SCPI peer; unexpected operations fail the test."""

    def __init__(self):
        self.calls = []
        self.closed = False
        self.settings = {':TRIG:STAT': 'STOP', ':WAV:STAR': '1',
                         ':WAV:STOP': '50000000', ':WAV:MODE': 'RAW'}
        self.overrides = {}
        self.errors = []

    def write(self, command):
        self.calls.append(('write', command))
        if ' ' in command:
            key, value = command.split(' ', 1)
            self.settings[key] = value
            if key == ':WAV:MODE' and value == 'RAW':
                self.settings[':WAV:STAR'] = '1'
                self.settings[':WAV:STOP'] = self.settings.get(':ACQ:MDEP', '1000000')
        if command == ':STOP':
            self.settings[':TRIG:STAT'] = 'STOP'
        if command == ':RUN':
            self.settings[':TRIG:STAT'] = 'WAIT'
        if command == ':SING':
            self.settings[':TRIG:STAT'] = 'WAIT'
            self.settings[':TRIG:SWE'] = 'SING'

    def query(self, command):
        self.calls.append(('query', command))
        if command in self.overrides:
            value = self.overrides[command]
            if isinstance(value, Exception):
                raise value
            return value
        if command == '*IDN?':
            return 'RIGOL TECHNOLOGIES,HDO4404,HDO4A244301137,00.01'
        if command == '*OPC?':
            return '1'
        if command == ':SYST:ERR?':
            return self.errors.pop(0) if self.errors else '0,"No error"'
        if command == ':WAV:PRE?':
            return '0,2,1000000,1,2e-9,-.0002,0,.1,0,128'
        return self.settings[command.removesuffix('?')]

    def close(self):
        self.closed = True


def test_import_from_clean_tree_needs_no_out_or_hardware(tmp_path):
    root = Path(__file__).resolve().parents[2]
    clock_source = root / 'tools/common/host_evidence_clock.py'
    clock_target = tmp_path / 'tools/common/host_evidence_clock.py'
    clock_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(clock_source, clock_target)
    for directory in ('tools/common/hdo_scope', 'tools/domains/vdc/scope_capture'):
        for source in (root / directory).glob('*.py'):
            target = tmp_path / directory / source.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    program = '''
import builtins, sys
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'pyvisa', 'serial'}:
        raise AssertionError('hardware dependency imported: ' + name)
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
from tools.common.hdo_scope.reader import Scope, decode_block
from tools.domains.vdc.scope_capture.sparse import SparseScope
assert decode_block(b'#13abc', 3) == b'abc'
assert not any('HardwareAcceptance' in str(getattr(m, '__file__', '')) for m in sys.modules.values())
'''
    result = subprocess.run([sys.executable, '-B', '-c', program], cwd=tmp_path,
                            text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('suffix', [b'', b'\n', b'\r\n'])
def test_binary_payload_is_not_stripped_or_text_decoded(suffix):
    payload = b'\x00\xff\r\n#'
    assert reader.decode_block(b'#15' + payload + suffix, 5) == payload


@pytest.mark.parametrize('raw,expected', [
    (b'', 3), (b'abc', 3), (b'prefix#13abc', 3), (b'#x3abc', 3),
    (b'#0abc', 3), (b'#2x3abc', 3), (b'#93', 3),
    (b'#13ab', 3), (b'#14abcd', 3), (b'#13abcx', 3),
    (b'#13abc ', 3), (b'#13abc\nignored', 3),
])
def test_malformed_or_wrong_sized_blocks_are_rejected(raw, expected):
    with pytest.raises(ValueError):
        reader.decode_block(raw, expected)


def test_sparse_prepare_preserves_long_timebase_workaround_and_short_memory(tmp_path, clock):
    instrument = Instrument()
    scope = sparse.SparseScope(tmp_path, instrument=instrument, scale=.0002,
                               points=1000000, offset=.0004)
    scope.prepare()
    commands = instrument.calls
    index = commands.index(('write', ':TIM:SCAL 0.0002'))
    expected = []
    for command in (':TRIG:SWE NORM', ':RUN', ':TIM:MODE MAIN',
                    ':TIM:SCAL 0.0002', ':TIM:MODE MAIN'):
        expected.extend([('write', command), ('query', '*OPC?'), ('query', ':SYST:ERR?')])
    assert commands[index - 9:index + 6] == expected
    writes = [command for kind, command in commands if kind == 'write']
    assert ':ACQ:MDEP 1000000' in writes
    assert not any('50000000' in command for command in writes)
    assert writes[-3:] == [':STOP', ':WAV:MODE NORM', ':WAV:MODE RAW']
    assert instrument.settings[':WAV:STOP'] == '1000000'
    assert scope.report['settings'][':TRIG:EDGE:SOUR?'] == 'CHAN1'
    assert clock.sleeps.count(.25) == 1
    assert .08 in clock.sleeps


@pytest.mark.parametrize('query,value', [
    ('*IDN?', 'RIGOL TECHNOLOGIES,HDO4404,ANOTHER-SCOPE,00.01'),
    (':TRIG:EDGE:SOUR?', 'EXT'), (':TRIG:EDGE:SLOP?', 'NEG'),
    (':ACQ:MDEP?', '50000000'), (':TIM:SCAL?', '.001'),
    (':TIM:OFFS?', '.1'), (':CHAN3:PROB?', '10'),
    ('*OPC?', '0'), (':WAV:STAR?', '2'), (':WAV:STOP?', '50000000'),
])
def test_wrong_setup_readback_cannot_be_accepted(tmp_path, clock, query, value):
    instrument = Instrument()
    instrument.overrides[query] = value
    scope = sparse.SparseScope(tmp_path, instrument=instrument, scale=.0002,
                               points=1000000, offset=.0004)
    with pytest.raises((ValueError, RuntimeError, AssertionError)):
        scope.prepare()
    assert not scope.report['capture_complete']


def test_initial_errors_are_retained_and_drain_is_bounded(tmp_path, clock):
    instrument = Instrument()
    instrument.errors = ['-200,"old error"', '+0,"No error"']
    scope = sparse.SparseScope(tmp_path / 'drained', instrument=instrument)
    scope.prepare()
    assert scope.report['initial_error_queue'] == ['-200,"old error"', '+0,"No error"']
    failing = Instrument()
    failing.overrides[':SYST:ERR?'] = '-200,"sticky"'
    scope = sparse.SparseScope(tmp_path / 'sticky', instrument=failing)
    with pytest.raises(RuntimeError, match='drain'):
        scope.prepare()
    assert len(failing.calls) == 32
    assert all(kind == 'query' for kind, _ in failing.calls)


@pytest.mark.parametrize('command,expected_sleeps', [
    (':STOP', []), (':SING', []), (':TRIG:EDGE:LEV 1.5', []),
    (':WAV:DATA?', []), (':WAV:SOUR CHAN2', [.08]),
    (':WAV:STAR 1', [.08]), (':WAV:STOP 650000', [.08]),
    (':WAV:MODE RAW', [.08]), (':WAV:FORM BYTE', [.08]),
])
def test_runtime_wait_is_only_for_asynchronous_waveform_setters(
        tmp_path, clock, monkeypatch, command, expected_sleeps):
    scope = sparse.SparseScope(tmp_path, instrument=Instrument())
    scope.sampling = True
    monkeypatch.setattr(scope, 'save', lambda: pytest.fail('runtime write persisted on VISA owner'))
    scope.write(command)
    assert clock.sleeps == expected_sleeps
    assert scope.report['commands'][-1]['end_ns'] >= scope.report['commands'][-1]['start_ns']


def test_preamble_normalizes_transfer_range_before_query(tmp_path, clock):
    instrument = Instrument()
    scope = sparse.SparseScope(tmp_path, instrument=instrument, points=1000000)
    scope.query(':WAV:PRE?')
    assert instrument.calls == [('write', ':WAV:STAR 1'),
                                ('write', ':WAV:STOP 1000000'), ('query', ':WAV:PRE?')]


def test_quiet_barrier_blocks_queries_export_and_timebase_changes(tmp_path, clock):
    instrument = Instrument()
    scope = sparse.SparseScope(tmp_path, instrument=instrument)
    scope.quiet = True
    for operation in (lambda: scope.query('*IDN?'), lambda: scope.export(),
                      lambda: scope.write(':TIM:SCAL .01')):
        with pytest.raises(RuntimeError):
            operation()
    assert not instrument.calls


def test_open_failure_closes_owned_resource_manager(tmp_path, monkeypatch):
    manager = SimpleNamespace(closed=False)
    def open_resource(resource):
        raise OSError('open failed')
    manager.open_resource = open_resource
    manager.close = lambda: setattr(manager, 'closed', True)
    monkeypatch.setitem(sys.modules, 'pyvisa', SimpleNamespace(ResourceManager=lambda: manager))
    with pytest.raises(OSError, match='open failed'):
        reader.Scope(tmp_path)
    assert manager.closed


@pytest.mark.parametrize('setting', ['timeout', 'chunk_size'])
@pytest.mark.parametrize('cleanup_failure', ['none', 'instrument', 'manager', 'both'])
def test_initial_setting_failure_releases_every_resource_and_preserves_cause(
        tmp_path, monkeypatch, setting, cleanup_failure):
    original_error = OSError(f'{setting} initialization failed')
    events = []

    class FailingInstrument:
        def __setattr__(self, key, value):
            if key == setting:
                raise original_error
            object.__setattr__(self, key, value)

        def close(self):
            events.append('instrument_close')
            if cleanup_failure in ('instrument', 'both'):
                raise RuntimeError('instrument close failed')

    def close_manager():
        events.append('manager_close')
        if cleanup_failure in ('manager', 'both'):
            raise RuntimeError('manager close failed')

    manager = SimpleNamespace(open_resource=lambda resource: FailingInstrument(),
                              close=close_manager)
    monkeypatch.setitem(sys.modules, 'pyvisa', SimpleNamespace(ResourceManager=lambda: manager))
    with pytest.raises(OSError) as error:
        reader.Scope(tmp_path)
    assert error.value is original_error
    assert events == ['instrument_close', 'manager_close']


def test_open_failure_cleanup_error_does_not_mask_original_error(tmp_path, monkeypatch):
    original_error = OSError('open_resource failed')
    events = []

    def open_resource(resource):
        raise original_error

    def close_manager():
        events.append('manager_close')
        raise RuntimeError('manager close failed')

    manager = SimpleNamespace(open_resource=open_resource, close=close_manager)
    monkeypatch.setitem(sys.modules, 'pyvisa', SimpleNamespace(ResourceManager=lambda: manager))
    with pytest.raises(OSError) as error:
        reader.Scope(tmp_path)
    assert error.value is original_error
    assert events == ['manager_close']


@pytest.mark.parametrize('failure', ['save', 'instrument_close'])
def test_close_failure_still_releases_owned_resources(tmp_path, monkeypatch, failure):
    instrument = Instrument()
    manager = SimpleNamespace(closed=False, open_resource=lambda resource: instrument)
    manager.close = lambda: setattr(manager, 'closed', True)
    monkeypatch.setitem(sys.modules, 'pyvisa', SimpleNamespace(ResourceManager=lambda: manager))
    scope = reader.Scope(tmp_path)
    if failure == 'save':
        monkeypatch.setattr(scope, 'save', lambda: (_ for _ in ()).throw(OSError('save failed')))
    else:
        def fail_close():
            instrument.closed = True
            raise OSError('instrument_close failed')
        instrument.close = fail_close
    with pytest.raises(OSError, match='failed'):
        scope.close()
    assert instrument.closed and manager.closed


def test_explicit_resource_and_identity_are_used(tmp_path, clock, monkeypatch):
    instrument = Instrument()
    instrument.overrides['*IDN?'] = 'RIGOL TECHNOLOGIES,HDO4404,TEST-SERIAL,00.01'
    opened = []
    manager = SimpleNamespace(open_resource=lambda resource: (opened.append(resource) or instrument),
                              close=lambda: None)
    monkeypatch.setitem(sys.modules, 'pyvisa', SimpleNamespace(ResourceManager=lambda: manager))
    scope = reader.Scope(tmp_path, resource='USB::TEST', expected_serial='TEST-SERIAL')
    scope.prepare()
    assert opened == ['USB::TEST']
    assert scope.report['resource'] == 'USB::TEST'
    assert scope.report['identity'].split(',')[2] == 'TEST-SERIAL'
    scope.close()


@pytest.mark.parametrize('claimed_serial', ['RIGOL TECHNOLOGIES', 'HDO4404', '00.01'])
def test_scope_serial_must_match_exact_idn_serial_field(tmp_path, clock, claimed_serial):
    instrument = Instrument()
    scope = reader.Scope(tmp_path, instrument=instrument, expected_serial=claimed_serial)
    with pytest.raises(ValueError, match='Wrong scope serial'):
        scope.prepare()
    assert [call for call in instrument.calls if call == ('query', '*IDN?')] == [
        ('query', '*IDN?')
    ]


class RawInstrument(Instrument):
    def __init__(self):
        super().__init__()
        self.settings[':ACQ:MDEP'] = '8'
        self.payload = b'#18\x00\x80\xff\x80\x00\x80\xff\x00\n'
        self.raw_reads = 0
        self.fault = None

    def query(self, command):
        if command == ':ACQ:SRAT?':
            return '500000000'
        if command == ':WAV:PRE?':
            if self.fault == 'axis' and self.settings[':WAV:SOUR'] == 'CHAN2':
                return '0,2,8,1,2e-9,1e-9,0,.1,0,128'
            if self.fault == 'nonfinite':
                return '0,2,8,1,nan,0,0,.1,0,128'
            return '0,2,8,1,2e-9,0,0,.1,0,128'
        if command == ':TRIG:STAT?' and self.fault == 'unfrozen' and self.raw_reads:
            return 'WAIT'
        return super().query(command)

    def read_raw(self):
        self.raw_reads += 1
        return self.payload


def test_small_frozen_record_exports_four_channels_with_verified_bytes(tmp_path, clock):
    instrument = RawInstrument()
    scope = reader.Scope(tmp_path, instrument=instrument, points=8)
    scope.report['armed_ns'] = 1
    scope.export()
    saved = json.loads((tmp_path / 'scope.json').read_text(encoding='utf-8'))
    assert saved['capture_complete'] and not saved['physical_lock_qualified']
    assert [row['channel'] for row in saved['channels']] == [1, 2, 3, 4]
    assert instrument.raw_reads == 4
    for row in saved['channels']:
        block = row['blocks'][0]
        raw = (tmp_path / block['path']).read_bytes()
        assert raw == instrument.payload
        assert hashlib.sha256(raw).hexdigest() == block['sha256']
        assert len(reader.decode_block(raw, 8)) == 8
    assert not any(command in (':SING', ':RUN') for _, command in instrument.calls)


@pytest.mark.parametrize('fault', ['axis', 'nonfinite', 'unfrozen', 'bad_block'])
def test_export_failure_never_qualifies_a_capture_and_keeps_raw_evidence(tmp_path, clock, fault):
    instrument = RawInstrument()
    instrument.fault = fault
    if fault == 'bad_block':
        instrument.payload = b'#18truncated'
    scope = reader.Scope(tmp_path, instrument=instrument, points=8)
    scope.report['armed_ns'] = 1
    with pytest.raises(ValueError):
        scope.export()
    assert not scope.report['capture_complete']
    if instrument.raw_reads:
        assert (tmp_path / 'ch1-00000001.block').read_bytes() == instrument.payload
        saved = json.loads((tmp_path / 'scope.json').read_text(encoding='utf-8'))
        assert not saved['capture_complete']
        if fault == 'bad_block':
            assert saved['channels'][0]['blocks'][0]['error']


def test_old_arm_admission_is_cleared_when_new_wait_is_missing(tmp_path, clock):
    instrument = Instrument()
    instrument.overrides[':TRIG:STAT?'] = 'STOP'
    scope = reader.Scope(tmp_path, instrument=instrument)
    scope.report.update(armed_ns=1, capture_complete=True)
    with pytest.raises(TimeoutError, match='WAIT'):
        scope.arm()
    assert 'armed_ns' not in scope.report
    assert not scope.report['capture_complete']
    with pytest.raises(RuntimeError, match='arm'):
        scope.export()
