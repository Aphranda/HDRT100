"""Optional VISA sampling boundaries; never connect boards or instruments."""
import copy
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.vdc_priority_trace import vdc_priority_joint_capture as joint
from tools.workflows.vdc_joint_capture import inputs as workflow_inputs


WORKFLOW_UIDS = ['uid1', 'uid2', 'uid3', 'uid4']
WORKFLOW_BASELINE_COMMANDS = [
    '*IDN?', 'SYSTem:FW:BUILD?', 'SYSTem:TDMA:RING:STATus?',
    'SYSTem:TDMA:FLIGHT:PRIORity:TIMing?',
    'SYSTem:TDMA:FLIGHT:PRIORity:BUDGet?',
    'SYSTem:TDMA:FLIGHT:PRIORity?',
]


def workflow_input_fixture(tmp_path):
    root = tmp_path / 'repo'
    adapter = root / 'out/adapter.py'
    startup = root / 'out/startup.py'
    summary = root / 'out/summary.py'
    monitor = root / 'out/monitor.py'
    parallel = root / 'out/parallel.py'
    late_scope = root / 'out/late_scope.py'
    outer_trial = root / 'out/outer_trial.py'
    outer_hil = root / 'out/outer_hil.py'
    legacy_scope = root / 'out/legacy_scope.py'
    run_output = root / 'out/run_output.py'
    probe_support = root / 'out/probe_support.py'
    phase_timer1 = root / 'out/phase_timer1.py'
    ancestor = root / 'out/ancestor.py'
    receipt = root / 'config/receipt.json'
    baseline = root / 'out/baseline.json'
    tap = root / 'out/tap.json'
    manifest = root / 'config/workflow.json'
    migrated = {
        'internal_seal': adapter,
        'quiet_startup': startup,
        'internal_summary': summary,
        'external_monitor': monitor,
        'parallel_startup': parallel,
        'late_scope_adapter': late_scope,
        'outer_trial': outer_trial,
        'outer_hil': outer_hil,
        'legacy_scope_reader': legacy_scope,
        'run_output': run_output,
        'probe_support': probe_support,
        'phase_timer1': phase_timer1,
    }
    for path in (*migrated.values(), ancestor, receipt, baseline, tap, manifest):
        path.parent.mkdir(parents=True, exist_ok=True)
    for role, path in migrated.items():
        path.write_text(f'# migrated {role}\n', encoding='utf-8')
    adapter.write_text('raise AssertionError("adapter must not import")\n', encoding='utf-8')
    ancestor.write_text('# transitive executable dependency\n', encoding='utf-8')
    receipt.write_text(json.dumps({'tdma_board_ids': list(WORKFLOW_UIDS)}), encoding='utf-8')
    baseline.write_text(json.dumps([
        {'uid': uid, 'command': command, 'raw': uid}
        for uid in WORKFLOW_UIDS for command in WORKFLOW_BASELINE_COMMANDS
    ]), encoding='utf-8')
    tap.write_text(json.dumps({
        'passed': True,
        'boards': {uid: {'tap_request': [0, 0, 0] if index == 0 else [1, 2, 3]}
                   for index, uid in enumerate(WORKFLOW_UIDS)},
    }), encoding='utf-8')

    def declared(path):
        return {'path': path.relative_to(root).as_posix(),
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}

    data = {
        'schema': workflow_inputs.SCHEMA,
        'scenario': workflow_inputs.SCENARIO,
        'board_uids': list(WORKFLOW_UIDS),
        'adapter': {
            'compatibility_entrypoint': adapter.relative_to(root).as_posix(),
            'migrated_sources': {
                role: declared(path) for role, path in migrated.items()},
            'runtime_entrypoint': ancestor.relative_to(root).as_posix(),
            'runtime_closure': [declared(ancestor)],
        },
        'files': {
            'receipt': {'path': receipt.relative_to(root).as_posix(),
                        'validation': 'P3_SOURCE_FINGERPRINT'},
            'baseline': declared(baseline),
            'tap_baseline': declared(tap),
        },
    }
    manifest.write_text(json.dumps(data), encoding='utf-8')
    return SimpleNamespace(root=root, adapter=adapter, startup=startup, summary=summary,
                           monitor=monitor, parallel=parallel, late_scope=late_scope,
                           outer_trial=outer_trial, outer_hil=outer_hil,
                           legacy_scope=legacy_scope, run_output=run_output,
                           probe_support=probe_support, phase_timer1=phase_timer1,
                           migrated=migrated,
                           ancestor=ancestor, receipt=receipt, baseline=baseline,
                           tap=tap, manifest=manifest, data=data)


