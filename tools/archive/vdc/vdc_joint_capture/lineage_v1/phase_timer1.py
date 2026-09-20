"""Finite phase/frequency run. Export only after four-board STOP barrier.

The SRAM trace is a complete control prefix, not necessarily the full run.
No last-frequency-decision equals final-DCO assumption is used after phase steps.
"""
import argparse
import json
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parents[1].parent/'20260917/dpll-local-phase-r1'
sys.path.insert(0, str(HERE.parent / 'dpll-longwindow-adoption-r1'))
import capture_longwindow as previous
parent = previous.parent
follow = parent.follow
trace = parent.trace
PREFIX = parent.PREFIX
PHASE = follow.FOLLOW + ':PHASe'
U32 = ('schema enabled active state reason request session generation calls prepared attempted '
       'committed applied held rejected cancelled event_sequence carrier_sequence source_slot '
       'target_slot before_model after_model before_dco after_dco rate_epoch reserved').split()
I64 = 'delta_ns cumulative_ns residual_lo residual_hi'.split()
U64 = ('before_base_vdc after_base_vdc raw_lo raw_hi local_lo local_hi expected_lo expected_hi '
       'valid_from_raw').split()


def parse_phase(raw):
    words = follow.match.direct.numbers(raw, 52)
    result = dict(zip(U32, words))
    pos = len(U32)
    for name in I64 + U64:
        value = words[pos] | (words[pos+1] << 32)
        result[name] = follow.signed(value, 64) if name in I64 else value
        pos += 2
    return result


