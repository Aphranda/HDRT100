#!/usr/bin/env python3
"""Optional sparse scope verification of an explicitly selected GUARD bench adapter.

The adapter owns board setup, SRAM capture, STOP barriers and restoration. Scope
traffic uses VISA only. No board query is added between START and the STOP barrier.
The bench adapter and its calibration dependencies must already be validated;
this is a diagnostic launcher, not a discovery or automatic calibration tool.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.vdc_priority_trace.vdc_scope_evidence import EvidenceWriter, capture_raw, analyze_memory


def load_adapter(path):
    spec = importlib.util.spec_from_file_location('joint_guard_adapter', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for name in ('Probe', 'base', 'parse_run_output', 'evaluate_board'):
        if not hasattr(module, name):
            raise ValueError(f'GUARD bench adapter missing {name}')
    return module


def verify_startup(probe):
    """Read-only admission on the adapter's existing STOP-owned connections."""
    from tools.calibration_ring_validate import trn03_stage as stage

    report = probe.report['startup_preflight'] = dict(passed=False, boards=[], errors=[])
    probe.save()
    try:
        if not probe.barrier or probe.block_queries:
            raise ValueError('STOP barrier is required before startup preflight')
        matrix_path = ROOT / probe.receipt['trn03_matrix']['path']
        raw = matrix_path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != probe.receipt['trn03_matrix']['sha256']:
            raise ValueError('Receipt matrix fingerprint mismatch')
        matrix = json.loads(raw)
        expected = matrix['node_ids_in_loop_order']
        if ([board.address for board in probe.boards] != expected or
                len(set(expected)) != matrix['node_count'] or
                len({board.port for board in probe.boards}) != len(expected)):
            raise ValueError('Board order or unique UID/port mapping mismatch')
        report['matrix_sha256'] = hashlib.sha256(raw).hexdigest()
        for slot, board in enumerate(probe.boards):
            row = dict(uid=board.address, port=board.port, passed=False, responses={})
            report['boards'].append(row)

            def query(command):
                response = probe.command(board, command)
                row['responses'][command] = response
                probe.save()
                return response

            identity = next(csv.reader([query('*IDN?')]))
            if (len(identity) != 4 or identity[0] != f'NO.{slot + 1}' or
                    identity[2] != board.address or
                    int(query('SYSTem:BOARD:NO?').strip('"')) != slot + 1):
                raise ValueError(f'{board.address}: IDN/board number mismatch')
            if query('SYSTem:FW:BUILD?').strip('"') != str(probe.receipt['build_id']):
                raise ValueError(f'{board.address}: firmware build mismatch')
            ring = list(map(int, query('SYSTem:TDMA:RING:STATus?').split(',')))
            if (len(ring) != 40 or ring[0] != 0
                    or ring[4] or ring[5] or ring[8] or ring[38] != ring[39]):
                raise ValueError(f'{board.address}: STOP/config ACK not ready')
            if query('SYSTem:TDMA:FLIGHT:MODE?').strip('"') != '2':
                raise ValueError(f'{board.address}: process-image mode required; restore MODE 1 while STOPPED')
            header = stage.parse_query(query('READ:CALibration:TRAINing:STAGe?'),
                                       stage.STAGE_QUERY_FIELDS, 'TRN03STG')
            if (header['enabled'] != 1 or header['complete'] != 1 or
                    header['valid_link_bitmap'] != (1 << len(expected)) - 1 or
                    any(header[key] != matrix[key] for key in stage.HEADER_FIELDS)):
                raise ValueError(f'{board.address}: training header differs from receipt')
            for index, link in enumerate(matrix['links']):
                observed = stage.parse_query(query(f'READ:CALibration:TRAINing:STAGe:LINK? {index}'),
                                             stage.LINK_QUERY_FIELDS, 'TRN03LNK')
                if (observed['valid'] != 1 or
                        any(observed[key] != link[key] for key in stage.LINK_FIELDS) or
                        any(observed[key] != matrix[key] for key in stage.HEADER_FIELDS[2:]) or
                        any(observed[key] != link.get(key, 0) for key in stage.ORIGIN_CAPTURE_FIELDS)):
                    raise ValueError(f'{board.address}: training link {index} differs from receipt')
            if not query('SYSTem:ERR?').startswith(('0,', '+0,')):
                raise ValueError(f'{board.address}: startup SCPI error queue not empty')
            row['passed'] = True
        report['passed'] = True
    except Exception as exc:
        report['errors'].append(f'{type(exc).__name__}: {exc}')
        raise RuntimeError(f'STARTUP_CONFIGURATION: {exc}') from exc
    finally:
        probe.save()