def good_sample(due):
    return dict(due_s=due, capture_complete=True,
                phase=dict(relative_ns=[[-100.0, 100.0], [0.0], [1.0]]))


def steady_sample(due, ns=0.0):
    return dict(due_s=due, capture_complete=True, phase=dict(relative_ns=[[ns], [0.0], [0.0]]))


def test_first_window_transient_is_graded_coarse_and_named_as_settling():
    rows = [steady_sample(due) for due in range(5, 121, 5)]
    rows[0]['phase']['relative_ns'] = [[61.986], [0.0], [0.0]]  # measured NO4 convergence point
    first = joint.scope_checkpoint(rows, 60)
    assert first['continue_run'] and not first['failures']
    assert first['settle_s'] == 5.0 and first['steady_threshold_ns'] == 50
    assert first['settling_windows'] == [5] and first['steady_windows'] == list(range(10, 61, 5))
    assert first['settling_exemptions'] == [{'due_s': 5, 'max_abs_ns': 61.986}]
    assert not first['steady_failures']
    # The same deviation one window later is a steady-state failure, not settling.
    rows[1]['phase']['relative_ns'] = [[50.001], [0.0], [0.0]]
    later = joint.scope_checkpoint(rows, 60)
    assert later['continue_run']  # Coarse fail-fast bound is unchanged.
    assert later['steady_failures'] == [{'due_s': 10, 'reason': 'phase_outside_50ns', 'max_abs_ns': 50.001}]
    # Only the settling window may use the allowance; window 10 is graded steady.
    assert later['settling_exemptions'] == [{'due_s': 5, 'max_abs_ns': 61.986}]
    # The coarse bound still applies during settling.
    rows[0]['phase']['relative_ns'] = [[100.001], [0.0], [0.0]]
    rows[1]['phase']['relative_ns'] = [[0.0], [0.0], [0.0]]
    assert not joint.scope_checkpoint(rows, 60)['continue_run']


def test_settling_allowance_is_explicit_and_cannot_be_assumed():
    rows = [steady_sample(due) for due in range(5, 121, 5)]
    rows[0]['phase']['relative_ns'] = [[61.986], [0.0], [0.0]]
    assert joint.scope_checkpoint(rows, 60, 0)['steady_failures'] == [
        {'due_s': 5, 'reason': 'phase_outside_50ns', 'max_abs_ns': 61.986}]
    assert joint.scope_checkpoint(rows, 60, 10)['settling_windows'] == [5, 10]
    duplicated = [steady_sample(due) for due in range(5, 121, 5)] + [steady_sample(10)]
    assert joint.scope_checkpoint(duplicated, 60)['failures'] == [{'reason': 'duplicate_schedule'}]


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
        [dict(checkpoint_s=60, continue_run=external, steady_failures=[])], 60, errors)
    assert result['passed'] is passed
    assert result['internal_health_passed'] is internal
    assert result['external_sampled_windows_passed'] is (external if enabled else None)
    assert not result['physical_continuous_lock_qualified']
    assert not result['exact_event_correlation_qualified']


