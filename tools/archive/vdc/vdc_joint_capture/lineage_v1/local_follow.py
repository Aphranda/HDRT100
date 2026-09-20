"""Silent four-board typed local frequency-control probe; STOP-only readback.

Reuses the source-bound receipt, matrix, UID, TAP and bounded owner-trial checks.
No calibration, OTA implementation change, NO5 operation, or live query.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('observer_probe',
    Path(__file__).resolve().parents[1] / 'capture_observer_dma_match.py')
observer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(observer)
match = observer.probe
parent = match.parent
FOLLOW = 'SYSTem:VDC:PRIORity:FOLLow'
STATUS = FOLLOW + ':STATus?'
U32 = ('schema active mode state last_reason request session generation calls tickets baselines waits '
       'prepared applied no_adjust rejected cancelled repeated baseline_sequence event_sequence '
       'carrier_sequence observer_epoch rx_epoch model_token expected_dco_seq before_dco_seq '
       'after_dco_seq local_slot reference_slot reserved').split()
I32 = 'before_ppb after_ppb selected_delta_ppb reserved_signed'.split()
U64 = 'arm_epoch baseline_raw raw_lo raw_hi interval_lo interval_hi local_interval_lo local_interval_hi'.split()
I64 = 'error_lo_ppb error_hi_ppb'.split()
MODEL = ('schema token session role_generation applied_command_seq clock_epoch_id clock_run_id '
         'local_slot valid_from_raw valid dco_update_seq source_model_seq epoch_id run_id '
         'base_local_tick64 base_vdc_time64_ns nominal_period_ns period_adjust_ppb phase_offset_ns '
         'slew_limit_ppb lock_state tdma_schedule_crc32 servo_profile_crc32').split()


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def parse_follow(raw):
    words = match.direct.numbers(raw, len(U32) + len(I32) + 2*(len(U64) + len(I64)))
    output = dict(zip(U32, words))
    pos = len(U32)
    for name in I32:
        output[name] = signed(words[pos], 32)
        pos += 1
    for name in U64 + I64:
        number = words[pos] | (words[pos+1] << 32)
        output[name] = signed(number, 64) if name in I64 else number
        pos += 2
    return output


def check_decision(s):
    assert s['prepared'] > 0 and s['applied'] + s['no_adjust'] > 0, s
    assert 0 < s['interval_lo'] <= s['interval_hi']
    assert 0 < s['local_interval_lo'] <= s['local_interval_hi']
    lo = s['local_interval_lo'] * 10**9 // s['interval_hi'] - 10**9
    hi = -(-s['local_interval_hi'] * 10**9 // s['interval_lo']) - 10**9
    assert [lo, hi] == [s['error_lo_ppb'], s['error_hi_ppb']], s
    step = -(lo // 4) if lo > 10 else ((-hi) // 4 if hi < -10 else 0)
    step = max(-1000, min(1000, step))
    assert step == s['selected_delta_ppb'], s
    assert s['event_sequence'] > s['baseline_sequence']
    if step:
        assert s['applied'] > 0 and s['after_dco_seq'] == s['before_dco_seq'] + 1
        assert s['after_ppb'] == s['before_ppb'] + step
    else:
        assert s['no_adjust'] > 0 and s['before_dco_seq'] == s['after_dco_seq']
        assert s['before_ppb'] == s['after_ppb']
    return {'independent_error_ppb': [lo, hi], 'independent_step_ppb': step,
            'actual_applied': bool(step), 'interpretation': 'bounded frequency decision, not phase lock'}


class FollowProbe(observer.ObserverProbe):
    def __init__(self, opt):
        super().__init__(opt)
        self.report['schema'] = 'TYPED_LOCAL_DCO_SHORT_PROBE_V1'
        self.report['tool_sources'].append(dict(path=str(Path(__file__)), sha256=parent.base.sha(Path(__file__))))
        self.report['plan'].update(dco_control=True, typed_local_follow=True,
                                  require_valid_decision_on_each_follower=True)
        parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[FOLLOW.upper()] = 1
        parent.scpi_serial.SCALAR_U32_QUERY_HEADERS.add(FOLLOW.upper() + '?')

    def control(self, board, text, expected='OK'):
        if text.upper() == parent.SYNC.upper() + ' 0':
            super().control(board, FOLLOW + ' 0', 0)
            assert int(self.command(board, FOLLOW + '?').strip('"')) == 0
        result = super().control(board, text, expected)
        if text.upper().startswith(parent.SYNC.upper() + ' ') and int(text.split()[-1]) > 0:
            if board.address == self.boards[0].address:
                for follower in self.boards[1:]:
                    row = self.report['boards'][follower.address]
                    row['follow_before_raw'] = self.command(follower, STATUS)
                    super().control(follower, FOLLOW + ' 1', 1)
                    assert int(self.command(follower, FOLLOW + '?').strip('"')) == 1
                    row['follow_enable_requested'] = True
                    self.save()
        return result

    def collect(self):
        assert self.barrier and not self.block_queries
        for board in self.boards:
            row = self.report['boards'][board.address]
            row['follow_raw'] = self.command(board, STATUS)
            self.save()
        super().collect()
        for slot, board in enumerate(self.boards[1:], 1):
            row = self.report['boards'][board.address]
            try:
                s = row['follow'] = parse_follow(row['follow_raw'])
                model_values = list(map(int, parent.fields(row['local_model_raw'])))
                assert len(model_values) == len(MODEL)
                model = row['follow_model'] = dict(zip(MODEL, model_values))
                assert s['schema'] == 1 and s['mode'] == 4
                assert s['session'] == s['generation'] == self.report['binding_generation']
                assert s['local_slot'] == slot and s['reference_slot'] == 0
                assert s['active'] == 0 and s['last_reason'] == 12
                row['decision_check'] = check_decision(s)
                assert model['dco_update_seq'] == s['after_dco_seq']
                assert model['period_adjust_ppb'] == s['after_ppb']
                assert model['session'] == s['session'] and model['local_slot'] == slot
                row['actual_dco_consistent'] = True
            except Exception as exc:
                self.error('follow-assessment:' + board.address, exc)
            self.save()
        self.report['all_followers_applied'] = all(
            self.report['boards'][b.address].get('follow', {}).get('applied', 0) > 0
            for b in self.boards[1:])
        self.report['passed'] = not self.report['errors']

    def run(self):
        super().run()
        self.report['conclusion_scope'] = ('Typed local frequency decision and actual DCO application only. '
            'A zero adjustment is valid only when the full interval intersects the deadband. '
            'No ACK, phase capture, physical output lock, 100 ns, WCET or one-lap deadline claim.')
        self.save()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--tap-baseline', type=Path, required=True)
    parser.add_argument('--check-only', action='store_true')
    options = parser.parse_args()
    if not 3 <= options.seconds <= 8:
        parser.error('seconds must be 3..8')
    probe = FollowProbe(options)
    probe.run()
    print(json.dumps(dict(passed=probe.report['passed'], errors=probe.report['errors'], out=str(options.out))))
    raise SystemExit(0 if probe.report['passed'] else 2)
