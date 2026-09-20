"""Bounded four-board RAM timing trial with journaled STOP/restore finalization."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('timing_trial_hil', HERE/'config-hil/accept_output_timing.py')
hil = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hil)
OUTPUT = 'SYSTem:VDC:OUTPut:RUN'


def child_capture(command, timeout, output):
    # subprocess.run kills and waits for its child on timeout before returning
    # control to our serial cleanup. No inherited serial connections exist.
    with (output/'child.stdout.log').open('x',encoding='utf-8') as stdout, (output/'child.stderr.log').open('x',encoding='utf-8') as stderr:
        return subprocess.run(command, timeout=timeout, stdout=stdout, stderr=stderr).returncode


class Trial(hil.Acceptance):
    def __init__(self, backend, receipt, output, profile, capture_command,
                 child=child_capture, capture_timeout=240.0, cleanup_timeout=120.0):
        super().__init__(backend,receipt,output,deadline_s=120.0)
        self.report.update(schema='DPLL_RAM_TIMING_TRIAL_V1',profile=profile,capture={},
                           limitations=['RAM timing only; no Flash, reboot or P0',
                                        'Child evidence determines waveform/continuity outcome'])
        self.profile = tuple(profile)
        self.capture_command = list(capture_command)
        self.child = child
        self.capture_timeout = capture_timeout
        self.cleanup_timeout = cleanup_timeout
        self.dirty = set()
        self.save()

    def open_board(self, uid):
        event=dict(uid=uid,action='open',stage=self.stage_name,start_ns=time.monotonic_ns())
        try:
            super().open_board(uid)
        except Exception as exc:
            event['error']=repr(exc)
            try:
                self.backend.close(uid)
            except Exception as close_error:
                event['close_error']=repr(close_error)
            self.opened.discard(uid)
            self.verified.discard(uid)
            raise
        finally:
            event['end_ns']=time.monotonic_ns()
            self.report.setdefault('connection_events',[]).append(event)
            self.save()

    def close_all(self):
        failures = []
        # Close every authorized UID with the real backend signature, even if
        # an open failed after partially creating a connection.
        for uid in hil.IDS:
            event = dict(uid=uid,action='close',stage=self.stage_name,start_ns=time.monotonic_ns())
            try:
                self.backend.close(uid)
            except Exception as exc:
                event['error'] = repr(exc)
                failures.append(f'{uid}: {exc!r}')
            finally:
                # SerialBackend.close pops the session before closing it;
                # even a close error therefore requires a fresh connection.
                self.opened.discard(uid)
                self.verified.discard(uid)
                event['end_ns'] = time.monotonic_ns()
                self.report.setdefault('connection_events',[]).append(event)
                self.save()
        hil.require(not failures,f'Close failures: {failures}')

    def barrier(self):
        failures = []
        for uid in hil.IDS:
            try:
                if uid not in self.opened:
                    self.open_board(uid)
            except Exception as exc:
                failures.append(f'{uid}: open: {exc!r}')
        # Send both intents to every reachable board before any query. A failed
        # output STOP cannot prevent TDMA STOP on this or another board.
        for command in (OUTPUT+':STOP',hil.STOP):
            for uid in hil.IDS:
                try:
                    hil.require(uid in self.opened,f'{uid}: no connection')
                    self.ok(uid,command)
                except Exception as exc:
                    failures.append(f'{uid}: {command}: {exc!r}')
        for uid in hil.IDS:
            try:
                hil.require(uid in self.opened,f'{uid}: no connection')
                self.wait_stopped(uid)
                self.identity(uid)
                for _ in range(40):
                    raw = self.command(uid,OUTPUT+'?')
                    fields = raw.split(',')
                    hil.require(len(fields)>=3,'Incomplete output status')
                    if hil.integers(','.join(fields[:3]),3)[2] in (0,4):
                        break
                    self.backend.sleep(.05)
                else:
                    raise RuntimeError(f'{uid}: output not retired')
            except Exception as exc:
                failures.append(f'{uid}: verify STOP/identity: {exc!r}')
        hil.require(not failures,f'All-board STOP barrier failed: {failures}')
        hil.require(self.verified==set(hil.IDS),'All board identities must be verified')

    def set_timing(self,uid,value):
        hil.require(self.verified==set(hil.IDS),'All current identities required before RAM mutation')
        self.dirty.add(uid)  # A lost acknowledgement may still follow mutation.
        command=hil.BASE+' '+','.join(map(str,value))
        hil.require(hil.pair(self.command(uid,command))==tuple(value),f'{uid}: SET readback mismatch')
        hil.require(hil.pair(self.command(uid,hil.BASE+'?'))==tuple(value),f'{uid}: query readback mismatch')
        self.clean_errors(uid)

    def restore_ram(self):
        failures=[]
        for uid in hil.IDS:
            if uid not in self.dirty:
                continue
            try:
                hil.require(uid in self.initial,f'{uid}: original RAM unavailable')
                original=self.initial[uid]['output_timing']
                self.set_timing(uid,original)
                self.report['boards'][uid]['restored']=original
                self.dirty.discard(uid)
            except Exception as exc:
                failures.append(f'{uid}: {exc!r}')
        hil.require(not failures,f'RAM restoration failures: {failures}')

    def run(self):
        started=time.monotonic_ns()
        try:
            with self.stage('initial_output_and_tdma_stop'):
                self.barrier()
            with self.stage('original_ram_and_invariants'):
                for uid in hil.IDS:
                    self.initial[uid]=self.snapshot(uid)
                    self.report['boards'][uid]['initial']=self.initial[uid]
                    self.clean_errors(uid)
            with self.stage('apply_ram_timing'):
                for uid in hil.IDS:
                    self.set_timing(uid,self.profile)
                    self.report['boards'][uid]['applied']=self.profile
            with self.stage('close_before_capture'):
                self.close_all()
            with self.stage('child_capture'):
                self.report['capture']=dict(command=self.capture_command,timeout_s=self.capture_timeout,
                                            started_ns=time.monotonic_ns())
                self.save()
                code=self.child(self.capture_command,self.capture_timeout,self.output)
                self.report['capture'].update(returncode=code,ended_ns=time.monotonic_ns())
                hil.require(code==0,f'Capture exited {code}')
            self.report['main_completed']=True
        except (Exception,KeyboardInterrupt) as exc:
            self.report['errors'].append(dict(stage=getattr(self,'failure_stage',self.stage_name),error=repr(exc)))
        finally:
            self.deadline=time.monotonic()+self.cleanup_timeout
            try:
                with self.stage('final_output_and_tdma_stop'):
                    self.barrier()
            except (Exception,KeyboardInterrupt) as exc:
                self.report['cleanup_errors'].append(dict(stage='final_stop',error=repr(exc)))
            else:
                for name,action in (('restore_original_ram',self.restore_ram),
                                    ('final_readbacks',self.verify_final)):
                    try:
                        with self.stage(name):action()
                    except (Exception,KeyboardInterrupt) as exc:
                        self.report['cleanup_errors'].append(dict(stage=name,error=repr(exc)))
            try:
                with self.stage('close_final_connections'):self.close_all()
            except (Exception,KeyboardInterrupt) as exc:
                self.report['cleanup_errors'].append(dict(stage='close',error=repr(exc)))
            self.report['restore_needed']=sorted(self.dirty)
            self.report['complete']=True
            self.report['passed']=(self.report['main_completed'] and not self.report['errors'] and
                                   not self.report['cleanup_errors'] and not self.dirty)
            self.report['duration_s']=(time.monotonic_ns()-started)/1e9
            self.save()
        return self.report


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--capture-out',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,default=hil.ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json')
    parser.add_argument('--baseline',type=Path,default=hil.ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json')
    parser.add_argument('--tap-baseline',type=Path,default=hil.ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json')
    parser.add_argument('--plan',type=int,default=16000)
    parser.add_argument('--commit',type=int,default=24000)
    parser.add_argument('--low',type=int,default=8000)
    parser.add_argument('--period-ns',type=int,default=1000000)
    parser.add_argument('--capture-timeout-s',type=float,default=240.0)
    parser.add_argument('--scope',action='store_true')
    args=parser.parse_args(argv)
    hil.pair(f'{args.plan},{args.commit},{args.low}')
    hil.require(args.capture_timeout_s>0,'Capture timeout must be positive')
    hil.require(1000000<=args.period_ns<=100000000,'Invalid output period')
    out,capture=args.out.resolve(),args.capture_out.resolve()
    hil.require(out!=capture and not out.exists() and not capture.exists(),'Evidence directories must be fresh and distinct')
    for path in (args.baseline,args.tap_baseline,HERE/'capture_run_output_schema9.py'):
        hil.require(path.is_file(),f'Required input absent: {path}')
    receipt=hil.validate_receipt(args.receipt)
    command=[sys.executable,str(HERE/'capture_run_output_schema9.py'),'--receipt',str(args.receipt.resolve()),
             '--out',str(capture),'--baseline',str(args.baseline.resolve()),
             '--tap-baseline',str(args.tap_baseline.resolve()),'--period-ns',str(args.period_ns)]
    if args.scope:command.append('--scope')
    runner=Trial(hil.SerialBackend(),receipt,out,(args.plan,args.commit,args.low),command,
                 capture_timeout=args.capture_timeout_s)
    runner.report.update(script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                         receipt_path=str(args.receipt.resolve()),
                         receipt_sha256=hashlib.sha256(args.receipt.read_bytes()).hexdigest())
    report=runner.run()
    print(json.dumps({key:report[key] for key in ('passed','errors','cleanup_errors','restore_needed','duration_s')}))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())