def scope_checkpoint(samples, due_s):
    expected = set(range(due_s - 55, due_s + 1, 5))
    window = [r for r in samples if due_s - 60 < r['due_s'] <= due_s]
    seen = [r['due_s'] for r in window]
    failures = [{'due_s': s, 'reason': 'missing_schedule'} for s in sorted(expected - set(seen))]
    if len(seen) != len(set(seen)):
        failures.append({'reason': 'duplicate_schedule'})
    for row in window:
        phase = row.get('phase', {})
        delta = phase.get('relative_ns', [])
        valid = (row.get('capture_complete') is True and not row.get('missed')
                 and len(delta) == 3 and all(values for values in delta)
                 and all(math.isfinite(v) for values in delta for v in values))
        if not valid:
            failures.append({'due_s': row['due_s'], 'reason': 'invalid_waveform'})
        elif any(abs(v) > 100 for values in delta for v in values):
            failures.append({'due_s': row['due_s'], 'reason': 'phase_outside_100ns'})
        if row.get('overrun'):
            failures.append({'due_s': row['due_s'], 'reason': 'acquisition_overrun'})
    return dict(checkpoint_s=due_s, continue_run=not failures, failures=failures,
                threshold_ns=100, unobserved_intervals_qualified=False)


def joint_verdict(internal_passed, scope_enabled, decisions, seconds, errors):
    external = None
    if scope_enabled:
        external = (len(decisions) == seconds // 60
                    and [d['checkpoint_s'] for d in decisions] == list(range(60, seconds + 1, 60))
                    and all(d['continue_run'] for d in decisions))
    return dict(internal_health_passed=bool(internal_passed),
                external_sampled_windows_passed=external,
                external_status='PASS' if external else 'FAIL' if scope_enabled else 'SKIPPED',
                passed=bool(internal_passed and not errors and external is not False),
                physical_continuous_lock_qualified=False,
                exact_event_correlation_qualified=False)


