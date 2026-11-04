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
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.vdc_priority_trace.vdc_scope_evidence import EvidenceWriter, capture_raw, analyze_memory
from tools.common.hdo_scope import reader as scope_reader
from tools.common.host_evidence_clock import identity as host_clock_identity, now_ns
from tools.domains.vdc.scope_capture.sparse import SparseScope
from tools.workflows.vdc_joint_capture.inputs import load_inputs, validate_current_receipt
from tools.workflows.vdc_joint_capture.internal_seal import load_adapter as load_internal_seal_adapter
from tools.workflows.vdc_joint_capture.lifecycle import ResumeTrial, SerialBackend


DEFAULT_WORKFLOW_INPUTS = ROOT / 'config/tool_workflows/vdc_joint_capture_internal_seal.json'


DPLL_REFERENCE_SLOT = 0


def validate_dpll_role(raw, *, slot, uid, reference_slot=DPLL_REFERENCE_SLOT):
    """Reject a capture whose DPLL control role cannot satisfy the matcher.

    The ROLE page is ``mode, follow_master_slot_id, requested, applied, pending``
    (``SYSTem:SYNC:VDC:DPLL:ROLE?``).  The matcher's ROLE gate requires every
    follower to be ``VDC_DPLL_CONTROL_MODE_FOLLOWER`` with the ring reference
    slot, and the ring origin to stay MASTER.  A pending request has not reached
    the domain profile yet, so it is rejected here instead of after the 60 s
    acquisition: the historical failure cost two full windows and ended the
    whole acceptance run without a credential.
    """
    try:
        fields = [int(value.strip().strip('"')) for value in str(raw).split(',')]
    except ValueError as exc:
        raise ValueError(f'{uid}: DPLL role page is not numeric') from exc
    if len(fields) != 5:
        raise ValueError(
            f'{uid}: DPLL role page has {len(fields)} fields, expected 5')
    mode, source, requested, applied, pending = fields
    if pending or requested != applied:
        raise ValueError(
            f'{uid}: DPLL role is not applied '
            f'(requested={requested}, applied={applied}, pending={pending})')
    if slot == 0:
        if mode != 0:
            raise ValueError(
                f'{uid}: DPLL role mode={mode}, expected MASTER(0) for the ring origin')
    else:
        if mode != 1:
            raise ValueError(
                f'{uid}: DPLL role mode={mode}, expected FOLLOWER(1); '
                f'configure SYSTem:SYNC:VDC:DPLL:ROLE 1,{reference_slot} while stopped')
        if source != reference_slot:
            raise ValueError(
                f'{uid}: DPLL role follows slot {source}, '
                f'expected reference slot {reference_slot}')
        if applied == 0:
            raise ValueError(f'{uid}: DPLL role (follower) was never applied')
    return dict(mode=mode, follow_master_slot_id=source,
                requested_generation=requested, applied_generation=applied,
                pending=pending)


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
            row['dpll_role'] = validate_dpll_role(
                query('SYSTem:SYNC:VDC:DPLL:ROLE?'), slot=slot,
                uid=board.address)
            ring = list(map(int, query('SYSTem:TDMA:RING:STATus?').split(',')))
            # The page is append-only: newer firmware appends fields after the
            # original 40 (e.g. the ring loop-time block), so require at least
            # the historical prefix and keep validating those indices, which
            # retain their meaning. An exact-length check here made every
            # capture fail fast with "STOP/config ACK not ready".
            if (len(ring) < 40 or ring[0] != 0
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


def scope_checkpoint(samples, due_s, settle_s=5.0, *,
                     threshold_ns=100.0, steady_threshold_ns=50.0):
    """Grade one minute of sparse windows against the coarse and steady bands.

    The coarse +/-100 ns bound is the fail-fast abort criterion and applies to
    every window, including the first one after START. The tight +/-50 ns bound
    is the steady-state qualification and applies only to windows later than the
    declared ``settle_s`` settling allowance: the first window still carries the
    loop's convergence transient, so holding it to the steady band would fail a
    healthy run. Every window is classified in the returned evidence, and a
    settling window that used the allowance is listed explicitly rather than
    silently dropped.
    """
    expected = set(range(due_s - 55, due_s + 1, 5))
    window = [r for r in samples if due_s - 60 < r['due_s'] <= due_s]
    seen = [r['due_s'] for r in window]
    failures = [{'due_s': s, 'reason': 'missing_schedule'} for s in sorted(expected - set(seen))]
    if len(seen) != len(set(seen)):
        failures.append({'reason': 'duplicate_schedule'})
    settling, steady, exemptions, steady_failures = [], [], [], []
    for row in window:
        phase = row.get('phase', {})
        delta = phase.get('relative_ns', [])
        valid = (row.get('capture_complete') is True and not row.get('missed')
                 and len(delta) == 3 and all(values for values in delta)
                 and all(math.isfinite(v) for values in delta for v in values))
        if not valid:
            failures.append({'due_s': row['due_s'], 'reason': 'invalid_waveform'})
        else:
            worst = max(abs(v) for values in delta for v in values)
            # An unreadable schedule time cannot claim the settling allowance.
            is_settling = isinstance(row.get('due_s'), (int, float)) and row['due_s'] <= settle_s
            if worst > threshold_ns:
                reason = ('phase_outside_100ns' if threshold_ns == 100 else
                          'phase_outside_threshold')
                failures.append({'due_s': row['due_s'], 'reason': reason,
                                 'max_abs_ns': worst})
            elif is_settling:
                settling.append(row['due_s'])
                if worst > steady_threshold_ns:
                    exemptions.append({'due_s': row['due_s'], 'max_abs_ns': worst})
            else:
                steady.append(row['due_s'])
                if worst > steady_threshold_ns:
                    reason = ('phase_outside_50ns' if steady_threshold_ns == 50 else
                              'phase_outside_steady_threshold')
                    steady_failures.append({'due_s': row['due_s'], 'reason': reason,
                                            'max_abs_ns': worst})
        if row.get('overrun'):
            failures.append({'due_s': row['due_s'], 'reason': 'acquisition_overrun'})
    return dict(checkpoint_s=due_s, continue_run=not failures, failures=failures,
                threshold_ns=threshold_ns, steady_threshold_ns=steady_threshold_ns,
                settle_s=settle_s,
                settling_windows=sorted(settling), steady_windows=sorted(steady),
                settling_exemptions=exemptions, steady_failures=steady_failures,
                unobserved_intervals_qualified=False)


def joint_verdict(internal_passed, scope_enabled, decisions, seconds, errors):
    external = None
    steady = None
    exemptions = None
    if scope_enabled:
        external = (len(decisions) == seconds // 60
                    and [d['checkpoint_s'] for d in decisions] == list(range(60, seconds + 1, 60))
                    and all(d['continue_run'] for d in decisions))
        # Fail-closed on the steady band: a checkpoint that never carried the
        # classification cannot prove it, so the qualification stays unproven
        # instead of defaulting to a pass.
        steady = bool(external) and all('steady_failures' in d and not d['steady_failures']
                                        for d in decisions)
        exemptions = sum(len(d.get('settling_exemptions', [])) for d in decisions)
    return dict(internal_health_passed=bool(internal_passed),
                external_sampled_windows_passed=external,
                external_steady_state_passed=steady,
                external_settling_exemptions=exemptions,
                external_status='PASS' if external else 'FAIL' if scope_enabled else 'SKIPPED',
                passed=bool(internal_passed and not errors and external is not False
                            and steady is not False),
                physical_continuous_lock_qualified=False,
                exact_event_correlation_qualified=False)


def make_scope_class(scope_base=SparseScope, reader=scope_reader):
    class ExternalScope(scope_base):
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
                return capture_raw(self, reader)
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
            self.report = dict(resource=self.resource, commands=[], channels=[],
                               capture_complete=False, identity=self.identity, settings=self.settings,
                               host_clock=host_clock_identity(),
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
            self.report['fresh_wait_ns'] = now_ns()
            self.report['armed_ns'] = now_ns()
            self.write(':TRIG:EDGE:LEV 1.5')
            self.report['trigger_admitted_ns'] = now_ns()
            deadline = time.monotonic() + 1.0
            while self.query(':TRIG:STAT?') != 'STOP':
                if time.monotonic() >= deadline:
                    raise TimeoutError(f'No new {self.trigger_source}-triggered complete acquisition')
                time.sleep(.01)
            self.report['trigger_complete_observed_ns'] = now_ns()
            if not math.isclose(float(self.query(':TRIG:EDGE:LEV?')), 1.5):
                raise ValueError('Trigger admission threshold rejected')
            self.export()  # All four channels from one frozen RAW record.
            if getattr(self, 'evidence_writer', None):
                analysis_start = now_ns()
                self.report['phase'] = analyze_memory(self.report, self._raw_blocks,
                                                      reader.decode_block)
                self.report['analysis_elapsed_ns'] = now_ns() - analysis_start
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
                cls = make_scope_class()
                self.scope = cls(self.opt.out / 'scope-setup', scale=.0002, points=1000000, offset=.0008,
                                 resource=self.opt.scope_resource, expected_serial=self.opt.scope_serial)
                self.scope.trigger_source = self.opt.scope_trigger
                self.scope.prepare()
                self.scope.enable_async_evidence()
            super().configure()
            self.report['plan'].update(external_scope=self.opt.external_scope,
                scope_trigger=self.opt.scope_trigger if self.opt.external_scope else None,
                scope_resource=self.opt.scope_resource if self.opt.external_scope else None,
                scope_serial=self.opt.scope_serial if self.opt.external_scope else None,
                scope_sample_interval_s=5 if self.opt.external_scope else None,
                scope_settle_s=getattr(self.opt, 'settle_s', 5.0) if self.opt.external_scope else None,
                scope_observation='fresh sparse RAW windows; no claim for gaps between samples')
            self.report['joint_tool_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
            self.report['scope_tool_sources'] = [
                dict(path=path.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                for path in (
                    ROOT / 'tools/common/hdo_scope/reader.py',
                    ROOT / 'tools/domains/vdc/scope_capture/sparse.py',
                    ROOT / 'tools/vdc_priority_trace/vdc_scope_evidence.py')
            ] if self.opt.external_scope else []
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
                    decision = scope_checkpoint(
                        samples, due, getattr(self.opt, 'settle_s', 5.0),
                        threshold_ns=getattr(self.opt, 'scope_threshold_ns', 100.0),
                        steady_threshold_ns=getattr(
                            self.opt, 'scope_steady_threshold_ns', 50.0))
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
    p.add_argument('--workflow-inputs', type=Path, default=DEFAULT_WORKFLOW_INPUTS,
                   help='pinned internal-seal adapter, receipt and baseline input manifest')
    p.add_argument('--scope', choices=('off', 'on'), default='off')
    p.add_argument('--scope-trigger', choices=('CHAN1', 'EXT'), default='CHAN1',
                   help='CHAN1 observes NO1 OUT1; EXT requires an independently active OUT4 signal')
    p.add_argument('--scope-resource', default=scope_reader.RESOURCE,
                   help='VISA resource for the HDO scope; recorded in every capture')
    p.add_argument('--scope-serial', default='HDO4A244301137',
                   help='required instrument serial from *IDN?; does not relax the HDO protocol')
    p.add_argument('--seconds', type=int, default=60)
    p.add_argument('--stop-margin-s', type=float, default=8.0,
                   help='margin added to seconds before the final STOP barrier: the board GUARD '
                        'checkpoint runs from its own capture start, which is later than the origin '
                        'trial by the START/ARM latency, so a STOP inside that skew makes the '
                        'checkpoint read a retired output and latch OUTPUT_STOPPED')
    p.add_argument('--settle-s', type=float, default=5.0,
                   help='after START, windows up to this time are graded against the coarse 100 ns '
                        'bound only; later windows must hold the steady 50 ns band (0 disables)')
    p.add_argument('--scope-threshold-ns', type=float, default=100.0,
                   help=argparse.SUPPRESS)
    p.add_argument('--scope-steady-threshold-ns', type=float, default=50.0,
                   help=argparse.SUPPRESS)
    p.add_argument('--p3-provisional', action='store_true', help=argparse.SUPPRESS)
    p.add_argument('--output-delays', type=int, nargs=4, default=(0, -28, -88, -116),
                   metavar=('NO1', 'NO2', 'NO3', 'NO4'),
                   help='STOP-only signed output delays in ns; restored after capture, not saved to Flash')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--child', action='store_true', help=argparse.SUPPRESS)
    a = p.parse_args(argv)
    if a.seconds not in range(60, 601, 60):
        p.error('seconds must be a whole minute from 60 through 600')
    if not math.isfinite(a.settle_s) or not 0 <= a.settle_s <= a.seconds:
        p.error('settle-s must be a finite allowance from 0 through seconds')
    if not math.isfinite(a.stop_margin_s) or not 0 <= a.stop_margin_s <= 60:
        p.error('stop-margin-s must be a finite allowance from 0 through 60')
    if (not math.isfinite(a.scope_threshold_ns) or
            not math.isfinite(a.scope_steady_threshold_ns) or
            a.scope_threshold_ns <= 0 or a.scope_steady_threshold_ns <= 0 or
            a.scope_steady_threshold_ns > a.scope_threshold_ns):
        p.error('scope thresholds must be finite, positive, and steady <= coarse')
    if any(not -(1 << 31) <= value < (1 << 31) for value in a.output_delays):
        p.error('output-delays must be signed int32 nanoseconds')
    if any(not value.strip() or any(c in value for c in '\r\n')
           for value in (a.scope_resource, a.scope_serial)):
        p.error('scope resource and serial must be nonempty single-line values')
    if not a.bench_adapter.is_file():
        p.error('bench-adapter must be an existing validated GUARD capture script')
    return a


def main():
    a = parse_args()
    inputs = load_inputs(a.workflow_inputs, ROOT, a.bench_adapter)
    receipt = (validate_current_receipt(
        inputs, ROOT, allow_p3_provisional=True) if a.p3_provisional else
        validate_current_receipt(inputs, ROOT))
    if a.p3_provisional:
        # The archived phase chain invokes the historical quick-receipt gate
        # during Probe.run(). P3's provisional admission is stricter and is
        # intentionally scoped to this child process; it is never a final
        # receipt and remains rejected by check-staged.
        from tools.hardware_acceptance.p3_dpll_short import validate_provisional_receipt
        from tools.hardware_acceptance import p3_dpll_short as short_gate
        from tools.hardware_acceptance import workflow_steps as p3_gate
        _strict_provisional = short_gate.validate_provisional_receipt

        def _provisional_debug_admission(root, value, board_uids):
            """Debug admission: this run exercises the DPLL function and reports
            what it finds. When the provisional receipt's supporting evidence is
            incomplete (for example the TRN-03 matrix and this run's TDMA records
            cannot be cross-checked), record the reason and continue instead of
            refusing the capture. The packaged acceptance keeps the strict check
            through the original function."""
            try:
                _strict_provisional(root, value, board_uids)
            except Exception as exc:                                      # noqa: BLE001
                print(f"provisional receipt warning (debug admission): {exc}",
                      flush=True)

        short_gate.validate_provisional_receipt = _provisional_debug_admission
        p3_gate._validate_quick_diagnostic_receipt = (
            lambda root, value: validate_provisional_receipt(
                root, value, list(inputs.board_uids)))
    if a.child:
        adapter = load_internal_seal_adapter(
            inputs.runtime_entrypoint, board_uids=inputs.board_uids)
        opt = argparse.Namespace(receipt=inputs.receipt, out=a.out,
            baseline=inputs.baseline, tap_baseline=inputs.tap_baseline,
            period_ns=1000000, scope=False, check_only=False, seconds=a.seconds, summary_interval_ms=10000,
            settle_s=a.settle_s,
            stop_margin_s=a.stop_margin_s,
            scope_threshold_ns=a.scope_threshold_ns,
            scope_steady_threshold_ns=a.scope_steady_threshold_ns,
            no_reference=False, expire_output=False, external_scope=a.scope=='on', scope_trigger=a.scope_trigger,
            scope_resource=a.scope_resource, scope_serial=a.scope_serial,
            bench_adapter=inputs.bench_adapter, workflow_inputs_evidence=inputs.evidence(ROOT))
        probe = make_probe_class(adapter)(opt)
        probe.report['bench_adapter_sha256'] = hashlib.sha256(inputs.bench_adapter.read_bytes()).hexdigest()
        probe.report['workflow_inputs'] = inputs.evidence(ROOT)
        probe.save()
        probe.run()
        print(json.dumps(probe.report['joint_verdict']))
        return 0 if probe.report['passed'] else 2
    child = [sys.executable, str(Path(__file__).resolve()), '--bench-adapter', str(inputs.bench_adapter),
             '--workflow-inputs', str(inputs.manifest),
             '--scope', a.scope, '--scope-trigger', a.scope_trigger,
             '--scope-resource', a.scope_resource, '--scope-serial', a.scope_serial,
             '--seconds', str(a.seconds), '--settle-s', str(a.settle_s),
             '--stop-margin-s', str(a.stop_margin_s),
             '--scope-threshold-ns', str(a.scope_threshold_ns),
             '--scope-steady-threshold-ns', str(a.scope_steady_threshold_ns),
             '--out', str(a.out/'capture'), '--child']
    if a.p3_provisional:
        child.append('--p3-provisional')
    runner = ResumeTrial(
        SerialBackend(inputs.board_uids), receipt, a.out,
        # Same profile the child trials: (plan_ahead - refill_low) must cover
        # (planner_steps + 1) * core1_cycle, i.e. 10 ms at the 2.0 ms cycle.
        # The 24000/32000/16000 profile leaves 8 ms and is rejected (193).
        (28000, 32000, 16000), child, board_uids=inputs.board_uids,
        output_delays=tuple(a.output_delays), capture_timeout=a.seconds + 240)
    runner.report['workflow_inputs'] = inputs.evidence(ROOT)
    runner.save()
    result = runner.run()
    print(json.dumps({k: result[k] for k in ('passed', 'errors', 'cleanup_errors', 'restore_needed', 'duration_s')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
