"""Optional VISA sampling boundaries; never connect boards or instruments."""
import copy
from types import SimpleNamespace

import pytest

from tools.vdc_priority_trace import vdc_priority_joint_capture as joint


def good_sample(due):
    return dict(due_s=due, capture_complete=True,
                phase=dict(relative_ns=[[-100.0, 100.0], [0.0], [1.0]]))


@pytest.mark.parametrize('due', [60, 600])
def test_complete_checkpoint_uses_only_its_minute(due):
    rows = [good_sample(s) for s in range(due-55, due+1, 5)]
    rows += [dict(due_s=due-60), dict(due_s=due+5)]
    result = joint.scope_checkpoint(rows, due)
    assert result['continue_run'] and not result['failures']
    assert not result['unobserved_intervals_qualified']


@pytest.mark.parametrize('fault', ['missing', 'duplicate', 'nan', 'infinity', 'empty',
                                  'missing_channel', 'incomplete', 'missed', 'overrun', 'outside'])
def test_bad_checkpoint_cannot_borrow_a_preceding_good_result(fault):
    rows = [good_sample(s) for s in range(5, 121, 5)]
    assert joint.scope_checkpoint(rows, 60)['continue_run']
    row = rows[-1]
    if fault == 'missing': rows.pop()
    elif fault == 'duplicate': rows.append(copy.deepcopy(row))
    elif fault == 'nan': row['phase']['relative_ns'][0] = [float('nan')]
    elif fault == 'infinity': row['phase']['relative_ns'][0] = [float('inf')]
    elif fault == 'empty': row['phase']['relative_ns'][1] = []
    elif fault == 'missing_channel': row['phase']['relative_ns'].pop()
    elif fault == 'incomplete': row['capture_complete'] = False
    elif fault == 'missed': row['missed'] = True
    elif fault == 'overrun': row['overrun'] = True
    elif fault == 'outside': row['phase']['relative_ns'][2] = [-100.00001]
    result = joint.scope_checkpoint(rows, 120)
    assert not result['continue_run'] and result['failures']
    assert joint.scope_checkpoint(rows, 60)['continue_run']


@pytest.mark.parametrize('internal,enabled,external,errors,passed', [
    (True, False, False, [], True), (False, False, True, [], False),
    (True, True, True, [], True), (True, True, False, [], False),
    (False, True, True, [], False), (True, True, True, ['cleanup failed'], False),
])
def test_joint_verdict_keeps_internal_external_and_cleanup_separate(internal, enabled, external, errors, passed):
    result = joint.joint_verdict(internal, enabled,
        [dict(checkpoint_s=60, continue_run=external)], 60, errors)
    assert result['passed'] is passed
    assert result['internal_health_passed'] is internal
    assert result['external_sampled_windows_passed'] is (external if enabled else None)
    assert not result['physical_continuous_lock_qualified']
    assert not result['exact_event_correlation_qualified']


@pytest.mark.parametrize('decisions', [[],
    [dict(checkpoint_s=60, continue_run=True)],
    [dict(checkpoint_s=60, continue_run=True)]*2,
    [dict(checkpoint_s=120, continue_run=True), dict(checkpoint_s=60, continue_run=True)],
])
def test_joint_verdict_requires_distinct_ordered_current_checkpoints(decisions):
    assert not joint.joint_verdict(True, True, decisions, 120, [])['passed']


class Clock:
    def __init__(self):
        self.now = 10.0

    def monotonic(self):
        return self.now

    def monotonic_ns(self):
        return int(self.now * 1e9)

    def sleep(self, seconds):
        self.now += max(float(seconds), .00001)


@pytest.fixture
def clock(monkeypatch):
    clock = Clock()
    monkeypatch.setattr(joint, 'time', SimpleNamespace(monotonic=clock.monotonic,
        monotonic_ns=clock.monotonic_ns, sleep=clock.sleep, time=lambda: 1234))
    return clock