class PhaseProbe(parent.TraceProbe):
    def __init__(self, options):
        super().__init__(options)
        self.report['schema'] = 'DPLL_LOCAL_PHASE_NATIVE_PREFIX_V1'
        self.report['plan'].update(runtime_queries=False, phase_enabled=True,
            run_seconds=11, native_schema=6, origin_native_schema=5, timebase='TIMER1_NS', capture_scope='complete finite prefix',
            trace_matches_min=2, trace_match_span_min_s=None,
            require_phase_on_each_follower=True, physical_lock_qualified=False,
            frequency_normalization_lineage_proven=False)
        self.report['tool_sources'].append(dict(path=str(Path(__file__)),
            sha256=follow.parent.base.sha(Path(__file__))))
        self.report['tool_sources'].append(dict(path=str(Path(previous.__file__)),
            sha256=follow.parent.base.sha(Path(previous.__file__))))
        for header in (PHASE, PREFIX+'PHASe', PREFIX+'ORIGin'):
            follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[header.upper()] = 1
        follow.parent.scpi_serial.SCALAR_ONE_QUERY_HEADERS.add(PHASE.upper()+'?')

    def control(self, board, text, expected='OK'):
        # Parent sends trace ARM through super(), so arm the phase mode before
        # its SYNC-triggered follower setup, then use our explicit ARM sequence.
        if text.upper() == follow.parent.SYNC.upper()+' 0':
            follow.FollowProbe.control(self, board, PHASE+' 0', 0)
        result = follow.FollowProbe.control(self, board, text, expected)
        if (text.upper().startswith(follow.parent.SYNC.upper()+' ') and
                int(text.split()[-1]) > 0 and board == self.boards[0]):
            for index, participant in enumerate(self.boards):
                row = self.report['boards'][participant.address]
                if index:
                    follow.FollowProbe.control(self, participant, PHASE+' 1', 1)
                    assert int(self.command(participant, PHASE+'?').strip('"')) == 1
                before = row['trace_before'] = self.trace_status(participant)
                capture_id = max(self.report['binding_generation'], before['capture_id']+1)
                row['trace_capture_id'] = capture_id
                row['trace_arm_requested'] = True
                self.save()
                follow.FollowProbe.control(self, participant,
                    f'{PREFIX}{"PHASe" if index else "ORIGin"} {capture_id}', capture_id)
                row['trace_arm_ack'] = self.trace_ack(participant, 1, 1, capture_id)
                status = row['trace_arm_ack']['status']
                assert status['schema'] == (6 if index else 5)
                assert status['record_count'] == 0
                assert status['session'] == status['generation'] == self.report['binding_generation']
                self.save()
        return result

    acquire = previous.LongWindowProbe.acquire

    def collect(self):
        assert self.barrier and not self.block_queries
        for index, board in enumerate(self.boards):
            row = self.report['boards'][board.address]
            try:
                if index:
                    row['follow_raw'] = self.command(board, follow.STATUS)
                    row['phase_raw'] = self.command(board, PHASE+':STATus?')
                    row['phase'] = parse_phase(row['phase_raw'])
                self.control(board, PREFIX+'STOP', 1)
                capture_id = row['trace_capture_id']
                row['trace_stop_ack'] = self.trace_ack(board, 2, 3, capture_id)
                data, pages = trace.download_capture(lambda q: self.command(board, q), capture_id)
                path = self.opt.out / f'{board.address}-native.ram.bin'
                path.write_bytes(data)
                decoded = trace.decode(data, capture_id)
                decoded_path = path.with_suffix('.json')
                decoded_path.write_text(json.dumps(decoded, indent=2)+'\n', encoding='utf-8')
                row['trace_export'] = dict(path=str(path), sha256=follow.parent.base.sha(path),
                    bytes=len(data), decoded=str(decoded_path), file_crc32=decoded['file_crc32'], pages=pages)
                row['trace_read_complete'] = True
                status = decoded['status']
                assert status['schema'] == (6 if index else 5)
                assert status['session'] == status['generation'] == self.report['binding_generation']
                assert status['local_slot'] == index and status['reason'] in (1,4)
                assert status['dropped_count'] == (1 if index and status['reason'] == 4 else 0)
                if index:
                    phases = [r for r in decoded['records'] if r['kind'] == 4]
                    decisions = [r for r in decoded['records'] if r['kind'] == 2]
                    matches = [r for r in decoded['records'] if r['kind'] == 1]
                    assert phases and decisions and len(matches) >= 2
                    assert any(d['index'] > phases[0]['index'] for d in decisions)
                    row['native_phase_count'] = len(phases)
                    row['native_decision_count'] = len(decisions)
                    row['native_phase_deltas_ns'] = [r['delta_ns'] for r in phases]
                    row['native_residual_bounds'] = [[r['residual_lo'],r['residual_hi']] for r in matches]
                    row['phase_prefix_confirmed'] = True
                else:
                    assert decoded['replay_matches_encoded'] and len(decoded['records']) >= 8
            except Exception as exc:
                self.error('phase-native:'+board.address, exc)
            self.save()
        try:
            # Keep all transport/exact-match/UID/model checks; legacy rate-only
            # final-DCO equality is inapplicable once phase changes the model.
            follow.observer.ObserverProbe.collect(self)
            for index, board in enumerate(self.boards[1:],1):
                row = self.report['boards'][board.address]
                try:
                    phase = row['phase']
                    f = row['follow'] = follow.parse_follow(row['follow_raw'])
                    values = list(map(int, follow.parent.fields(row['local_model_raw'])))
                    assert len(values) == len(follow.MODEL)
                    model = row['follow_model'] = dict(zip(follow.MODEL, values))
                    assert f['schema'] == 1 and f['mode'] == 4
                    assert f['session'] == f['generation'] == self.report['binding_generation']
                    assert f['local_slot'] == index and f['reference_slot'] == 0
                    assert f['active'] == 0 and f['last_reason'] == 12
                    assert phase['session'] == phase['generation'] == self.report['binding_generation']
                    assert phase['source_slot'] == 0 and phase['target_slot'] == index
                    assert phase['applied'] > 0 and phase['committed'] == phase['applied']
                    assert phase['active'] == 0
                    assert f['applied']+f['no_adjust'] > 0
                    assert model['session'] == phase['session'] and model['local_slot'] == index
                    phase_seq = phase['after_dco'] or phase['before_dco']
                    final_seq = max(phase_seq, f['after_dco_seq'])
                    assert model['dco_update_seq'] == final_seq
                    assert model['period_adjust_ppb'] == f['after_ppb']
                    if final_seq == phase_seq:
                        assert model['base_vdc_time64_ns'] == phase['after_base_vdc']
                        assert model['token'] == (phase['after_model'] or phase['before_model'])
                    row['phase_frequency_endpoint_consistent'] = True
                except Exception as exc:
                    self.error('phase-endpoint:'+board.address, exc)
                self.save()
        finally:
            for board in self.boards:
                row = self.report['boards'][board.address]
                if not row.get('trace_read_complete'):
                    row['trace_retained_for_recovery'] = bool(row.get('trace_arm_requested'))
                    continue
                try:
                    self.control(board, PREFIX+'RELease', 1)
                    row['trace_release_ack'] = self.trace_ack(board, 3, 0)
                except Exception as exc:
                    self.error('phase-release:'+board.address, exc)
            self.report['passed'] = not self.report['errors']
            self.save()

    def run(self):
        super().run()
        self.report['conclusion_scope'] = ('Actual confirmed local phase updates plus continued frequency '
            'decisions and STOP endpoint. Native capture is a finite complete prefix. '
            'No GPIO, 100 ns, complete frequency normalization lineage or lock qualification.')
        self.save()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--baseline',type=Path)
    parser.add_argument('--tap-baseline',type=Path,required=True)
    parser.add_argument('--check-only',action='store_true')
    opt=parser.parse_args();opt.seconds=11
    probe=PhaseProbe(opt);probe.run()
    print(json.dumps(dict(passed=probe.report['passed'],errors=probe.report['errors'],out=str(opt.out))))
    raise SystemExit(0 if probe.report['passed'] else 2)
