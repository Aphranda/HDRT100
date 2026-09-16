"""Known wiring may be reused; current identities and hardware gates may not."""
from copy import deepcopy
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest

from tools.hardware_acceptance import p3_hardware_acceptance as p3


IDS = ['n1', 'n2', 'n3', 'n4']


def measured():
    return {
        'measurement_domain': 'calibration',
        'measurement_phase': 'link_adjacency_and_ring_topology',
        'passed': True, 'anchor_id': IDS[0], 'ring_order': IDS,
        'node_map': [{'node': i, 'no': i + 1, 'address': uid} for i, uid in enumerate(IDS)],
        'node_discovery': {'committed': True}, 'error': '',
        'adjacency': {uid: [IDS[(i + 1) % 4]] for i, uid in enumerate(IDS)},
        'assignments': [{'address': uid, 'no': i + 1, 'readback': str(i + 1),
                         'passed': True} for i, uid in enumerate(IDS)],
        'boards': {uid: {'address': uid, 'build': 'old'} for uid in IDS},
    }


def freeze(tmp_path, value=None):
    source = tmp_path / 'previous.json'
    source.write_text(json.dumps(measured() if value is None else value), encoding='utf-8')
    return p3.freeze_known_topology(tmp_path, source, tmp_path / 'run', IDS, IDS[0])


def fake_transport(monkeypatch, *, missing=False, wrong_build=False, wrong_no=False,
                   fail=False, identity_changed=False):
    calls = []
    boards = {uid: SimpleNamespace(address=uid, build='new', port=f'COM{i}',
                                   idn=f'NO.{i+1},DHRT100,{uid},0.1.0')
              for i, uid in enumerate(IDS)}
    if missing:
        del boards[IDS[-1]]
    if wrong_build:
        boards[IDS[1]].build = 'old'

    def command(board, text, args):
        calls.append((board.address, text))
        assert text == 'SYSTem:BOARD:NO?'
        if fail:
            raise OSError('serial unavailable')
        if identity_changed:
            raise RuntimeError('identity changed while opening port')
        return '9' if wrong_no else str(IDS.index(board.address) + 1)

    monkeypatch.setitem(sys.modules, 'tdma_start_ring', SimpleNamespace(
        discover=lambda args: boards, board_command=command,
        close_persistent_connections=lambda: calls.append(('close', ''))))
    return calls


def verify(tmp_path, monkeypatch, **kwargs):
    evidence = freeze(tmp_path)
    calls = fake_transport(monkeypatch, **kwargs)
    summary = p3.verify_known_topology(tmp_path, evidence, IDS, 'new',
                                      p3.DEFAULT_ACCEPTANCE_TIMING)
    return summary, evidence, calls


def test_freeze_preserves_original_bytes_and_old_build(tmp_path):
    evidence = freeze(tmp_path)
    assert (tmp_path / evidence['path']).read_bytes() == (tmp_path / 'previous.json').read_bytes()
    assert json.loads((tmp_path / evidence['path']).read_text())['boards'][IDS[0]]['build'] == 'old'


@pytest.mark.parametrize('case', ['failed', 'order', 'anchor', 'adjacency', 'no', 'missing',
                                 'reused', 'error', 'node_map', 'uncommitted'])
def test_invalid_history_is_rejected_before_hardware(tmp_path, case):
    value = deepcopy(measured())
    if case == 'failed': value['passed'] = False
    if case == 'order': value['ring_order'] = list(reversed(IDS))
    if case == 'anchor': value['anchor_id'] = IDS[1]
    if case == 'adjacency': value['adjacency'][IDS[0]] = [IDS[2]]
    if case == 'no': value['assignments'][1]['readback'] = '1'
    if case == 'missing': del value['boards'][IDS[2]]
    if case == 'reused': value['mode'] = 'REUSED_KNOWN_TOPOLOGY'
    if case == 'error': value['error'] = 'measurement failed'
    if case == 'node_map': value['node_map'][0]['no'] = 4
    if case == 'uncommitted': value['node_discovery']['committed'] = False
    with pytest.raises(p3.AcceptanceError, match='topology'):
        freeze(tmp_path, value)


def test_current_readback_is_read_only_and_keeps_old_provenance(tmp_path, monkeypatch):
    summary, evidence, calls = verify(tmp_path, monkeypatch)
    assert summary['passed'] is True
    assert summary['mode'] == 'REUSED_KNOWN_TOPOLOGY'
    assert summary['remeasured'] is False
    assert summary['source_topology'] == evidence
    assert summary['build_id'] == 'new'
    assert calls == [(uid, 'SYSTem:BOARD:NO?') for uid in IDS] + [('close', '')]
    p3.validate_known_topology_readback(summary, IDS, 'new')


