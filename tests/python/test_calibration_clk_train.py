"""Maintenance interleavings must not arm an old or unapplied topology."""
from argparse import Namespace
from collections import deque
import json

import pytest

from tools.calibration_ring_validate import calibration_clk_train as coarse
from tools.tdma_ring_monitor.tdma_start_ring import Board


def options():
    return Namespace(arm_wait=1.,idle_poll_interval=.01,gap=0.)


def runtime(*, started=0, requested=7, applied=7, node_count=2, slot=0):
    values = dict.fromkeys(coarse.RUNTIME_FIELDS,0)
    values.update(ring_enabled=started,ring_adapter_started=started,
                  ring_config_seq=requested,ring_applied_config_seq=applied,
                  ring_node_count=node_count,ring_local_slot_id=slot)
    return ','.join(str(values[k]) for k in coarse.RUNTIME_FIELDS)


def test_stop_waits_for_owner_applied_generation(monkeypatch):
    responses = deque([runtime(applied=6),'"UNAVAILABLE"',runtime()])
    monkeypatch.setattr(coarse,'board_command',lambda *args:responses.popleft())
    monkeypatch.setattr(coarse.time,'sleep',lambda _:None)
    result = coarse.wait_ring_stopped(Board('P','A','',''),options())
    assert result['ring_applied_config_seq'] == 7
    assert not responses


def test_late_stop_readback_cannot_pass_deadline(monkeypatch):
    now = [0.]
    monkeypatch.setattr(coarse.time,'monotonic',lambda:now[0])
    def command(*args):
        now[0] = 2.
        return runtime()
    monkeypatch.setattr(coarse,'board_command',command)
    with pytest.raises(RuntimeError,match='deadline'):
        coarse.wait_ring_stopped(Board('P','A','',''),options())


def test_started_wait_rejects_old_topology_and_unapplied_generation(monkeypatch):
    responses = deque([runtime(started=1,node_count=2),
                       runtime(started=1,node_count=4,applied=6),
                       runtime(started=1,node_count=4)])
    monkeypatch.setattr(coarse,'board_command',lambda *args:responses.popleft())
    monkeypatch.setattr(coarse.time,'sleep',lambda _:None)
    result = coarse._wait_ring_state(Board('P','A','',''),options(),started=True,topology=(4,0,0))
    assert result['ring_node_count'] == 4 and not responses


@pytest.mark.parametrize('failure',['<timeout>','2,1,0','OK(no payload; verified by state readback)'])
def test_topology_failure_preserves_partial_actions_and_prevents_arm(monkeypatch,failure):
    boards = [Board('P0','A','',''),Board('P1','B','','')]
    commands = []
    def command(board,text,args):
        commands.append((board.address,text))
        if 'TOPology ' in text:
            return failure if board.address == 'A' else '2,1,0'
        return 'OK'
    monkeypatch.setattr(coarse,'board_command',command)
    monkeypatch.setattr(coarse,'wait_calibration_idle',lambda *args:{})
    monkeypatch.setattr(coarse,'wait_ring_stopped',lambda *args:{})
    actions = []
    with pytest.raises(RuntimeError,match='TOPology'):
        coarse.arm_training_persona(boards,0,options(),actions)
    assert not any(text == 'SYSTem:TDMA:RING:ARM' for _,text in commands)
    assert any(row.get('response') == failure and 'error' in row for row in actions)