def scope_external(clock, mode='good'):
    class Scope:
        def __init__(self, *args, **kwargs):
            self.scope = SimpleNamespace(timeout=0)
            self.report = dict(identity='mock scope', settings={})
            self.commands = []
            self.state = 'STOP'
            self.level = 1.5
            self.source = 'EXT'
            self.exports = 0
            self.saved_reports = []

        def prepare(self):
            pass

        def save(self):
            self.saved_reports.append(copy.deepcopy(self.report))

        def write(self, command):
            self.commands.append(command)
            if command.startswith(':TRIG:EDGE:SOUR '):
                self.source = command.rsplit(' ', 1)[1]
            if command.startswith(':TRIG:EDGE:LEV '):
                self.level = float(command.rsplit(' ', 1)[1])
                if self.level == 1.5 and self.state == 'WAIT' and mode != 'no_new_trigger':
                    self.state = 'STOP'
            if command == ':SING': self.state = 'STOP' if mode == 'old_stopped' else 'WAIT'
            if command == ':STOP': self.state = 'STOP'

        def query(self, command):
            self.commands.append(command)
            clock.sleep(.001)
            self.save()  # The real inherited query persists after every I/O.
            return {':TRIG:STAT?': self.state, ':TRIG:EDGE:LEV?': str(self.level),
                    ':TRIG:SWE?': 'AUTO' if mode == 'wrong_mode' else 'SING',
                    ':TRIG:EDGE:SOUR?': self.source, ':SYST:ERR?': '0,"No error"'}[command]

        def export(self):
            assert self.state == 'STOP' and self.report['fresh_wait_ns'] <= self.report['trigger_admitted_ns']
            self.exports += 1
            if mode == 'export_failure':
                self.report['partial_block'] = 'retained'
                self.save()
                raise IOError('RAW transfer failed')
            self.report['capture_complete'] = True

    return SimpleNamespace(SparseScope=Scope, base=SimpleNamespace(scope_reader=SimpleNamespace(RESOURCE='MOCK')))


@pytest.mark.parametrize('source', ['EXT', 'CHAN1'])
def test_scope_requires_new_wait_then_stop_before_export(tmp_path, clock, source):
    scope = joint.make_scope_class(scope_external(clock))()
    scope.trigger_source = source
    scope.prepare()
    first = scope.fresh_snapshot(tmp_path/'first')
    assert first['capture_complete'] and scope.exports == 1
    assert first['fresh_wait_ns'] <= first['trigger_admitted_ns'] < first['trigger_complete_observed_ns']
    second = scope.fresh_snapshot(tmp_path/'second')
    assert second is not first and scope.exports == 2
    assert second['fresh_wait_ns'] > first['trigger_complete_observed_ns']
    assert scope.commands.count(':SING') == 2
    with pytest.raises(FileExistsError): scope.fresh_snapshot(tmp_path/'first')
    assert scope.exports == 2


@pytest.mark.parametrize('mode', ['old_stopped', 'no_new_trigger', 'wrong_mode'])
def test_scope_never_exports_preceding_frozen_record(tmp_path, clock, mode):
    scope = joint.make_scope_class(scope_external(clock, mode))()
    scope.prepare()
    with pytest.raises((TimeoutError, ValueError)):
        scope.fresh_snapshot(tmp_path/'failed')
    assert not scope.report['capture_complete'] and not scope.exports


@pytest.mark.parametrize('mode', ['good', 'no_new_trigger', 'export_failure'])
def test_snapshot_batches_disk_saves_and_flushes_partial_evidence(tmp_path, clock, mode):
    scope = joint.make_scope_class(scope_external(clock, mode))()
    scope.prepare()
    before = len(scope.saved_reports)
    if mode == 'good':
        scope.fresh_snapshot(tmp_path/'sample')
    else:
        with pytest.raises((TimeoutError, IOError)):
            scope.fresh_snapshot(tmp_path/'sample')
    assert len(scope.saved_reports) == before + 1
    assert scope.saved_reports[-1] == scope.report
    assert not scope._defer_save
    assert scope.report['capture_complete'] is (mode == 'good')
    if mode == 'export_failure':
        assert scope.report['partial_block'] == 'retained'
    scope.query(':SYST:ERR?')
    assert len(scope.saved_reports) == before + 2  # Recovery/setup still persists.