def make_scope_class(external):
    class ExternalScope(external.SparseScope):
        trigger_source = 'EXT'

        def save(self):
            # Keep query/RAW evidence in memory through one acquisition. The
            # inherited query() otherwise rewrites the report after every I/O.
            if not getattr(self, '_defer_save', False) and not getattr(self, 'evidence_writer', None):
                return super().save()

        def enable_async_evidence(self):
            self.evidence_writer = EvidenceWriter()

        def finish_evidence(self):
            if getattr(self, 'evidence_writer', None):
                self.evidence_writer.close()

        def export(self):
            if getattr(self, 'evidence_writer', None):
                return capture_raw(self, external.base.scope_reader)
            return super().export()

        def prepare(self):
            super().prepare()
            self.write(f':TRIG:EDGE:SOUR {self.trigger_source}')
            self.write(':TRIG:EDGE:LEV 1.5')
            if self.query(':TRIG:EDGE:SOUR?') != self.trigger_source:
                raise ValueError('External trigger source not applied')
            if not self.query(':SYST:ERR?').startswith(('0,', '+0,')):
                raise ValueError('External trigger setup rejected')
            self.report['settings'][':TRIG:EDGE:SOUR?'] = self.trigger_source
            self.identity = self.report['identity']
            self.settings = dict(self.report['settings'])
            self.save()

        def fresh_snapshot(self, folder):
            writer = getattr(self, 'evidence_writer', None)
            if writer:
                writer.check()
                if len(writer.pending) >= writer.capacity:
                    raise RuntimeError('Evidence write queue full before acquisition')
            # Never replace an existing capture, including on an error path.
            folder.mkdir(parents=True, exist_ok=False)
            self._raw_blocks = {}
            self._defer_save = True
            try:
                return self._capture_snapshot(folder)
            finally:
                self._defer_save = False
                if writer:
                    writer.submit(folder, self.report, self._raw_blocks)
                else:
                    self.save()  # Persist partial evidence on timeout/export failure too.

        def _capture_snapshot(self, folder):
            self.folder = folder
            self.report = dict(resource=external.base.scope_reader.RESOURCE, commands=[], channels=[],
                               capture_complete=False, identity=self.identity, settings=self.settings,
                               voltage_formula='(code-y_origin-y_reference)*y_increment')
            self.sampling = True
            self.scope.timeout = 1800
            begun = time.monotonic()
            self.write(':STOP')
            if self.query(':TRIG:STAT?') != 'STOP':
                raise ValueError('Scope failed to STOP')
            # Negative threshold is outside the nominal 0..5 V pulse. Seeing
            # WAIT is mandatory: a preceding frozen record is never accepted.
            self.write(':TRIG:EDGE:LEV -1.5')
            if not math.isclose(float(self.query(':TRIG:EDGE:LEV?')), -1.5):
                raise ValueError('Fresh-arm inhibit threshold rejected')
            self.write(':SING')
            deadline = time.monotonic() + 1.0
            while self.query(':TRIG:STAT?') != 'WAIT':
                if time.monotonic() >= deadline:
                    raise TimeoutError(f'Fresh {self.trigger_source} single WAIT was not observed')
                time.sleep(.01)
            if self.query(':TRIG:SWE?') != 'SING' or self.query(':TRIG:EDGE:SOUR?') != self.trigger_source:
                raise ValueError('Fresh arm source/mode mismatch')
            self.report['fresh_wait_ns'] = time.monotonic_ns()
            self.report['armed_ns'] = time.monotonic_ns()
            self.write(':TRIG:EDGE:LEV 1.5')
            self.report['trigger_admitted_ns'] = time.monotonic_ns()
            deadline = time.monotonic() + 1.0
            while self.query(':TRIG:STAT?') != 'STOP':
                if time.monotonic() >= deadline:
                    raise TimeoutError(f'No new {self.trigger_source}-triggered complete acquisition')
                time.sleep(.01)
            self.report['trigger_complete_observed_ns'] = time.monotonic_ns()
            if not math.isclose(float(self.query(':TRIG:EDGE:LEV?')), 1.5):
                raise ValueError('Trigger admission threshold rejected')
            self.export()  # All four channels from one frozen RAW record.
            if getattr(self, 'evidence_writer', None):
                analysis_start = time.monotonic_ns()
                self.report['phase'] = analyze_memory(self.report, self._raw_blocks,
                                                      external.base.scope_reader.decode_block)
                self.report['analysis_elapsed_ns'] = time.monotonic_ns() - analysis_start
            self.report['elapsed_s'] = time.monotonic() - begun
            self.save()
            return self.report
    return ExternalScope