@pytest.mark.parametrize('fault', ['missing', 'wrong_build', 'wrong_no', 'fail', 'identity_changed'])
def test_current_identity_or_io_failure_is_retained_and_closed(tmp_path, monkeypatch, fault):
    summary, _, calls = verify(tmp_path, monkeypatch, **{fault: True})
    assert summary['passed'] is False
    assert summary['error']
    assert calls[-1] == ('close', '')
    with pytest.raises(p3.AcceptanceError):
        p3.validate_known_topology_readback(summary, IDS, 'new')


def test_reuse_evidence_rechecks_frozen_history_and_current_readback(tmp_path, monkeypatch):
    summary, evidence, _ = verify(tmp_path, monkeypatch)
    path = tmp_path / 'current.json'
    path.write_text(json.dumps(summary), encoding='utf-8')
    record = {'topology_summary': p3.evidence_entry(tmp_path, path),
              'topology_reuse_source': evidence, 'tdma_board_ids': IDS,
              'ota_board_ids': IDS, 'build_id': 'new',
              'acceptance_profile': 'QUICK_DIAGNOSTIC'}
    reset = tmp_path / 'reset.json'
    reset.write_text(json.dumps({'passed': True, 'board_ids': IDS, 'expected_build': 'new',
                                 'builds_after': {uid: 'new' for uid in IDS}}))
    record['initialization_reset'] = p3.evidence_entry(tmp_path, reset)
    p3.validate_topology_reuse_evidence(tmp_path, record)
    bad = deepcopy(record)
    reset.write_text(json.dumps({'passed': True, 'board_ids': IDS, 'expected_build': 'old',
                                 'builds_after': {uid: 'old' for uid in IDS}}))
    bad['initialization_reset'] = p3.evidence_entry(tmp_path, reset)
    with pytest.raises(p3.AcceptanceError, match='post-reset identity'):
        p3.validate_topology_reuse_evidence(tmp_path, bad)
    (tmp_path / evidence['path']).write_text('{}', encoding='utf-8')
    with pytest.raises(p3.AcceptanceError, match='digest changed'):
        p3.validate_topology_reuse_evidence(tmp_path, record)


@pytest.mark.parametrize('verb', ['run', 'resume'])
def test_reuse_cli_is_explicit(monkeypatch, verb):
    argv = ['p3', verb, '--tdma-only', '--reuse-topology', 'prior.json']
    if verb == 'resume': argv += ['--package', 'image.pkg', '--ota-summary', 'ota.json']
    monkeypatch.setattr(sys, 'argv', argv)
    assert p3.parse_args().reuse_topology == Path('prior.json')


def test_run_reuses_topology_then_reaches_fresh_calibration(tmp_path, monkeypatch):
    """Exercise the real run dispatcher, not only the identity helper."""
    config = p3.load_bench_config(p3.ROOT / p3.DEFAULT_CONFIG)
    config['p3_board_ids_in_physical_order'] = IDS
    config['topology_anchor_board_id'] = IDS[0]
    source = tmp_path / 'old.json'
    source.write_text(json.dumps(measured()), encoding='utf-8')
    package = tmp_path / 'build' / 'DHRT100_UPDATE.pkg'
    package.parent.mkdir()
    package.write_bytes(b'fake package')
    out = tmp_path / 'run'
    (out / 'ota-four-board-tdma').mkdir(parents=True)
    (out / 'ota-four-board-tdma' / 'summary.json').write_text('{}')
    calls = fake_transport(monkeypatch)
    steps = []
    class CalibrationReached(Exception):
        pass
    def step(command, root, log, **kwargs):
        script = Path(command[1]).name
        steps.append(script)
        assert script != 'calibration_ring_topology.py'
        if script == 'calibration_clk_train.py':
            raise CalibrationReached
        return 0
    monkeypatch.setattr(p3, 'load_bench_config', lambda _: config)
    monkeypatch.setattr(p3, 'working_source_fingerprint', lambda _: ('source', 1))
    monkeypatch.setattr(p3, '_run_step', step)
    monkeypatch.setattr(p3, '_read_package_build_id', lambda _: 'new')
    monkeypatch.setattr(p3, 'validate_ota', lambda *args: None)
    monkeypatch.setattr(p3, 'reset_acceptance_boards', lambda *args: {'elapsed_s': 0})
    args = SimpleNamespace(root=tmp_path, config=p3.DEFAULT_CONFIG, receipt=Path('receipt.json'),
                           command='run', full=False, tdma_only=True,
                           diagnostic_continue=False, out_dir=out,
                           reuse_topology=source, build_dir=package.parent)
    with pytest.raises(CalibrationReached):
        p3.run_acceptance(args)
    assert steps == ['cmake_build_auto.py', 'ota_multi_update.py', 'calibration_clk_train.py']
    assert len([row for row in calls if row[1] == 'SYSTem:BOARD:NO?']) == 4
    summary = json.loads((out / 'p0t-topology' / 'summary.json').read_text())
    assert summary['passed'] is True and summary['remeasured'] is False