def fake_adapter(tmp_path, clock, *, enabled=True, fault=None, internal=True):
    events = []

    class SparseScope:
        def __init__(self, *args, **kwargs):
            events.append('scope_construct')
            self.quiet = False

        def prepare(self):
            events.append('scope_prepare')

        def fresh_snapshot(self, folder):
            events.append(('scope_capture', folder.name))
            if fault in ('capture', 'undrained') and folder.name == 'sample-0060':
                raise TimeoutError('new record never completed')
            if fault == 'overrun' and folder.name == 'sample-0005': clock.sleep(10.1)
            clock.sleep(.05)
            return dict(capture_complete=True, trigger_admitted_ns=clock.monotonic_ns()-100,
                        trigger_complete_observed_ns=clock.monotonic_ns(), elapsed_s=.05)

        def write(self, command):
            events.append(('scope_write', command))

        def query(self, command):
            events.append(('scope_query', command))
            return '-100,"pending"' if fault == 'undrained' else '0,"No error"'

        def close(self):
            events.append('scope_close')

    def analyze(folder):
        events.append(('analyze', folder.name))
        return dict(relative_ns=[[101 if fault == 'outside' else 0], [0], [0]])

    external = SimpleNamespace(SparseScope=SparseScope, analyze_window=analyze,
        startup=SimpleNamespace(parallel_start=lambda probe: events.append('board_START')))

    class Probe:
        def __init__(self, opt):
            self.opt = opt
            self.boards = ['board1', 'board2']
            self.report = dict(plan={}, errors=[], passed=False,
                strict_replan_qualification=dict(criteria_passed=True, passed=False))

        def configure(self):
            events.append('base_configure')

        def acquire(self):
            events.append('base_acquire')

        def save(self):
            pass

        def collect(self):
            assert self.barrier, 'Board evidence may be read only after STOP'
            events.append('board_stopped_readback')
            self.report['passed'] = internal

        def command(self, board, command):
            assert '?' not in command, 'No board query is legal in the acquisition interval'
            assert self.block_queries and not self.barrier
            events.append(('board_command', command))
            return '1234'

        def run(self):
            try:
                self.configure()
                self.acquire()
            except Exception as exc:
                self.report['errors'].append(str(exc))
                raise
            finally:
                events.append('board_STOP_barrier')
                self.barrier = True
                self.collect()
                if hasattr(self, 'scope'): self.scope.close()
                if fault == 'cleanup':
                    self.report['errors'].append('scope restoration failed')
                    self.report['passed'] = False

    path = tmp_path/'adapter.py'
    path.write_text('# fake validated bench adapter\n', encoding='utf-8')
    opt = SimpleNamespace(external_scope=enabled, scope_trigger='CHAN1', seconds=60, out=tmp_path, bench_adapter=path)
    return SimpleNamespace(Probe=Probe, base=SimpleNamespace(external=external)), opt, events


def test_scope_off_never_constructs_scope_and_delegates_original_capture(tmp_path, clock, monkeypatch):
    adapter, opt, events = fake_adapter(tmp_path, clock, enabled=False)
    def forbidden(*args, **kwargs):
        raise AssertionError('scope capability accessed while off')
    adapter.base.external = SimpleNamespace()
    monkeypatch.setattr(joint, 'make_scope_class', forbidden)
    probe = joint.make_probe_class(adapter)(opt)
    probe.run()
    assert events == ['base_configure', 'base_acquire', 'board_STOP_barrier', 'board_stopped_readback']
    assert probe.report['passed'] and probe.report['joint_verdict']['external_status'] == 'SKIPPED'


@pytest.mark.parametrize('fault,internal', [(None, True), (None, False), ('outside', True),
                                          ('capture', True), ('overrun', True), ('undrained', True),
                                          ('cleanup', True)])