def make_probe_class(adapter):
    base = adapter.base
    external = base.external

    class JointProbe(adapter.Probe):
        def command(self, board, text, unused=None):
            if getattr(self, '_startup_rejected', False):
                # Bypass adapter initialization hooks during failed-admission
                # cleanup; retain the original command journal and STOP guard.
                return base.follow.parent.base.Probe.command(self, board, text, unused)
            return super().command(board, text, unused)

        def configure(self):
            try:
                verify_startup(self)
            except Exception:
                self._startup_rejected = True
                raise
            if self.opt.external_scope:
                cls = make_scope_class(external)
                self.scope = cls(self.opt.out / 'scope-setup', scale=.0002, points=1000000, offset=.0008)
                self.scope.trigger_source = self.opt.scope_trigger
                self.scope.prepare()
                self.scope.enable_async_evidence()
            super().configure()
            self.report['plan'].update(external_scope=self.opt.external_scope,
                scope_trigger=self.opt.scope_trigger if self.opt.external_scope else None,
                scope_sample_interval_s=5 if self.opt.external_scope else None,
                scope_observation='fresh sparse RAW windows; no claim for gaps between samples')
            self.report['joint_tool_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
            self.report['bench_adapter_sha256'] = hashlib.sha256(self.opt.bench_adapter.read_bytes()).hexdigest()
            self.save()

        def acquire(self):
            if not self.opt.external_scope:
                return super().acquire()
            self.block_queries = True
            self.barrier = False
            self.scope.quiet = True
            external.startup.parallel_start(self)
            time.sleep(.2)
            epoch = int(time.time()) & 0xffffffff
            grant = int(self.command(self.boards[0], f'CALibration:ORIGin:TRIAL {epoch},8192,256,0').strip('"'))
            if not grant or grant & 1:
                raise RuntimeError('Origin trial grant rejected')
            self.report['trial_epoch'] = grant
            self.scope.quiet = False  # VISA only; board queries stay blocked.
            start = time.monotonic()
            self.report['monitor_started_monotonic'] = start
            samples = self.report['scope_samples'] = []
            decisions = self.report['scope_checkpoints'] = []
            for due in range(5, self.opt.seconds + 1, 5):
                while time.monotonic() < start + due:
                    time.sleep(min(.2, max(0, start + due - time.monotonic())))
                row = dict(due_s=due, started_s=time.monotonic()-start)
                samples.append(row)
                self.save()
                if row['started_s'] >= due + 5:
                    row.update(missed=True, reason='prior acquisition overrun; no catch-up')
                else:
                    try:
                        folder = self.opt.out / f'sample-{due:04d}'
                        raw = self.scope.fresh_snapshot(folder)
                        row.update(capture_complete=raw['capture_complete'],
                                   phase=raw['phase'],
                                   acquisition_host_ns=[raw['trigger_admitted_ns'], raw['trigger_complete_observed_ns']],
                                   capture_elapsed_s=raw['elapsed_s'])
                    except Exception as exc:
                        row.update(missed=True, error=f'{type(exc).__name__}: {exc}')
                        # Preserve incomplete RAW/commands and try to regain a
                        # bounded scope state. No preceding waveform is reused.
                        self.scope.write(':STOP')
                        row['scope_error_queue'] = []
                        for _ in range(16):
                            err = self.scope.query(':SYST:ERR?')
                            row['scope_error_queue'].append(err)
                            if err.startswith(('0,', '+0,')):
                                break
                        else:
                            raise RuntimeError('Scope error queue did not drain')
                row['ended_s'] = time.monotonic() - start
                row['overrun'] = row['ended_s'] > due + 5
                if due % 60 == 0:
                    decision = scope_checkpoint(samples, due)
                    decisions.append(decision)
                    (self.opt.out / f'scope-checkpoint-{due}.json').write_text(json.dumps(decision, indent=2), encoding='utf-8')
                    if not decision['continue_run']:
                        self.report['scope_checkpoint_stop'] = decision
                        self.save()
                        break
                self.save()
                print(json.dumps(dict(scope_sample_s=due, missed=row.get('missed', False),
                                      relative_ns=row.get('phase', {}).get('relative_ns'))), flush=True)
            # Cover target sealing independently of scope transfer duration.
            if not self.report.get('scope_checkpoint_stop'):
                while time.monotonic() < start + self.opt.seconds + 2:
                    time.sleep(min(.2, max(0, start + self.opt.seconds + 2 - time.monotonic())))
            self.report['quiet_elapsed_s'] = time.monotonic() - start

        def collect(self):
            if not self.report.get('startup_preflight', {}).get('passed'):
                self.report['joint_internal_health_passed'] = False
                self.report['collection_skipped'] = 'startup configuration rejected before ARM'
                self.save()
                return
            super().collect()
            # Preserve the internal capture verdict before scope restoration
            # or the optional external precision verdict affects overall PASS.
            self.report['joint_internal_health_passed'] = bool(self.report['passed'])
            self.save()

        def run(self):
            try:
                super().run()
            finally:
                scope = getattr(self, 'scope', None)
                if scope is not None and hasattr(scope, 'finish_evidence'):
                    try:
                        scope.finish_evidence()
                        self.report['scope_evidence_flushed'] = True
                    except Exception as exc:
                        self.report['errors'].append(dict(scope='evidence_flush', error=repr(exc)))
                        self.report['scope_evidence_flushed'] = False
                verdict = joint_verdict(self.report.get('joint_internal_health_passed', False), self.opt.external_scope,
                                        self.report.get('scope_checkpoints', []), self.opt.seconds,
                                        self.report['errors'])
                self.report['joint_verdict'] = verdict
                self.report['passed'] = verdict['passed']
                self.report['conclusion_scope'] = ('Internal GUARD health and optional simultaneous sparse external phase '
                    'checks are independent. Same run is proven; exact event/bin alignment, unsampled GPIO intervals, '
                    'and full physical lock are not qualified. All board queries remain after the STOP barrier.')
                if 'strict_replan_qualification' in self.report:
                    q = self.report['strict_replan_qualification']
                    q['base_health_passed'] = verdict['internal_health_passed']
                    q['passed'] = bool(q['criteria_passed'] and verdict['internal_health_passed'])
                self.save()
    return JointProbe


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bench-adapter', type=Path, required=True)
    p.add_argument('--scope', choices=('off', 'on'), default='off')
    p.add_argument('--scope-trigger', choices=('CHAN1', 'EXT'), default='CHAN1',
                   help='CHAN1 observes NO1 OUT1; EXT requires an independently active OUT4 signal')
    p.add_argument('--seconds', type=int, default=60)
    p.add_argument('--output-delays', type=int, nargs=4, default=(0, -28, -88, -116),
                   metavar=('NO1', 'NO2', 'NO3', 'NO4'),
                   help='STOP-only signed output delays in ns; restored after capture, not saved to Flash')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--child', action='store_true', help=argparse.SUPPRESS)
    a = p.parse_args(argv)
    if a.seconds not in range(60, 601, 60):
        p.error('seconds must be a whole minute from 60 through 600')
    if any(not -(1 << 31) <= value < (1 << 31) for value in a.output_delays):
        p.error('output-delays must be signed int32 nanoseconds')
    if not a.bench_adapter.is_file():
        p.error('bench-adapter must be an existing validated GUARD capture script')
    return a


def main():
    a = parse_args()
    adapter = load_adapter(a.bench_adapter)
    base = adapter.base
    if a.child:
        opt = argparse.Namespace(receipt=ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json', out=a.out,
            baseline=ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json',
            tap_baseline=ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json',
            period_ns=1000000, scope=False, check_only=False, seconds=a.seconds, summary_interval_ms=10000,
            no_reference=False, expire_output=False, external_scope=a.scope=='on', scope_trigger=a.scope_trigger,
            bench_adapter=a.bench_adapter)
        probe = make_probe_class(adapter)(opt)
        probe.run()
        print(json.dumps(probe.report['joint_verdict']))
        return 0 if probe.report['passed'] else 2
    receipt = base.external.base.trial.hil.validate_receipt(ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json')
    child = [sys.executable, str(Path(__file__).resolve()), '--bench-adapter', str(a.bench_adapter.resolve()),
             '--scope', a.scope, '--scope-trigger', a.scope_trigger,
             '--seconds', str(a.seconds), '--out', str(a.out/'capture'), '--child']
    runner = base.external.base.ResumeTrial(base.external.base.trial.hil.SerialBackend(), receipt, a.out,
        (24000, 32000, 16000), child, output_delays=tuple(a.output_delays), capture_timeout=a.seconds+240)
    result = runner.run()
    print(json.dumps({k: result[k] for k in ('passed', 'errors', 'cleanup_errors', 'restore_needed', 'duration_s')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