def test_joint_verdict_cannot_pass_an_ungraded_steady_state_band():
    graded = [dict(checkpoint_s=60, continue_run=True, steady_failures=[])]
    assert joint.joint_verdict(True, True, graded, 60, [])['passed']
    assert joint.joint_verdict(True, True, graded, 60, [])['external_steady_state_passed'] is True
    # A checkpoint without the classification proves nothing about the tight band.
    ungraded = [dict(checkpoint_s=60, continue_run=True)]
    unproven = joint.joint_verdict(True, True, ungraded, 60, [])
    assert unproven['external_steady_state_passed'] is False
    assert unproven['external_sampled_windows_passed'] is True and not unproven['passed']
    deviating = [dict(checkpoint_s=60, continue_run=True,
                      steady_failures=[{'due_s': 10, 'reason': 'phase_outside_50ns', 'max_abs_ns': 55.0}])]
    assert not joint.joint_verdict(True, True, deviating, 60, [])['passed']
    exempted = [dict(checkpoint_s=60, continue_run=True,
                     settling_exemptions=[{'due_s': 5, 'max_abs_ns': 61.986}], steady_failures=[])]
    used = joint.joint_verdict(True, True, exempted, 60, [])
    assert used['passed'] and used['external_settling_exemptions'] == 1
    off = joint.joint_verdict(True, False, [], 60, [])
    assert off['external_steady_state_passed'] is None and off['external_settling_exemptions'] is None


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
            self.resource = 'MOCK'
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

    return Scope


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

        def enable_async_evidence(self):
            pass

        def fresh_snapshot(self, folder):
            events.append(('scope_capture', folder.name))
            if fault in ('capture', 'undrained') and folder.name == 'sample-0060':
                raise TimeoutError('new record never completed')
            if fault == 'overrun' and folder.name == 'sample-0005': clock.sleep(10.1)
            clock.sleep(.05)
            return dict(capture_complete=True, trigger_admitted_ns=clock.monotonic_ns()-100,
                        trigger_complete_observed_ns=clock.monotonic_ns(), elapsed_s=.05,
                        phase=dict(relative_ns=[[101 if fault == 'outside' else 0], [0], [0]]))

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

        def command(self, board, command, unused=None):
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
    opt = SimpleNamespace(external_scope=enabled, scope_trigger='CHAN1', scope_resource='MOCK',
                          scope_serial='MOCK123', seconds=60, out=tmp_path, bench_adapter=path,
                          workflow_inputs_evidence={'schema': 'TEST_WORKFLOW_INPUTS'})
    return SimpleNamespace(Probe=Probe, base=SimpleNamespace(external=external)), opt, events


def admit_fake_startup(probe):
    probe.report['startup_preflight'] = dict(passed=True)


def test_scope_off_never_constructs_scope_and_delegates_original_capture(tmp_path, clock, monkeypatch):
    monkeypatch.setattr(joint, 'verify_startup', admit_fake_startup)
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
    monkeypatch.setattr(joint, 'verify_startup', admit_fake_startup)
    adapter, opt, events = fake_adapter(tmp_path, clock, fault=fault, internal=internal)
    monkeypatch.setattr(joint, 'make_scope_class', lambda: adapter.base.external.SparseScope)
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
        assert verdict['external_steady_state_passed'] is True
        assert verdict['external_settling_exemptions'] == 0
        assert probe.report['plan']['scope_settle_s'] == 5.0
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
    assert options.scope_resource == joint.scope_reader.RESOURCE
    assert options.scope_serial == 'HDO4A244301137'
    options = joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path),
                                '--scope', 'on', '--scope-trigger', 'EXT'])
    assert options.scope == 'on' and options.scope_trigger == 'EXT'


@pytest.mark.parametrize('flag,value', [('--scope-resource', ''), ('--scope-serial', ' '),
                                        ('--scope-resource', 'USB\nBAD'), ('--scope-serial', 'X\rY')])
def test_scope_identity_rejected_before_adapter_import(tmp_path, flag, value):
    path = tmp_path / 'adapter.py'
    path.write_text('raise AssertionError("must not import")\n', encoding='utf-8')
    with pytest.raises(SystemExit):
        joint.parse_args(['--bench-adapter', str(path), '--out', str(tmp_path), flag, value])


@pytest.mark.parametrize('values', [('-2147483649', '0', '0', '0'),
                                   ('0', '0', '0', '2147483648'), ('0', '20', '0')])
