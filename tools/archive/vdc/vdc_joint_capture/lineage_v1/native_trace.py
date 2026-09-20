"""Eight-second typed trace probe, no output pulses and no RUN queries."""
import argparse
import json
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'dpll-priority-follow-r1'))
import capture_follow as follow
from tools.vdc_priority_trace import vdc_priority_trace as trace

PREFIX = 'SYSTem:VDC:PRIORity:TRACe:'


class TraceProbe(follow.FollowProbe):
    def __init__(self, opt):
        super().__init__(opt)
        self.report['schema'] = 'TYPED_TRACE_EIGHT_SECOND_PROBE_V1'
        self.report['plan'].update(native_typed_trace=True, diagnostic_gpio_output=False,
                                   trace_match_span_min_s=6, trace_matches_min=20)
        self.report['tool_sources'].extend(dict(path=str(p), sha256=follow.parent.base.sha(p)) for p in (
            Path(__file__), Path(trace.__file__), HERE.parent/'capture_observer_dma_match.py'))
        follow.parent.scpi_serial.SCALAR_ONE_QUERY_HEADERS.add(follow.FOLLOW.upper()+'?')
        for suffix in ('ARM', 'STOP', 'RELease'):
            follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[(PREFIX+suffix).upper()] = 1

    def trace_status(self, board):
        return trace.parse_status(self.command(board, PREFIX+'STATus?'))

    def trace_ack(self, board, command, state, capture_id=None):
        end = time.monotonic()+2
        samples = []
        while time.monotonic() < end:
            status = self.trace_status(board)
            samples.append(status)
            if (status['request_seq'] == status['ack_seq'] != 0 and
                status['command'] == command and status['state'] == state and
                (capture_id is None or status['capture_id'] == capture_id)):
                return dict(status=status, samples=samples)
            time.sleep(.01)
        raise RuntimeError('Trace request not acknowledged: '+json.dumps(samples))

    def control(self, board, text, expected='OK'):
        result = super().control(board, text, expected)
        if (text.upper().startswith(follow.parent.SYNC.upper()+' ') and
            int(text.split()[-1]) > 0 and board.address == self.boards[0].address):
            for follower in self.boards[1:]:
                row = self.report['boards'][follower.address]
                previous = row['trace_before'] = self.trace_status(follower)
                capture_id = max(self.report['binding_generation'], previous['capture_id']+1)
                row['trace_capture_id'] = capture_id
                row['trace_arm_requested'] = True
                self.save()
                super().control(follower, f'{PREFIX}ARM {capture_id}', capture_id)
                row['trace_arm_ack'] = self.trace_ack(follower, 1, 1, capture_id)
                self.save()
        return result

    def collect(self):
        assert self.barrier and not self.block_queries
        for board in self.boards[1:]:
            row = self.report['boards'][board.address]
            if not row.get('trace_arm_requested'):
                continue
            try:
                self.control(board, PREFIX+'STOP', 1)
                capture_id = row['trace_capture_id']
                row['trace_stop_ack'] = self.trace_ack(board, 2, 3, capture_id)
                self.save()
                data, pages = trace.download_capture(lambda q: self.command(board, q), capture_id)
                path = self.opt.out / f'{board.address}-typed.ram.bin'
                path.write_bytes(data)
                decoded = trace.decode(data, capture_id)
                decoded_path = path.with_suffix('.json')
                decoded_path.write_text(json.dumps(decoded, indent=2)+'\n')
                row['trace_export'] = dict(path=str(path), sha256=follow.parent.base.sha(path),
                    bytes=len(data), decoded=str(decoded_path), file_crc32=decoded['file_crc32'], pages=pages)
                row['trace_read_complete'] = True
                s = decoded['status']
                assert s['session'] == s['generation'] == self.report['binding_generation']
                assert s['local_slot'] == self.boards.index(board) and s['reference_slot'] == 0
                assert s['reason'] == 1 and s['dropped_count'] == 0
                matches = [r for r in decoded['records'] if r['kind'] == 1]
                decisions = [r for r in decoded['records'] if r['kind'] == 2]
                assert len(matches) >= 20 and len(decisions) >= 1
                span = (matches[-1]['raw_lo'] - matches[0]['raw_hi']) / s['tick_hz']
                row['trace_match_span_s'] = span
                assert span >= 6
                row['trace_decisions'] = decisions
            except Exception as exc:
                self.error('typed-trace:'+board.address, exc)
            self.save()
        try:
            super().collect()
            for board in self.boards[1:]:
                row = self.report['boards'][board.address]
                try:
                    last = row['trace_decisions'][-1]
                    f = row['follow']
                    keys = {'event_sequence':'event_sequence', 'baseline_sequence':'baseline_sequence',
                            'before_seq':'before_dco_seq', 'after_seq':'after_dco_seq',
                            'before_ppb':'before_ppb', 'after_ppb':'after_ppb', 'delta_ppb':'selected_delta_ppb',
                            'error_lo_ppb':'error_lo_ppb', 'error_hi_ppb':'error_hi_ppb'}
                    assert all(last[k] == f[v] for k,v in keys.items())
                    row['trace_last_decision_consistent'] = True
                except Exception as exc:
                    self.error('trace-last-decision:'+board.address, exc)
        finally:
            for board in self.boards[1:]:
                row = self.report['boards'][board.address]
                if not row.get('trace_read_complete'):
                    row['trace_retained_for_recovery'] = bool(row.get('trace_arm_requested'))
                    continue
                try:
                    self.control(board, PREFIX+'RELease', 1)
                    row['trace_release_ack'] = self.trace_ack(board, 3, 0)
                except Exception as exc:
                    self.error('trace-release:'+board.address, exc)
            self.report['passed'] = not self.report['errors']
            self.save()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--tap-baseline', type=Path, required=True)
    parser.add_argument('--check-only', action='store_true')
    opt = parser.parse_args()
    opt.seconds = 8
    probe = TraceProbe(opt)
    probe.run()
    print(json.dumps(dict(passed=probe.report['passed'], errors=probe.report['errors'], out=str(opt.out))))
    raise SystemExit(0 if probe.report['passed'] else 2)