def test_prepare_failure_is_written_and_all_boards_are_cleaned(monkeypatch,tmp_path):
    boards = [Board('P0','A','','BUILD'),Board('P1','B','','BUILD')]
    args = Namespace(**vars(options()),board_id=['A','B'],expected_build='BUILD',level=7,
        pulse_start=10,pulse_limit=100,growth_factor=10,repeats=1,binary_refine=True,
        short_open=False,probe_phase_cycles=2,dry_run=False,out_dir=tmp_path)
    commands = []
    def command(board,text,_args):
        commands.append((board.address,text))
        if text == 'CALibration:TOPology:PROBe 1,2':
            return '1,2'
        if 'OPMode:STAGe' in text:
            return '7,10000000,1000000,4096,0,123'
        if text == 'SYSTem:TDMA:OPMode?':
            return '7,10000000,1000000,4096,0,123,7,10000000,1000000,4096,0,123,1,1,0,0'
        if text == 'SYSTem:ERRor?':
            return '0,"No error"'
        if text == 'SYSTem:TDMA:OPMode:APPLy':
            return '<timeout>'
        if text == 'CALibration:TOPology:PROBe 0':
            return '0,0'
        return 'OK'
    monkeypatch.setattr(coarse,'parse_args',lambda:args)
    monkeypatch.setattr(coarse,'discover',lambda _: {b.address:b for b in boards})
    monkeypatch.setattr(coarse,'board_command',command)
    monkeypatch.setattr(coarse,'wait_ring_stopped',lambda *args:{
        'ring_config_seq':7,'ring_applied_config_seq':7,'ring_enabled':0,'ring_adapter_started':0})
    assert coarse.main() == 1
    report = json.loads((tmp_path/'summary.json').read_text(encoding='utf-8'))
    assert not report['passed'] and 'APPLy' in report['error']
    assert any(row.get('response') == '<timeout>' for row in report['preparation_actions'])
    assert {uid for uid,text in commands if text == 'CALibration:TOPology:PROBe 0'} == {'A','B'}
    assert not any(text == 'SYSTem:TDMA:RING:ARM' for _,text in commands)
    assert not report['cleanup_errors']


@pytest.mark.parametrize('error_reply', ['-200,"TDMA_RING_TOPOLOGY"', '0,"No error"'])
def test_failed_control_retains_error_queue_without_retry_or_promotion(monkeypatch, error_reply):
    calls = []
    def command(_board, text, _args):
        calls.append(text)
        return error_reply if text == 'SYSTem:ERRor?' else '<timeout>'
    monkeypatch.setattr(coarse, 'board_command', command)
    actions = []
    with pytest.raises(RuntimeError, match='TOPology'):
        coarse._control_command(Board('P','A','',''), 'SYSTem:TDMA:RING:TOPology 2,0,0',
                                options(), actions, expected=(2,0,0))
    assert calls == ['SYSTem:TDMA:RING:TOPology 2,0,0', 'SYSTem:ERRor?']
    assert actions[0]['response'] == '<timeout>' and actions[0]['error_after'] == error_reply
    assert actions[0]['elapsed_s'] >= 0 and actions[0]['started_at']


def test_failed_error_readback_does_not_hide_original_control_failure(monkeypatch):
    def command(_board, text, _args):
        if text == 'SYSTem:ERRor?': raise OSError('disconnected')
        return '<timeout>'
    monkeypatch.setattr(coarse, 'board_command', command)
    actions = []
    with pytest.raises(RuntimeError, match='ACK missing'):
        coarse._control_command(Board('P','A','',''), 'SYSTem:TDMA:RING:STOP', options(), actions, ack=True)
    assert actions[0]['response'] == '<timeout>'
    assert actions[0]['error_readback_failure'] == 'OSError: disconnected'