def test_invalid_delays_rejected_before_hardware(tmp_path, values):
    adapter = tmp_path/'adapter.py'
    adapter.write_text('# fixture', encoding='utf-8')
    with pytest.raises(SystemExit):
        joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path),
                          '--output-delays', *values])


@pytest.mark.parametrize('value', ['-1', 'nan', 'inf', '61'])
def test_settle_allowance_is_validated_before_hardware(tmp_path, value):
    adapter = tmp_path/'adapter.py'
    adapter.write_text('# fixture', encoding='utf-8')
    with pytest.raises(SystemExit):
        joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path), '--settle-s', value])
    assert joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path)]).settle_s == 5.0
    assert joint.parse_args(['--bench-adapter', str(adapter), '--out', str(tmp_path),
                             '--settle-s', '0']).settle_s == 0.0


def test_workflow_manifest_pins_all_inputs_before_adapter_import(tmp_path):
    fixture = workflow_input_fixture(tmp_path)
    loaded = workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.adapter)
    assert loaded.board_uids == tuple(WORKFLOW_UIDS)
    assert loaded.bench_adapter == fixture.adapter
    assert loaded.migrated_sources == tuple(
        fixture.migrated[role] for role in workflow_inputs.MIGRATED_SOURCE_ROLES)
    assert loaded.runtime_entrypoint == fixture.ancestor
    assert loaded.executable_closure == (fixture.ancestor,)
    assert loaded.receipt == fixture.receipt
    assert loaded.baseline == fixture.baseline
    assert loaded.tap_baseline == fixture.tap
    evidence = loaded.evidence(fixture.root)
    assert evidence['scenario'] == 'internal_seal'
    assert evidence['files']['receipt']['validation'] == 'P3_SOURCE_FINGERPRINT'


def test_repository_workflow_manifest_matches_loader_schema():
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((
        root / 'config/tool_workflows/vdc_joint_capture_internal_seal.json'
    ).read_text(encoding='utf-8'))
    assert manifest['schema'] == workflow_inputs.SCHEMA
    assert set(manifest['adapter']['migrated_sources']) == set(
        workflow_inputs.MIGRATED_SOURCE_ROLES)


@pytest.mark.parametrize('name,attribute,error', [
    ('entrypoint', 'adapter', r'migrated source internal_seal sha256 mismatch'),
    ('startup', 'startup', r'migrated source quiet_startup sha256 mismatch'),
    ('summary', 'summary', r'migrated source internal_summary sha256 mismatch'),
    ('monitor', 'monitor', r'migrated source external_monitor sha256 mismatch'),
    ('ancestor', 'ancestor', r'runtime closure\[0\] sha256 mismatch'),
    ('baseline', 'baseline', 'baseline sha256 mismatch'),
    ('tap_baseline', 'tap', 'tap_baseline sha256 mismatch'),
])
def test_workflow_manifest_rejects_tampered_pinned_input(tmp_path, name, attribute, error):
    fixture = workflow_input_fixture(tmp_path)
    getattr(fixture, attribute).write_text(
        'tampered', encoding='utf-8')
    with pytest.raises(ValueError, match=error):
        workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.adapter)


def test_workflow_manifest_rejects_adapter_selection_and_uid_drift(tmp_path):
    fixture = workflow_input_fixture(tmp_path)
    other = fixture.root / 'out/other.py'
    other.write_text('# another adapter\n', encoding='utf-8')
    with pytest.raises(ValueError, match='selected bench adapter differs'):
        workflow_inputs.load_inputs(fixture.manifest, fixture.root, other)
    fixture.data['board_uids'].reverse()
    fixture.manifest.write_text(json.dumps(fixture.data), encoding='utf-8')
    with pytest.raises(ValueError, match='receipt board UID order differs'):
        workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.adapter)


def test_workflow_manifest_requires_fixed_migrated_lineage(tmp_path):
    fixture = workflow_input_fixture(tmp_path)
    fixture.data['adapter']['migrated_sources'].pop('quiet_startup')
    fixture.manifest.write_text(json.dumps(fixture.data), encoding='utf-8')
    with pytest.raises(ValueError, match='fixed internal-seal lineage'):
        workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.adapter)


