"""Continuous four-board output; fresh sparse RAW scope snapshots only during RUN."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import traceback

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj

startup = module('continuous_startup', ROOT/'out/HardwareAcceptance/20260918/dpll-extended-window-r1/capture_startup_one_second.py')
base = startup.base
capture = base.capture
phase = capture.phase
follow = phase.follow

def checkpoint_decision(samples, due_s):
    """Fail this diagnostic run at a minute boundary, not the live loop early."""
    window = [row for row in samples if due_s-60 < row['due_s'] <= due_s]
    expected = set(range(due_s-55, due_s+1, 5))
    observed = {row['due_s'] for row in window}
    failures = [dict(sample_s=t, reason='schedule_missing') for t in sorted(expected-observed)]
    for row in window:
        if row.get('missed') or not row.get('capture_complete') or 'phase' not in row:
            failures.append(dict(sample_s=row['due_s'],reason='missing_or_invalid_waveform',detail=row.get('error',row.get('reason'))))
        elif not row['phase']['within_100ns']:
            failures.append(dict(sample_s=row['due_s'],reason='phase_outside_100ns'))
        if row.get('overrun'):
            failures.append(dict(sample_s=row['due_s'],reason='acquisition_overrun'))
    return dict(checkpoint_s=due_s,continue_run=not failures,failures=failures,
        unobserved_intervals_qualified=False,threshold_ns=100)

class SparseScope(base.ConfigurableScope):
    def __init__(self, *args, **kwargs):
        # LongScope's constructor forces 50M points; short windows explicitly
        # keep the requested memory depth while reusing its verified setup.
        base.scope_reader.Scope.__init__(self,*args,**kwargs)

    def prepare(self):
        super().prepare()
        # HDO retains the old 50M-point RAW transfer range after shrinking
        # acquisition memory. NORM->RAW resets that invalid cached range.
        self.write(':WAV:MODE NORM')
        self.write(':WAV:MODE RAW')
        assert self.query(':WAV:STAR?')=='1'
        assert int(float(self.query(':WAV:STOP?')))==self.points
        assert self.query(':SYST:ERR?').startswith(('0,','+0,'))

    def write(self, command):
        # Retain proven slow setup; scope-only runtime commands are serialized
        # by VISA and status responses, with no arbitrary 80ms per-command gap.
        if not getattr(self, 'sampling', False):
            return super().write(command)
        row = dict(command=command, start_ns=time.monotonic_ns())
        self.report['commands'].append(row)
        self.scope.write(command)
        # HDO waveform range/source setters complete asynchronously. Retain
        # the proven settling time; later readbacks must match exactly.
        if command.startswith(':WAV:') and '?' not in command:
            time.sleep(.08)
        row['end_ns'] = time.monotonic_ns()

    def fresh_snapshot(self, folder):
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=False)
        self.report = dict(resource=base.scope_reader.RESOURCE, commands=[], channels=[],
            capture_complete=False, voltage_formula='(code-y_origin-y_reference)*y_increment',
            identity=self.identity, settings=self.settings)
        self.sampling = True
        self.scope.timeout = 1800
        begun = time.monotonic()
        self.write(':STOP')
        assert self.query(':TRIG:STAT?') == 'STOP'
        self.write(':TRIG:EDGE:LEV 7')
        assert math.isclose(float(self.query(':TRIG:EDGE:LEV?')), 7.0)
        self.write(':SING')
        deadline = time.monotonic()+1.0
        while self.query(':TRIG:STAT?') != 'WAIT':
            if time.monotonic() >= deadline:
                raise TimeoutError('Fresh single WAIT was not observed')
            time.sleep(.01)
        assert self.query(':TRIG:SWE?') == 'SING'
        assert self.query(':TRIG:EDGE:SOUR?') == 'CHAN1'
        assert self.query(':SYST:ERR?').startswith(('0,','+0,'))
        self.report['fresh_wait_ns'] = time.monotonic_ns()
        self.report['armed_ns'] = time.monotonic_ns()
        self.write(':TRIG:EDGE:LEV 1.5')
        self.report['trigger_admitted_ns'] = time.monotonic_ns()
        deadline = time.monotonic()+1.0
        while self.query(':TRIG:STAT?') != 'STOP':
            if time.monotonic() >= deadline:
                raise TimeoutError('No new NO1-triggered complete acquisition')
            time.sleep(.01)
        assert math.isclose(float(self.query(':TRIG:EDGE:LEV?')), 1.5)
        # Four channels downloaded from the same frozen record; no re-arm.
        self.export()
        self.report['elapsed_s'] = time.monotonic()-begun
        self.save()
        return self.report

def analyze_window(folder):
    import numpy as np
    report = json.loads((folder/'scope.json').read_text(encoding='utf8'))
    edges = []
    for row in report['channels']:
        p = list(map(float,row['preamble'].split(',')))
        arrays = []
        for block in row['blocks']:
            raw = (folder/block['path']).read_bytes()
            assert hashlib.sha256(raw).hexdigest() == block['sha256']
            arrays.append(np.frombuffer(base.scope_reader.decode_block(raw,block['stop']-block['start']+1),dtype=np.uint8))
        codes = np.concatenate(arrays).astype(float)
        voltage = (codes-p[8]-p[9])*p[7]
        ix = np.flatnonzero((voltage[:-1]<2.5)&(voltage[1:]>=2.5))
        when = p[5]+(ix+(2.5-voltage[ix])/(voltage[ix+1]-voltage[ix])-p[6])*p[4]
        # Ignore repeated threshold crossings within one high pulse, but
        # retain all separated rising pulses and their actual sample spacing.
        when = when[np.r_[True,np.diff(when)>1e-5]] if len(when) else when
        if len(when)<1:
            raise ValueError(f'No actual 2.5V rising edge on CH{row["channel"]}')
        edges.append(when)
    result = dict(edges_s=[v.tolist() for v in edges],relative_ns=[],pairing='nearest periodic edge; absolute ordinal unproven')
    for signal in edges[1:]:
        delta = [float(signal[np.argmin(abs(signal-t))]-t)*1e9 for t in edges[0]]
        result['relative_ns'].append(delta)
    result['large_phase_ambiguity'] = any(abs(d)>100000 for a in result['relative_ns'] for d in a)
    result['within_100ns'] = all(abs(d)<=100 for a in result['relative_ns'] for d in a)
    result['within_50ns'] = all(abs(d)<=50 for a in result['relative_ns'] for d in a)
    (folder/'phase.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    return result

class Probe(capture.RunProbe):
    def __init__(self, opt):
        super().__init__(opt)
        self.deadline = time.monotonic()+opt.seconds+240
        self.samples = []
        self.report['monitor_script_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        self.report['plan'].update(output_duration_ms=0,run_seconds=opt.seconds,
            internal_trace=False,run_status_schema=11,sample_interval_s=5,
            checkpoints_s=list(range(60,opt.seconds+1,60)),runtime_board_queries=False,
            stop_on_failed_checkpoint=True)

    def error(self, scope, exc):
        super().error(scope,exc)
        self.report['errors'][-1]['traceback']=traceback.format_exc()
        self.save()

    def control(self, board, text, expected='OK'):
        if text.upper() == follow.parent.SYNC.upper()+' 0':
            follow.FollowProbe.control(self,board,phase.PHASE+' 0',0)
        result = follow.FollowProbe.control(self,board,text,expected)
        if text.upper().startswith(follow.parent.SYNC.upper()+' ') and int(text.split()[-1])>0 and board==self.boards[0]:
            for index,participant in enumerate(self.boards):
                if index:
                    follow.FollowProbe.control(self,participant,phase.PHASE+' 1',1)
                    assert int(self.command(participant,phase.PHASE+'?').strip('"')) == 1
                row = self.report['boards'][participant.address]
                row['run_prepare_requested'] = True
                self.save()
                row['run_request'] = int(self.command(participant,f'{capture.PREFIX} 1000000,2000,0').strip('"'))
                assert row['run_request']>0
        return result

    def configure(self):
        # No diagnostic trace ARM: normal Match/FOLLOW remains the live loop.
        for board in self.boards:
            status = self.trace_status(board)
            if status['state'] != 0:
                raise RuntimeError('Prior native capture is not released; preserve it')
        self.scope = SparseScope(self.opt.out/'scope-setup',scale=.0002,points=1000000,offset=.0008)
        self.scope.prepare()
        self.scope.identity = self.scope.report['identity']
        self.scope.settings = self.scope.report['settings']
        super().configure()

    def acquire(self):
        self.block_queries=True
        self.barrier=False
        self.scope.quiet=True
        startup.parallel_start(self)
        time.sleep(.2)
        epoch = int(time.time()) & 0xffffffff
        # Origin DMA latches are required live timestamps, not optional trace
        # sampling; do not select NORECORD, which removes those latches.
        grant = int(self.command(self.boards[0],f'CALibration:ORIGin:TRIAL {epoch},8192,256,0').strip('"'))
        assert grant and not grant&1
        self.report['trial_epoch']=grant
        self.scope.quiet=False
        started=time.monotonic()
        self.report['monitor_started_monotonic']=started
        self.report['samples']=self.samples
        next_due=5
        while next_due<=self.opt.seconds:
            while time.monotonic()<started+next_due:
                time.sleep(max(0,min(.2,started+next_due-time.monotonic())))
            row=dict(due_s=next_due,started_s=time.monotonic()-started)
            self.samples.append(row)
            self.save()
            folder=self.opt.out/f'sample-{next_due:04d}'
            try:
                acquired=self.scope.fresh_snapshot(folder)
                row.update(capture_complete=True,phase=analyze_window(folder),capture_elapsed_s=acquired['elapsed_s'])
            except (TimeoutError,AssertionError,ValueError) as exc:
                row['error']=repr(exc)
                row['missed']=True
                row['traceback']=traceback.format_exc()
                row['scope_error_queue']=[]
                self.scope.write(':STOP')
                for _ in range(16):
                    err=self.scope.query(':SYST:ERR?')
                    row['scope_error_queue'].append(err)
                    if err.startswith(('0,','+0,')):break
                else:raise RuntimeError('Scope error queue did not drain')
                self.save()
                # Missing samples never reuse the preceding waveform and
                # never stop the board loop merely for measurement quality.
            row['ended_s']=time.monotonic()-started
            row['overrun']=row['ended_s']>next_due+5
            self.save()
            if next_due % 60 == 0:
                decision = checkpoint_decision(self.samples,next_due)
                (self.opt.out/f'checkpoint-{next_due}.json').write_text(json.dumps(dict(
                    elapsed_s=row['ended_s'],samples=self.samples,**decision),indent=2),encoding='utf8')
                self.report.setdefault('checkpoint_decisions',[]).append(decision)
                if not decision['continue_run']:
                    self.report['checkpoint_stop']=decision
                    self.save()
                    print(json.dumps(dict(checkpoint_stop=decision)),flush=True)
                    break
            print(json.dumps(dict(sample_s=next_due,phase_ns=row.get('phase',{}).get('relative_ns'),elapsed_s=row.get('capture_elapsed_s'),missed=row.get('missed',False))),flush=True)
            next_due+=5
            while next_due<time.monotonic()-started and next_due<=self.opt.seconds:
                self.samples.append(dict(due_s=next_due,missed=True,reason='previous acquisition/transfer overrun; no catch-up'))
                if next_due % 60 == 0:
                    decision=checkpoint_decision(self.samples,next_due)
                    self.report['checkpoint_stop']=decision
                    self.report.setdefault('checkpoint_decisions',[]).append(decision)
                    (self.opt.out/f'checkpoint-{next_due}.json').write_text(json.dumps(decision,indent=2),encoding='utf8')
                    self.save()
                    break
                next_due+=5
            if self.report.get('checkpoint_stop'):
                break
        self.report['monitor_elapsed_s']=time.monotonic()-started

    def collect(self):
        assert self.barrier and not self.block_queries
        for board in self.boards:
            row=self.report['boards'][board.address]
            for key, command in (('final_follow_raw',follow.STATUS),
                    ('final_phase_raw',phase.PHASE+':STATus?'),
                    ('final_model_raw',follow.parent.MODEL)):
                row[key]=self.command(board,command)
            if not row.get('run_prepare_requested'):
                continue
            raw=row['run_output_raw']=self.command(board,capture.PREFIX+'?')
            values=list(map(int,follow.parent.fields(raw)))
            assert len(values)==len(capture.FIELDS) and values[0]==11
            run=row['run_output']=dict(zip(capture.FIELDS,values))
            assert run['generation']==row['run_request'] and run['state'] in (0,4)
            assert run['duration_ms']==run['expires_tick']==0 and run['blocks']>1
            assert run['pio_enabled']==run['dma_busy']==0
            row['output_terminated_by_stop']=run['reason']==1
            if not row['output_terminated_by_stop']:
                self.error('output-retired-early:'+board.address,RuntimeError(str(run['reason'])))
        self.report['passed']=any(r.get('capture_complete') for r in self.samples) and not self.report['errors'] and not self.report.get('checkpoint_stop')
        self.save()

    def run(self):
        super().run()
        self.report['conclusion_scope']='Continuous operation with fresh sparse external windows; phase outside observed windows and absolute ordinal are unqualified. Missed samples remain explicit.'
        self.save()

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=600)
    p.add_argument('--child',action='store_true')
    p.add_argument('--scope-check',action='store_true')
    a=p.parse_args()
    if a.scope_check:
        scope=SparseScope(a.out,scale=.0002,points=1000000,offset=.0008)
        try: scope.prepare()
        finally:
            for cmd in (':STOP',':TRIG:EDGE:SOUR EXT',':TRIG:SWE NORM'): scope.write(cmd)
            scope.close()
        return 0
    if a.child:
        opt=argparse.Namespace(receipt=ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json',out=a.out,
            baseline=ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json',
            tap_baseline=ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json',
            period_ns=1000000,scope=False,check_only=False,seconds=a.seconds)
        probe=Probe(opt)
        probe.run()
        print(json.dumps(dict(passed=probe.report['passed'],errors=probe.report['errors'])))
        return 0 if probe.report['passed'] else 2
    receipt=base.trial.hil.validate_receipt(ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json')
    runner=base.ResumeTrial(base.trial.hil.SerialBackend(),receipt,a.out,(24000,32000,16000),
        [sys.executable,str(Path(__file__).resolve()),'--out',str(a.out/'capture'),'--seconds',str(a.seconds),'--child'],
        output_delays=(0,-28,-88,-160),capture_timeout=a.seconds+240)
    report=runner.run()
    print(json.dumps({k:report[k] for k in ('passed','errors','cleanup_errors','restore_needed','duration_s')}))
    return 0 if report['passed'] else 1

if __name__=='__main__':
    raise SystemExit(main())
