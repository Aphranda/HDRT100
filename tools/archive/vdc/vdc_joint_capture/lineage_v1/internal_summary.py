"""Bench adapter for the verified DPLL setup, without opening a scope."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
spec = importlib.util.spec_from_file_location('external_proven', ROOT/'out/HardwareAcceptance/20260919/dpll-checkpoint-r2/monitor_external.py')
external = importlib.util.module_from_spec(spec)
spec.loader.exec_module(external)
from tools.vdc_priority_trace import vdc_priority_trace_capture as helper

capture, follow, phase = external.capture, external.follow, external.phase
trace = helper.TRACE_ROOT


class Probe(external.Probe):
    def configure(self):
        for board in self.boards:
            if self.trace_status(board)['state'] != 0:
                raise RuntimeError('Prior trace is not released')
        self.report['plan'].update(internal_trace=True, external_scope=False,
            no_runtime_queries=True, summary_pool_limit_s=600, summary_interval_ms=self.opt.summary_interval_ms)
        capture.RunProbe.configure(self)

    def control(self, board, text, expected='OK'):
        result = external.Probe.control(self, board, text, expected)
        if text.upper().startswith(follow.parent.SYNC.upper()+' ') and int(text.split()[-1])>0 and board==self.boards[0]:
            for i, participant in enumerate(self.boards):
                row = self.report['boards'][participant.address]
                before = self.trace_status(participant)
                cid = max(before['capture_id']+1, self.report['binding_generation']+i)
                row.update(trace_capture_id=cid, trace_arm_requested=True)
                self.save()
                cmd = helper.arm_command(cid, origin=i==0, summary_interval_ms=self.opt.summary_interval_ms)
                follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[cmd.split()[0].upper()] = 1
                follow.FollowProbe.control(self, participant, cmd, cid)
                row['trace_arm_ack'] = self.trace_ack(participant,1,1,cid)
                assert row['trace_arm_ack']['status']['schema'] == helper.summary_schema(i==0,self.opt.summary_interval_ms)
                assert row['trace_arm_ack']['status']['sample_interval_ms'] == (self.opt.summary_interval_ms or 1000)
                self.save()
        return result

    def acquire(self):
        self.block_queries=True
        self.barrier=False
        # Reuse the previously verified bounded parallel START handshake.
        self.scope=SimpleNamespace(quiet=True)
        try:
            external.startup.parallel_start(self)
        finally:
            self.scope=None
        time.sleep(.2)
        epoch=int(time.time()) & 0xffffffff
        grant=int(self.command(self.boards[0],f'CALibration:ORIGin:TRIAL {epoch},8192,256,0').strip('"'))
        assert grant and not grant&1
        self.report['trial_epoch']=grant
        self.save()
        begin=time.monotonic()
        helper.quiet_wait(self.opt.seconds)
        self.report['quiet_elapsed_s']=time.monotonic()-begin

    def collect(self):
        assert self.barrier and not self.block_queries
        for board in self.boards:
            row=self.report['boards'][board.address]
            for key,cmd in [('origin_release','READ:CALibration:ORIGin:RELease?'),
                            ('origin_handoff','READ:CALibration:ORIGin:HANDoff?'),
                            ('origin_runtime','READ:CALibration:ORIGin:RUNTIme?'),
                            ('priority_tx',follow.parent.TX),
                            ('match','SYSTem:VDC:PRIORity:MATCH:STATus?')]:
                row[key]=self.command(board,cmd)
        # All ring STOPs complete before any trace STOP or download.
        for board in self.boards:
            row=self.report['boards'][board.address]
            if row.get('trace_arm_requested'):
                self.control(board,trace+':STOP',1)
                row['trace_stop_ack']=self.trace_ack(board,2,3,row['trace_capture_id'])
        assessments=[]
        for i,board in enumerate(self.boards):
            row=self.report['boards'][board.address]
            if not row.get('trace_arm_requested'): continue
            try:
                data,pages=helper.download_capture(lambda c:self.command(board,c),row['trace_capture_id'])
                folder=self.opt.out/f'NO{i+1}'
                folder.mkdir(parents=True,exist_ok=True)
                (folder/'summary.ram.bin').write_bytes(data)
                (folder/'read-pages.json').write_text(json.dumps(pages,indent=2),encoding='utf-8')
                decoded=helper.decode(data,row['trace_capture_id'])
                (folder/'decoded.json').write_text(json.dumps(decoded,indent=2),encoding='utf-8')
                assessment=helper.assess_capture(decoded,self.opt.seconds,i==0,summary_interval_ms=self.opt.summary_interval_ms)
                row['internal_assessment']=assessment
                assessments.append(assessment)
                self.control(board,trace+':RELease',1)
                row['trace_release_ack']=self.trace_ack(board,3,0)
                row['run_output_raw']=self.command(board,capture.PREFIX+'?')
                row['final_follow_raw']=self.command(board,follow.STATUS)
                row['final_phase_raw']=self.command(board,phase.PHASE+':STATus?')
            except Exception as exc:
                self.error('internal:'+board.address,exc)
            self.save()
        self.report['passed']=len(assessments)==4 and not self.report['errors'] and all(
            a['coverage_complete'] and a['all_bins_have_success'] and a['success_gap_passed'] for a in assessments)
        self.report['physical_lock_qualified']=False
        self.save()


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=60)
    p.add_argument('--child',action='store_true')
    p.add_argument('--summary-interval-ms',type=int,default=10000)
    a=p.parse_args()
    helper.validate_duration(a.seconds,a.summary_interval_ms)
    if a.child:
        opt=argparse.Namespace(receipt=ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json',out=a.out,
            baseline=ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json',
            tap_baseline=ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json',
            period_ns=1000000,scope=False,check_only=False,seconds=a.seconds,summary_interval_ms=a.summary_interval_ms)
        probe=Probe(opt)
        probe.run()
        print(json.dumps(dict(passed=probe.report['passed'],errors=probe.report['errors'])))
        return 0 if probe.report['passed'] else 2
    receipt=external.base.trial.hil.validate_receipt(ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json')
    runner=external.base.ResumeTrial(external.base.trial.hil.SerialBackend(),receipt,a.out,(24000,32000,16000),
        [sys.executable,str(Path(__file__).resolve()),'--out',str(a.out/'capture'),'--seconds',str(a.seconds),'--summary-interval-ms',str(a.summary_interval_ms),'--child'],
        output_delays=(0,-28,-88,-116),capture_timeout=a.seconds+180)
    report=runner.run()
    print(json.dumps({k:report[k] for k in ('passed','errors','cleanup_errors','restore_needed','duration_s')}))
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