def test_workflow_manifest_binds_compatibility_selector_to_internal_seal(tmp_path):
    fixture = workflow_input_fixture(tmp_path)
    fixture.data['adapter']['compatibility_entrypoint'] = (
        fixture.startup.relative_to(fixture.root).as_posix())
    fixture.manifest.write_text(json.dumps(fixture.data), encoding='utf-8')
    with pytest.raises(ValueError, match='must select migrated internal_seal'):
        workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.startup)


def test_workflow_manifest_rejects_path_escape_before_adapter_import(tmp_path):
    fixture = workflow_input_fixture(tmp_path)
    fixture.data['adapter']['migrated_sources']['internal_seal']['path'] = '../adapter.py'
    fixture.manifest.write_text(json.dumps(fixture.data), encoding='utf-8')
    with pytest.raises(ValueError, match='repository-relative'):
        workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.adapter)


def test_main_rejects_manifest_before_adapter_import(tmp_path, monkeypatch):
    fixture = workflow_input_fixture(tmp_path)
    fixture.adapter.write_text('tampered executable\n', encoding='utf-8')
    monkeypatch.setattr(joint, 'ROOT', fixture.root)
    monkeypatch.setattr(joint.sys, 'argv', [
        'capture', '--bench-adapter', str(fixture.adapter),
        '--workflow-inputs', str(fixture.manifest), '--out', str(tmp_path / 'run')])
    monkeypatch.setattr(joint, 'load_internal_seal_adapter',
                        lambda path, **kwargs: pytest.fail(
                            'tracked adapter loaded before manifest admission'))
    with pytest.raises(ValueError, match=r'migrated source internal_seal sha256 mismatch'):
        joint.main()


def test_main_rejects_stale_receipt_before_adapter_import(tmp_path, monkeypatch):
    fixture = workflow_input_fixture(tmp_path)
    monkeypatch.setattr(joint, 'ROOT', fixture.root)
    monkeypatch.setattr(joint.sys, 'argv', [
        'capture', '--bench-adapter', str(fixture.adapter),
        '--workflow-inputs', str(fixture.manifest), '--out', str(tmp_path / 'run')])
    monkeypatch.setattr(joint, 'validate_current_receipt',
                        lambda inputs, root: (_ for _ in ()).throw(
                            ValueError('current source does not match the P3 receipt')))
    monkeypatch.setattr(joint, 'load_internal_seal_adapter',
                        lambda path, **kwargs: pytest.fail(
                            'tracked adapter loaded before P3 admission'))
    with pytest.raises(ValueError, match='current source does not match'):
        joint.main()


def test_current_receipt_validation_binds_source_and_board_order(tmp_path, monkeypatch):
    from tools.hardware_acceptance import p3_hardware_acceptance as gate

    fixture = workflow_input_fixture(tmp_path)
    receipt = {'schema': gate.QUICK_GRADED_RECEIPT_SCHEMA,
               'tdma_board_ids': WORKFLOW_UIDS, 'source_tree_sha256': 'a' * 64,
               'source_file_count': 7}
    fixture.receipt.write_text(json.dumps(receipt), encoding='utf-8')
    loaded = workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.adapter)
    monkeypatch.setattr(gate, '_validate_quick_diagnostic_receipt', lambda root, value: None)
    monkeypatch.setattr(gate, 'working_source_fingerprint', lambda root: ('a' * 64, 7))
    assert workflow_inputs.validate_current_receipt(loaded, fixture.root) == receipt
    monkeypatch.setattr(gate, 'working_source_fingerprint', lambda root: ('b' * 64, 7))
    with pytest.raises(ValueError, match='current source does not match'):
        workflow_inputs.validate_current_receipt(loaded, fixture.root)
    receipt['schema'] = 'WRONG_RECEIPT_SCHEMA'
    fixture.receipt.write_text(json.dumps(receipt), encoding='utf-8')
    with pytest.raises(ValueError, match='receipt schema'):
        workflow_inputs.validate_current_receipt(loaded, fixture.root)