def test_transient_topology_rejection_rechecks_stop_and_applies_before_arm(monkeypatch):
    boards = [Board('P0', 'A', '', ''), Board('P1', 'B', '', '')]
    events = []
    attempts = {'A': 0, 'B': 0}

    def command(board, text, _args):
        events.append((board.address, text))
        if 'TOPology ' in text:
            attempts[board.address] += 1
            if board.address == 'A' and attempts['A'] == 1:
                return '<timeout>'
            return text.split(' ', 1)[1]
        if text == 'SYSTem:ERRor?':
            return '-200,"Execution error"'
        if text in {'SYSTem:TDMA:RING:ARM:STATus?', 'SYSTem:TDMA:RING:TRAIN 1'}:
            return '1'
        return 'OK'

    def state(board, _args, *, started, topology=None, after_config_seq=None):
        events.append((board.address, 'STATE', started, topology, after_config_seq))
        node = boards.index(board)
        return dict(ring_node_count=2, ring_local_slot_id=node,
                    ring_reference_slot_id=0, ring_enabled=int(started),
                    ring_adapter_started=int(started),
                    ring_config_seq=8 if after_config_seq is None else 9,
                    ring_applied_config_seq=8 if after_config_seq is None else 9)

    monkeypatch.setattr(coarse, 'board_command', command)
    monkeypatch.setattr(coarse, '_wait_ring_state', state)
    monkeypatch.setattr(coarse, 'wait_calibration_idle', lambda *args: {})
    monkeypatch.setattr(coarse, 'train_status', lambda *args: {'request_seq': 0})
    monkeypatch.setattr(coarse, 'wait_train', lambda *args: {
        'state': coarse.STATE_FORWARDING, 'result': coarse.RESULT_FORWARD_ARMED})
    actions = []
    coarse.arm_training_persona(boards, 0, options(), actions)
    assert attempts == {'A': 2, 'B': 1}
    a_topology = [i for i, event in enumerate(events)
                  if event == ('A', 'SYSTem:TDMA:RING:TOPology 2,0,0')]
    assert any(event[:3] == ('A', 'STATE', False)
               for event in events[a_topology[0] + 1:a_topology[1]])
    first_arm = next(i for i, event in enumerate(events)
                     if event[1] == 'SYSTem:TDMA:RING:ARM')
    for node, board in enumerate(boards):
        assert (board.address, 'STATE', False, None, 8) in events[:first_arm]
    assert any(row.get('response') == '<timeout>' and
               row.get('error_after') == '-200,"Execution error"'
               for row in actions)


def test_topology_apply_wait_requires_new_acknowledged_generation(monkeypatch):
    responses = deque([runtime(), runtime(requested=8, applied=7),
                       runtime(requested=8, applied=8)])
    monkeypatch.setattr(coarse, 'board_command', lambda *args: responses.popleft())
    monkeypatch.setattr(coarse.time, 'sleep', lambda _: None)
    result = coarse._wait_ring_state(Board('P', 'A', '', ''), options(),
                                    started=False, after_config_seq=7)
    assert result['ring_applied_config_seq'] == 8 and not responses


@pytest.mark.parametrize('reply,error_reply,expected_attempts', [
    ('<timeout>', '-200,"Execution error"', coarse.TOPOLOGY_ATTEMPT_LIMIT),
    ('<timeout>', '0,"No error"', 1),
    ('<timeout>', '-222,"Data out of range"', 1),
    ('2,1,0', '-200,"Execution error"', 1),
])
def test_topology_retries_are_bounded_and_only_for_owner_refusal(
        monkeypatch, reply, error_reply, expected_attempts):
    commands = []
    def command(_board, text, _args):
        commands.append(text)
        return error_reply if text == 'SYSTem:ERRor?' else reply
    monkeypatch.setattr(coarse, 'board_command', command)
    monkeypatch.setattr(coarse, 'wait_ring_stopped', lambda *args: {
        'ring_config_seq': 7, 'ring_applied_config_seq': 7})
    actions = []
    with pytest.raises(RuntimeError, match='TOPology'):
        coarse._set_stopped_topology(Board('P', 'A', '', ''), (2, 0, 0),
                                     options(), actions)
    assert commands.count('SYSTem:TDMA:RING:TOPology 2,0,0') == expected_attempts
    assert len([row for row in actions if 'error' in row]) == expected_attempts
    assert not any(row['command'] == 'TOPOLOGY_APPLIED' for row in actions)


def test_topology_does_not_retry_when_fresh_stop_proof_fails(monkeypatch):
    commands = []
    stop_proofs = []
    def stopped(*args):
        stop_proofs.append(True)
        if len(stop_proofs) > 1:
            raise RuntimeError('STOP not acknowledged')
        return {'ring_config_seq': 7, 'ring_applied_config_seq': 7}
    def command(_board, text, _args):
        commands.append(text)
        return '-200,"Execution error"' if text == 'SYSTem:ERRor?' else '<timeout>'
    monkeypatch.setattr(coarse, 'board_command', command)
    monkeypatch.setattr(coarse, 'wait_ring_stopped', stopped)
    actions = []
    with pytest.raises(RuntimeError, match='STOP not acknowledged'):
        coarse._set_stopped_topology(Board('P', 'A', '', ''), (2, 0, 0),
                                     options(), actions)
    assert commands.count('SYSTem:TDMA:RING:TOPology 2,0,0') == 1
    assert len(stop_proofs) == 2
