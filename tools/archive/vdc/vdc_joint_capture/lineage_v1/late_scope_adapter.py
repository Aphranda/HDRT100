"""Current-source native/RUN capture with a configurable delayed NO1-triggered scope window."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import traceback
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
FROZEN = ROOT/'out/HardwareAcceptance/20260918/dpll-run-timeline-r2'

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value

capture = module('resume_capture_parent', HERE/'capture_run_output_schema10.py')
trial = module('resume_trial_parent', FROZEN/'timing_trial_safe_schema9.py')
scope_reader = module('resume_scope_reader', ROOT/'out/HardwareAcceptance/20260916/dpll-scope-hold-r1/capture_scope_r3.py')

class ConfigurableScope(scope_reader.Scope):
    def query(self, command):
        if command == ':WAV:PRE?':
            # HDO PRE point count is the selected transfer range. Normalize
            # that range before comparing otherwise identical channel axes.
            self.write(':WAV:STAR 1')
            self.write(f':WAV:STOP {self.points}')
        return super().query(command)

    def write(self, command):
        if command.startswith(':TIM:SCAL '):
            # In STOP, HDO limits offset edits to the retained record. Configure
            # the next acquisition while waiting for a normal NO1 trigger.
            super().write(':TRIG:SWE NORM')
            super().write(':RUN')
        super().write(command)
        if command.startswith(':ACQ:MDEP '):
            time.sleep(.25)
            super().write(f':TIM:OFFS {self.offset}')

    def prepare(self):
        prior = self.report['initial_error_queue'] = []
        for _ in range(32):
            raw = self.query(':SYST:ERR?')
            prior.append(raw)
            if raw.startswith(('0,','+0,')):
                break
        else:
            raise RuntimeError('Scope initial error queue did not drain')
        super().prepare()
        self.write(':STOP')

class ResumeTrial(trial.Trial):
    DELAY = 'SYSTem:VDC:OUTPut:DELay'

    def __init__(self, *args, output_delays, **kwargs):
        super().__init__(*args, **kwargs)
        if len(output_delays) != len(trial.hil.IDS):
            raise ValueError('Exactly four output-delay values are required')
        self.output_delays = tuple(output_delays)
        self.original_delays = {}

    def _configure_delays(self, values, phase):
        rows = self.report.setdefault('boards', {})
        for uid, value in zip(trial.hil.IDS, values):
            row = rows.setdefault(uid, {})
            raw = self.command(uid, self.DELAY + '?')
            try:
                before = int(raw.strip())
            except ValueError as exc:
                raise RuntimeError(f'{uid}: invalid output-delay readback {raw!r}') from exc
            if phase == 'initial':
                self.original_delays[uid] = before
                row['output_delay_original_ns'] = before
            raw = self.command(uid, f'{self.DELAY} {value}')
            try:
                applied = int(raw.strip())
            except ValueError as exc:
                raise RuntimeError(f'{uid}: invalid output-delay apply {raw!r}') from exc
            check = int(self.command(uid, self.DELAY + '?').strip())
            if applied != value or check != value:
                raise RuntimeError(f'{uid}: output-delay mismatch requested={value} applied={applied} readback={check}')
            row[f'output_delay_{phase}_requested_ns'] = value
            row[f'output_delay_{phase}_readback_ns'] = check
            self.save()

    def _snapshot_delays(self):
        for uid in trial.hil.IDS:
            raw = self.command(uid, self.DELAY + '?')
            try:
                value = int(raw.strip())
            except ValueError as exc:
                raise RuntimeError(f'{uid}: invalid initial output-delay readback {raw!r}') from exc
            self.original_delays[uid] = value
            self.report['boards'][uid]['output_delay_original_ns'] = value
        self.save()

    def set_timing(self, uid, value):
        super().set_timing(uid, value)
        # The parent takes its invariant snapshot before this stage. Apply the
        # candidate only after that snapshot so final verification still expects
        # the true pre-trial delay values.
        if self.stage_name == 'apply_ram_timing' and uid == trial.hil.IDS[-1]:
            self._configure_delays(self.output_delays, 'apply')

    def barrier(self):
        super().barrier()
        if self.stage_name == 'initial_output_and_tdma_stop':
            # A retained queue is not attributable to this trial. Preserve every
            # item, bound the drain, then enforce the parent's clean checks for
            # all new configuration/run/restore activity.
            for uid in trial.hil.IDS:
                errors = self.report['boards'][uid]['initial_error_queue'] = []
                for _ in range(32):
                    raw = self.command(uid, trial.hil.ERROR)
                    errors.append(raw)
                    self.save()
                    if trial.hil.error_code(raw) == 0:
                        break
                else:
                    raise RuntimeError(f'{uid}: initial error queue did not drain')
            self._snapshot_delays()
        elif self.stage_name == 'final_output_and_tdma_stop':
            # Restore the exact pre-trial values only after all boards are STOP.
            # A failed restore remains a cleanup error and cannot be reported as
            # a successful capture.
            if set(self.original_delays) != set(trial.hil.IDS):
                raise RuntimeError('Initial output-delay snapshot is incomplete')
            self._configure_delays(tuple(self.original_delays[uid] for uid in trial.hil.IDS), 'final')

class Probe(capture.RunProbe):
    def __init__(self, opt):
        super().__init__(opt)
        self.trial_timing = tuple(opt.profile)

    def error(self, scope, exc):
        super().error(scope, exc)
        self.report['errors'][-1]['traceback'] = traceback.format_exc()
        self.save()

    def configure(self):
        # Parent owns all fresh session/MATCH/FOLLOW/trace/RUN setup and ARM.
        # The scope is configured before any START; no RUN query is added.
        self.opt.scope = False
        self.scope = ConfigurableScope(self.opt.out/'scope', scale=self.opt.scope_scale,
                                       points=10000000, offset=self.opt.scope_offset)
        self.scope.prepare()
        super().configure()
        self.report['plan']['scope_window'] = f'{self.opt.scope_scale}s/div, {self.opt.scope_offset}s delayed center after NO1 trigger; frozen PRE is authoritative; overlap assessed offline'
        self.report['scope_trigger'] = 'NO1 OUT1 / CH1 rising, 1x; restore STOP/EXT/NORM'
        self.report['tool_sources'].append(dict(path=str(Path(__file__)),
            sha256=capture.phase.follow.parent.base.sha(Path(__file__))))
        self.save()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--child', action='store_true')
    parser.add_argument('--scope-offset', type=float, default=1.0)
    parser.add_argument('--scope-scale', type=float, default=.02)
    parser.add_argument('--scope-check', action='store_true')
    parser.add_argument('--recover-scope', type=Path)
    parser.add_argument('--profile', default='24000,32000,16000')
    parser.add_argument('--output-delay', default='0,0,0,0',
                        help='NO1,NO2,NO3,NO4 signed output delay in ns; STOP-only')
    args = parser.parse_args()
    receipt_path = ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json'
    profile = trial.hil.pair(args.profile)
    try:
        output_delays = tuple(int(value.strip()) for value in args.output_delay.split(','))
    except ValueError as exc:
        raise SystemExit('--output-delay requires four signed decimal integers') from exc
    if len(output_delays) != 4 or any(not -(1 << 31) <= value < (1 << 31) for value in output_delays):
        raise SystemExit('--output-delay requires four signed 32-bit integers')
    if args.recover_scope:
        old_path = args.recover_scope/'scope.json'
        old = json.loads(old_path.read_text())
        scope = ConfigurableScope(args.out, scale=args.scope_scale, points=10000000, offset=args.scope_offset)
        scope.report = old
        scope.report.update(channels=[], capture_complete=False,
            recovery_source=str(old_path), recovery_source_sha256=capture.phase.follow.parent.base.sha(old_path),
            recovery_note='Same retained STOP acquisition; no new RUN, SINGLE or trigger')
        try:
            scope.export()
            # Prove the already-exported CH1 bytes were not replaced by a new
            # acquisition while recovering the other frozen channels.
            old_report = json.loads(old_path.read_text())
            for a,b in zip(old_report['channels'][0]['blocks'], scope.report['channels'][0]['blocks']):
                assert a['sha256'] == b['sha256']
            scope.report['recovered_ch1_matches_original'] = True
            scope.save()
            print(json.dumps(dict(capture_complete=True, ch1_unchanged=True)))
        finally:
            for cmd in (':STOP', ':TRIG:EDGE:SOUR EXT', ':TRIG:SWE NORM'):
                scope.write(cmd)
            scope.close()
        return 0
    if args.scope_check:
        scope = ConfigurableScope(args.out, scale=args.scope_scale, points=10000000, offset=args.scope_offset)
        try:
            scope.prepare()
            print(json.dumps(scope.report['settings']))
        finally:
            for cmd in (':STOP', ':TRIG:EDGE:SOUR EXT', ':TRIG:SWE NORM'):
                scope.write(cmd)
            scope.close()
        return 0
    if args.child:
        opt = argparse.Namespace(receipt=receipt_path, out=args.out,
            baseline=ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json',
            tap_baseline=ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json',
            period_ns=1000000, scope=False, check_only=False, seconds=11, profile=profile,
            scope_offset=args.scope_offset, scope_scale=args.scope_scale)
        probe = Probe(opt)
        probe.run()
        print(json.dumps(dict(passed=probe.report['passed'], errors=probe.report['errors'])))
        return 0 if probe.report['passed'] else 2
    receipt = trial.hil.validate_receipt(receipt_path)
    runner = ResumeTrial(trial.hil.SerialBackend(), receipt, args.out,
        profile, [sys.executable, str(Path(__file__).resolve()),
                  '--out', str(args.out/'capture'), '--child', '--profile', args.profile,
                  '--scope-offset', str(args.scope_offset), '--scope-scale', str(args.scope_scale)],
        output_delays=output_delays,
        capture_timeout=240)
    runner.report.update(script_sha256=capture.phase.follow.parent.base.sha(Path(__file__)),
        receipt_path=str(receipt_path),
        receipt_sha256=capture.phase.follow.parent.base.sha(receipt_path),
        source_tree_sha256=receipt['source_tree_sha256'],
        run_status_schema=10, origin_native_schema=5, timebase='TIMER1_NS',
        quiet_acquisition_seconds=11, scope_trigger='CH1 rising',
        scope_scale=args.scope_scale, scope_offset=args.scope_offset,
        output_delay_requested_ns=list(output_delays),
        adapter_validation=str(HERE/'adapter-validation.json'))
    runner.save()
    report = runner.run()
    print(json.dumps({k:report[k] for k in ('passed','errors','cleanup_errors','restore_needed','duration_s')}))
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