@pytest.mark.parametrize('target', ['baseline', 'tap'])
def test_workflow_manifest_rejects_malformed_nested_evidence(tmp_path, target):
    fixture = workflow_input_fixture(tmp_path)
    if target == 'baseline':
        value = json.loads(fixture.baseline.read_text(encoding='utf-8'))
        value[0].pop('raw')
        path = fixture.baseline
        key = 'baseline'
        error = 'baseline rows'
    else:
        value = json.loads(fixture.tap.read_text(encoding='utf-8'))
        value['boards'][WORKFLOW_UIDS[1]]['tap_request'] = '1,2,3'
        path = fixture.tap
        key = 'tap_baseline'
        error = 'tap_request'
    path.write_text(json.dumps(value), encoding='utf-8')
    fixture.data['files'][key]['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    fixture.manifest.write_text(json.dumps(fixture.data), encoding='utf-8')
    with pytest.raises(ValueError, match=error):
        workflow_inputs.load_inputs(fixture.manifest, fixture.root, fixture.adapter)


def test_explicit_delays_reach_stop_restore_runner(tmp_path, monkeypatch):
    fixture = workflow_input_fixture(tmp_path)
    monkeypatch.setattr(joint, 'ROOT', fixture.root)
    monkeypatch.setattr(joint, 'validate_current_receipt', lambda inputs, root: {
        'tdma_board_ids': list(inputs.board_uids), 'build_id': '1',
        'source_tree_sha256': 'source',
    })
    monkeypatch.setattr(joint.sys, 'argv', ['capture', '--bench-adapter', str(fixture.adapter),
                        '--workflow-inputs', str(fixture.manifest),
                        '--out', str(tmp_path), '--output-delays', '0', '-8', '-68', '-116',
                        '--scope-resource', 'USB::TEST', '--scope-serial', 'TEST123'])
    seen = {}

    class Runner:
        def __init__(self, *args, **kwargs):
            seen.update(kwargs)
            seen['child'] = args[4]
            self.report = {}

        def save(self):
            seen['saved_workflow_inputs'] = self.report.get('workflow_inputs')

        def run(self):
            return dict(passed=True, errors=[], cleanup_errors=[], restore_needed=False, duration_s=60)

    monkeypatch.setattr(joint, 'ResumeTrial', Runner)
    monkeypatch.setattr(joint, 'SerialBackend', lambda board_uids: ('backend', tuple(board_uids)))
    monkeypatch.setattr(joint, 'load_internal_seal_adapter',
                        lambda path, **kwargs: pytest.fail('outer lifecycle loaded child adapter'))
    assert joint.main() == 0
    assert seen['output_delays'] == (0, -8, -68, -116)
    assert seen['child'][seen['child'].index('--scope-resource') + 1] == 'USB::TEST'
    assert seen['child'][seen['child'].index('--scope-serial') + 1] == 'TEST123'
    assert seen['child'][seen['child'].index('--workflow-inputs') + 1] == str(fixture.manifest)
    assert (seen['saved_workflow_inputs']['adapter']['compatibility_entrypoint'] ==
            'out/adapter.py')


def test_child_uses_tracked_adapter_factory_with_runtime_entrypoint(tmp_path, monkeypatch):
    fixture = workflow_input_fixture(tmp_path)
    monkeypatch.setattr(joint, 'ROOT', fixture.root)
    monkeypatch.setattr(joint, 'validate_current_receipt', lambda inputs, root: {
        'tdma_board_ids': list(inputs.board_uids), 'build_id': '1',
        'source_tree_sha256': 'source',
    })
    monkeypatch.setattr(joint.sys, 'argv', [
        'capture', '--bench-adapter', str(fixture.adapter),
        '--workflow-inputs', str(fixture.manifest), '--out', str(tmp_path / 'child'),
        '--child'])
    seen = {}

    class Probe:
        def __init__(self, opt):
            seen['opt'] = opt
            self.report = {'joint_verdict': {'passed': True}, 'passed': True}

        def save(self):
            seen['saved'] = dict(self.report)

        def run(self):
            seen['ran'] = True

    adapter = SimpleNamespace(Probe=object, base=SimpleNamespace())
    monkeypatch.setattr(joint, 'load_internal_seal_adapter',
                        lambda path, **kwargs: (
                            seen.update(runtime_entrypoint=path, factory_kwargs=kwargs) or adapter))
    monkeypatch.setattr(joint, 'make_probe_class', lambda value: Probe)
    assert joint.main() == 0
    assert seen['runtime_entrypoint'] == fixture.ancestor
    assert seen['factory_kwargs']['board_uids'] == tuple(WORKFLOW_UIDS)
    assert seen['opt'].bench_adapter == fixture.adapter
    assert seen['opt'].workflow_inputs_evidence['adapter']['runtime_entrypoint'] == 'out/ancestor.py'
    assert seen['ran'] and seen['saved']['workflow_inputs']['schema'] == workflow_inputs.SCHEMA


def startup_fixture(tmp_path, monkeypatch):
    from tools.calibration_ring_validate import trn03_stage as stage
    uids = ['uid1', 'uid2', 'uid3', 'uid4']
    matrix = dict(node_count=4, node_ids_in_loop_order=uids,
                  evidence_flags=31, calibration_generation=11,
                  topology_generation=12, topology_crc32=13,
                  profile_crc32=14, schedule_crc32=15, links=[])
    for index in range(4):
        link = dict.fromkeys(stage.LINK_FIELDS, 1)
        link['link_index'] = index
        matrix['links'].append(link)
    data = json.dumps(matrix).encode()
    (tmp_path/'matrix.json').write_bytes(data)
    monkeypatch.setattr(joint, 'ROOT', tmp_path)
    calls = []
    responses = {}
    for index, uid in enumerate(uids):
        ring = [0] * 40
        # STOP clears runtime topology; the frozen matrix retains its identity.
        ring[38:] = [3, 3]
        header = dict(matrix, tag='TRN03STG', enabled=1, complete=1, valid_link_bitmap=15)
        r = responses[uid] = {
            '*IDN?': f'NO.{index+1},DHRT100,{uid},0.1.0',
            'SYSTem:BOARD:NO?': str(index+1), 'SYSTem:FW:BUILD?': '"123"',
            'SYSTem:TDMA:RING:STATus?': ','.join(map(str, ring)),
            'SYSTem:TDMA:FLIGHT:MODE?': '2',
            'READ:CALibration:TRAINing:STAGe?': ','.join(str(header[k]) for k in stage.STAGE_QUERY_FIELDS),
            'SYSTem:ERR?': '0,"No error"',
        }
        for li, link in enumerate(matrix['links']):
            observed = dict(header, **link, valid=1)
            observed['tag'] = 'TRN03LNK'
            observed.update(dict.fromkeys(stage.ORIGIN_CAPTURE_FIELDS, 0))
            r[f'READ:CALibration:TRAINing:STAGe:LINK? {li}'] = ','.join(
                str(observed[k]) for k in stage.LINK_QUERY_FIELDS)

    def command(board, cmd):
        assert '?' in cmd, 'Preflight must never write or ARM'
        calls.append((board.address, cmd))
        return responses[board.address][cmd]

    probe = SimpleNamespace(barrier=True, block_queries=False, report={}, save=lambda: None,
        receipt=dict(build_id='123', trn03_matrix=dict(path='matrix.json', sha256=hashlib.sha256(data).hexdigest())),
        boards=[SimpleNamespace(address=uid, port=f'COM{i}') for i, uid in enumerate(uids)], command=command)
    return probe, responses, calls


def test_startup_accepts_complete_stop_configuration(tmp_path, monkeypatch):
    probe, _, calls = startup_fixture(tmp_path, monkeypatch)
    joint.verify_startup(probe)
    assert probe.report['startup_preflight']['passed']
    assert len([c for _, c in calls if ':LINK?' in c]) == 16


@pytest.mark.parametrize('fault', ['empty', 'raw_mode', 'identity', 'duplicate_port',
                                  'unacked', 'link', 'fingerprint', 'running', 'error',
                                  'build', 'crc', 'generation', 'bitmap', 'delay', 'origin'])
def test_startup_rejects_regressions_before_arm(tmp_path, monkeypatch, fault):
    probe, responses, calls = startup_fixture(tmp_path, monkeypatch)
    r = responses['uid1']
    if fault == 'empty': r['READ:CALibration:TRAINing:STAGe?'] = '"EMPTY"'
    elif fault == 'raw_mode': r['SYSTem:TDMA:FLIGHT:MODE?'] = '1'
    elif fault == 'identity': r['*IDN?'] = 'NO.2,DHRT100,uid1,0.1.0'
    elif fault == 'duplicate_port': probe.boards[1].port = probe.boards[0].port
    elif fault == 'unacked': r['SYSTem:TDMA:RING:STATus?'] = r['SYSTem:TDMA:RING:STATus?'][:-1] + '2'
    elif fault == 'link': r['READ:CALibration:TRAINing:STAGe:LINK? 0'] = '"EMPTY"'
    elif fault == 'fingerprint': probe.receipt['trn03_matrix']['sha256'] = 'bad'
    elif fault == 'running': probe.barrier = False
    elif fault == 'error': r['SYSTem:ERR?'] = '-200,"Execution error"'
    elif fault == 'build': r['SYSTem:FW:BUILD?'] = '"124"'
    elif fault in ('crc', 'generation', 'bitmap', 'delay', 'origin'):
        from tools.calibration_ring_validate import trn03_stage as stage
        is_link = fault in ('delay', 'origin')
        cmd = 'READ:CALibration:TRAINing:STAGe' + (':LINK? 0' if is_link else '?')
        fields = stage.LINK_QUERY_FIELDS if is_link else stage.STAGE_QUERY_FIELDS
        field = dict(crc='profile_crc32', generation='calibration_generation', bitmap='valid_link_bitmap',
                     delay='loop_delay_cycles', origin=stage.ORIGIN_CAPTURE_FIELDS[0])[fault]
        values = r[cmd].split(',')
        values[fields.index(field)] = str(int(values[fields.index(field)]) + 1)
        r[cmd] = ','.join(values)
    with pytest.raises(RuntimeError, match='STARTUP_CONFIGURATION'):
        joint.verify_startup(probe)
    assert not probe.report['startup_preflight']['passed']
    assert probe.report['startup_preflight']['errors']
    assert all('?' in cmd for _, cmd in calls)


def test_failed_preflight_run_bypasses_initialization_hooks_and_old_capture(tmp_path, monkeypatch):
    events = []

    class LowLevel:
        def command(self, board, text, unused=None):
            events.append(text)
            return 'OK'

    class Adapter(LowLevel):
        def command(self, board, text, unused=None):
            if text == 'SESSION 0':
                events.append('PROVISIONAL_AND_PROFILE_INSTALL')
            return super().command(board, text, unused)

        def configure(self): events.append('ARM')
        def collect(self): events.append('READ_OLD_SRAM')
        def save(self): pass

        def run(self):
            try:
                self.configure()
                events.append('START')
            except Exception as exc:
                self.report['errors'].append(str(exc))
            finally:
                self.command('board', 'STOP')
                self.collect()
                self.command('board', 'SESSION 0')
                events.append('CLOSE')

    def reject(probe):
        probe.report['startup_preflight'] = dict(passed=False)
        raise RuntimeError('STARTUP_CONFIGURATION: EMPTY')

    monkeypatch.setattr(joint, 'verify_startup', reject)
    adapter = SimpleNamespace(Probe=Adapter, base=SimpleNamespace(external=None,
        follow=SimpleNamespace(parent=SimpleNamespace(base=SimpleNamespace(Probe=LowLevel)))))
    probe = joint.make_probe_class(adapter)()
    probe.report = dict(errors=[])
    probe.opt = SimpleNamespace(external_scope=True, seconds=60)
    probe.run()
    assert events == ['STOP', 'SESSION 0', 'CLOSE']
    assert not probe.report['joint_verdict']['passed']
    assert probe.report['collection_skipped']
