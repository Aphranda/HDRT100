"""Eleven-second quiet typed native capture; only the caller operates hardware.

Inherit TraceProbe's existing source/UID/build/TAP/transport/STOP/CRC/coverage
checks. Longer estimator thresholds are observations, never a lock requirement.
"""
import argparse
from fractions import Fraction
import json
import math
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'dpll-priority-trace-r1'))
import capture_trace as parent

THRESHOLDS = (1_000_000_000, 1_500_000_000, 1_800_000_000,
              4_000_000_000, 8_000_000_000)
MAX_NS = 10_000_000_000
QUANTIZATION_NS = 1000
RUN_SECONDS = 11
TRIAL_SECONDS = 20


def require(ok, explanation):
    if not ok:
        raise ValueError(explanation)


def decision_oracle(record):
    """Recompute positive rational secants with explicit outward rounding."""
    e0, e1 = record['expected_delta_lo'], record['expected_delta_hi']
    l0, l1 = record['local_delta_lo'], record['local_delta_hi']
    require(THRESHOLDS[0] <= e0 <= e1 <= MAX_NS and 0 < l0 <= l1 <= MAX_NS,
            'Decision interval outside admitted positive bounds')
    lo = math.floor(Fraction(l0, e1) * 10**9 - 10**9)
    hi = math.ceil(Fraction(l1, e0) * 10**9 - 10**9)
    require((lo, hi) == (record['error_lo_ppb'], record['error_hi_ppb']),
            'Fraction secant differs from recorded error interval')
    step = -(lo // 4) if lo > 10 else ((-hi) // 4 if hi < -10 else 0)
    step = max(-1000, min(1000, step))
    # Preserve the already-accepted FollowProbe decision check. This trial
    # does not add an unrecorded servo-limit assumption or relax saturation.
    require(step == record['delta_ppb'], 'Recorded step differs from existing decision criterion')
    before, after = record['before_seq'], record['after_seq']
    require(before > 0 and record['model_token'] > 0, 'Missing model/DCO identity')
    if after == before + 1:
        require(step != 0 and record['after_ppb'] == record['before_ppb'] + step,
                'Actual DCO apply is inconsistent')
        outcome = 'applied'
    else:
        require(after == before and record['after_ppb'] == record['before_ppb'],
                'Unexplained DCO change')
        outcome = 'rejected' if step else 'no_adjust'
    q = Fraction(10**9 + record['before_ppb'], 10**9)
    require(q > 0, 'Nonpositive DCO scale')
    width_bound = math.ceil((2 * QUANTIZATION_NS + 1) * q) + 1
    require(l1-l0 <= width_bound, 'Local interval exceeds full quantization bound')
    return dict(index=record['index'], event_sequence=record['event_sequence'],
                baseline_sequence=record['baseline_sequence'], error_ppb=[lo, hi],
                step_ppb=step, outcome=outcome, local_width_ns=l1-l0,
                quantization_width_bound_ns=width_bound)


def check_windows(records):
    groups, checks = {}, []
    previous_event = -1
    for record in records:
        event, baseline = record['event_sequence'], record['baseline_sequence']
        require(event > previous_event and event > baseline, 'Decision event rollback')
        previous_event = event
        check = decision_oracle(record)
        checks.append(check)
        key = (baseline, record['model_token'])
        group = groups.setdefault(key, [])
        require(len(group) < len(THRESHOLDS), 'More than five decisions per baseline/model')
        if group:
            prior = group[-1]
            future = [v for v in THRESHOLDS if v > prior['expected_delta_lo']]
            require(bool(future) and record['expected_delta_lo'] >= future[0],
                    'Same baseline reused without a strictly later threshold')
            require(prior['delta_ppb'] == 0 and prior['before_seq'] == prior['after_seq'],
                    'Prior decision did not retain its baseline')
            require(prior['error_lo_ppb'] <= 10 and prior['error_hi_ppb'] >= -10 and
                    (prior['error_lo_ppb'] < -10 or prior['error_hi_ppb'] > 10),
                    'Retained baseline was not an uncertain deadband overlap')
            require(record['before_seq'] == prior['after_seq'] and
                    record['before_ppb'] == prior['after_ppb'], 'Retained model changed')
        group.append(record)
    return dict(decisions=checks, baselines=[dict(baseline_sequence=k[0], model_token=k[1],
                decision_count=len(v), intervals_ns=[r['expected_delta_lo'] for r in v],
                observed_interval_thresholds_ns=[max(t for t in THRESHOLDS if t <= r['expected_delta_lo']) for r in v])
                for k, v in groups.items()],
                max_decisions_per_baseline=max(map(len, groups.values()), default=0),
                observed_four_second_interval=any(r['expected_delta_lo'] >= THRESHOLDS[3] for r in records),
                observed_eight_second_interval=any(r['expected_delta_lo'] >= THRESHOLDS[4] for r in records),
                long_interval_required=False,
                limit='Observed interval bins, not reconstructed next_evaluation history. A late sample may skip thresholds; consumed but unpublished preparations require host tests.')


def check_last_projection(follow, status):
    hz = status['tick_hz']
    delta = follow['raw_lo'] - follow['baseline_raw']
    require(hz > 0 and 0 < delta <= 10 * hz, 'Last raw delta is outside ten-second bound')
    nominal = Fraction(delta * 10**9, hz)
    q = Fraction(10**9 + follow['before_ppb'], 10**9)
    require(q > 0, 'Nonpositive DCO scale')
    lower = math.floor(max(0, math.floor(nominal)-QUANTIZATION_NS) * q)
    upper = math.ceil((math.ceil(nominal)+QUANTIZATION_NS) * q)
    actual = (follow['local_interval_lo'], follow['local_interval_hi'])
    require(0 < lower <= actual[0] <= actual[1] <= upper,
            'Last local interval escapes independent correlated projection')
    return dict(raw_delta_ticks=delta, tick_hz=hz, raw_delta_seconds=float(Fraction(delta, hz)),
                nominal_delta_ns_fraction=[nominal.numerator, nominal.denominator],
                independent_candidate_ns=[lower, upper], actual_local_interval_ns=list(actual),
                quantization_margin_each_endpoint_ns=QUANTIZATION_NS,
                intersection_may_narrow=True,
                scope='Final decision only; other decision records do not contain raw endpoints.')


class LongWindowProbe(parent.TraceProbe):
    def __init__(self, opt):
        super().__init__(opt)
        self.report['schema'] = 'TYPED_LONGWINDOW_ELEVEN_SECOND_PROBE_V1'
        self.report['plan'].update(run_seconds=RUN_SECONDS, temporary_trial_seconds=TRIAL_SECONDS,
            native_typed_trace=True, trace_capture=True, diagnostic_gpio_output=False,
            window_thresholds_ns=list(THRESHOLDS), max_interval_ns=MAX_NS,
            require_long_interval=False, require_phase_lock=False,
            correlated_local_delta_quantization_ns=QUANTIZATION_NS,
            expected_capacity=dict(approximate_matches=55, approximate_decisions=11, capacity=76,
                                   dropped_records_are_failure=True),
            scope='Finite recorded decisions, endpoint DCO consistency and retained baseline coverage; no physical lock claim')
        dependencies = [Path(__file__), Path(parent.__file__), Path(parent.follow.__file__),
                        Path(parent.follow.observer.__file__), Path(parent.follow.match.__file__),
                        Path(parent.follow.match.direct.__file__), Path(parent.follow.parent.__file__),
                        Path(parent.follow.parent.base.__file__), Path(parent.trace.__file__)]
        known = {r['path'] for r in self.report['tool_sources']}
        for path in dependencies:
            if str(path) not in known:
                self.report['tool_sources'].append(dict(path=str(path), sha256=parent.follow.parent.base.sha(path)))
                known.add(str(path))

    def acquire(self):
        self.block_queries = True
        self.barrier = False
        for board in reversed(self.boards):
            self.control(board, 'SYSTem:TDMA:RING:START')
        time.sleep(.2)
        epoch = int(time.time()) & 0xffffffff
        grant = int(self.command(self.boards[0],
            f"CALibration:ORIGin:TRIAL {epoch},8192,256,{self.report['clock_hz'] * TRIAL_SECONDS}").strip('"'))
        require(grant > 0 and not grant & 1, 'Temporary trial admission failed')
        self.report['trial_epoch'] = grant
        self.report['quiet_wait_started_monotonic'] = time.monotonic()
        self.save()
        time.sleep(RUN_SECONDS)
        self.report['quiet_wait_elapsed_s'] = time.monotonic() - self.report['quiet_wait_started_monotonic']

    def collect(self):
        super().collect()
        for board in self.boards[1:]:
            row = self.report['boards'][board.address]
            try:
                row['longwindow_check'] = check_windows(row['trace_decisions'])
                row['correlated_last_decision'] = check_last_projection(row['follow'], row['trace_stop_ack']['status'])
                row['longwindow_independent_checks_passed'] = True
            except Exception as exc:
                self.error('longwindow-independent:' + board.address, exc)
            self.save()
        self.report['passed'] = not self.report['errors']
        self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--tap-baseline', type=Path, required=True)
    parser.add_argument('--check-only', action='store_true')
    opt = parser.parse_args()
    opt.seconds = RUN_SECONDS
    probe = LongWindowProbe(opt)
    probe.run()
    print(json.dumps(dict(passed=probe.report['passed'], errors=probe.report['errors'], out=str(opt.out))))
    return 0 if probe.report['passed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