def test_joint_acquisition_keeps_board_silent_and_always_cleans_up(tmp_path, clock, monkeypatch, fault, internal):
    adapter, opt, events = fake_adapter(tmp_path, clock, fault=fault, internal=internal)
    monkeypatch.setattr(joint, 'make_scope_class', lambda external: external.SparseScope)
    probe = joint.make_probe_class(adapter)(opt)
    if fault == 'undrained':
        with pytest.raises(RuntimeError, match='did not drain'): probe.run()
    else: probe.run()
    assert 'base_acquire' not in events
    assert events.index('board_START') < events.index('board_STOP_barrier')
    assert events[-3:] == ['board_STOP_barrier', 'board_stopped_readback', 'scope_close']
    assert len([e for e in events if isinstance(e, tuple) and e[0] == 'board_command']) == 1
    verdict = probe.report['joint_verdict']
    assert verdict['passed'] is (fault is None and internal)
    assert verdict['internal_health_passed'] is internal
    if fault is None:
        assert len(probe.report['scope_samples']) == 12
        assert verdict['external_sampled_windows_passed'] is True
        assert probe.report['quiet_elapsed_s'] >= 62
    if fault == 'capture':
        final = probe.report['scope_samples'][-1]
        assert final['missed'] and 'phase' not in final and 'capture_complete' not in final
        assert ('analyze', 'sample-0060') not in events
        assert not verdict['external_sampled_windows_passed']
    if fault == 'overrun':
        assert probe.report['scope_samples'][0]['overrun']
        assert probe.report['scope_samples'][1]['missed']
        assert ('scope_capture', 'sample-0010') not in events
    if fault == 'undrained':
        assert len([e for e in events if e == ('scope_query', ':SYST:ERR?')]) == 16
        assert probe.report['errors'] and not verdict['passed']
    if fault == 'cleanup':
        assert verdict['external_sampled_windows_passed'] is True
        assert verdict['internal_health_passed'] is True and not verdict['passed']


@pytest.mark.parametrize('seconds', ['0', '59', '61', '601', '-60'])
def test_invalid_duration_rejected_before_adapter_or_scope(tmp_path, seconds):
    adapter = tmp_path/'adapter.py'
    adapter.write_text('raise AssertionError("must not import")\n', encoding='utf-8')
    with pytest.raises(SystemExit):
        joint.parse_args(['--bench-adapter', str(adapter), '--seconds', seconds, '--out', str(tmp_path)])


def test_scope_default_off_and_minute_duration_limit(tmp_path):
    adapter = tmp_path/'adapter.py'
    adapter.write_text('# validated test adapter\n', encoding='utf-8')
    options = joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path), '--seconds', '600'])
    assert options.scope == 'off' and options.seconds == 600
    assert tuple(options.output_delays) == (0, -28, -88, -116)
    assert options.scope_trigger == 'CHAN1'
    options = joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path),
                                '--scope', 'on', '--scope-trigger', 'EXT'])
    assert options.scope == 'on' and options.scope_trigger == 'EXT'


@pytest.mark.parametrize('values', [('-2147483649', '0', '0', '0'),
                                   ('0', '0', '0', '2147483648'), ('0', '20', '0')])
def test_invalid_delays_rejected_before_hardware(tmp_path, values):
    adapter = tmp_path/'adapter.py'
    adapter.write_text('# fixture', encoding='utf-8')
    with pytest.raises(SystemExit):
        joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path),
                          '--output-delays', *values])


def test_explicit_delays_reach_stop_restore_runner(tmp_path, monkeypatch):
    path = tmp_path/'adapter.py'
    path.write_text('# fixture', encoding='utf-8')
    monkeypatch.setattr(joint.sys, 'argv', ['capture', '--bench-adapter', str(path),
                        '--out', str(tmp_path), '--output-delays', '0', '-8', '-68', '-116'])
    seen = {}

    class Runner:
        def __init__(self, *args, **kwargs):
            seen.update(kwargs)

        def run(self):
            return dict(passed=True, errors=[], cleanup_errors=[], restore_needed=False, duration_s=60)

    hil = SimpleNamespace(validate_receipt=lambda path: 'receipt', SerialBackend=lambda: 'backend')
    base = SimpleNamespace(ResumeTrial=Runner, trial=SimpleNamespace(hil=hil))
    monkeypatch.setattr(joint, 'load_adapter', lambda path:
                        SimpleNamespace(base=SimpleNamespace(external=SimpleNamespace(base=base))))
    assert joint.main() == 0
    assert seen['output_delays'] == (0, -8, -68, -116)
