"""Schema3 GUARD with owner-sealed native records; quiet RUN until ring STOP."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
import sys
import time
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT))
SOURCE=ROOT/'out/HardwareAcceptance/20260919/internal-startup-r1/capture.py'
spec=importlib.util.spec_from_file_location('guard_quiet_base',SOURCE)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
base=m.m
FIELDS=('schema capture_id session generation target_s state reason_mask first_failure_ms '
        'checked_s passed_mask elapsed_ms ring_config_seq stop_config_seq stop_accepted '
        'ring_retired output_retired output_request output_state output_reason output_ordinal_lo output_ordinal_hi').split()

OUTPUT_FLAGS=(1<<22)|(1<<23)|(1<<24)
MISSING_FLAGS=2|128|32|(1<<17)
PLANNED_FIELDS=('planned_calls planned_submissions planned_rebuilds planned_wall_samples '
                'planned_wall_max_cycles planned_budget_overruns').split()
RUN_FIELDS=tuple(base.capture.FIELDS)+tuple(PLANNED_FIELDS)
assert len(base.capture.FIELDS)==119 and len(RUN_FIELDS)==125
PLANNED_BUDGET_CYCLES=100000  # This diagnostic's fixed strict acceptance limit.


def parse_run_output(raw):
    tokens=[v.strip().strip('"') for v in raw.split(',')]
    if len(tokens)!=len(RUN_FIELDS) or not all(re.fullmatch(r'[+-]?[0-9]+',v) for v in tokens):
        raise ValueError('RUN schema12 requires exactly 125 decimal integer fields')
    run=dict(zip(RUN_FIELDS,map(int,tokens)))
    if run['schema']!=12: raise ValueError('Expected RUN schema12')
    for name,value in run.items():
        if name=='delay_ns':
            if not -(1<<31)<=value<(1<<31): raise ValueError('Signed delay outside int32')
        else:
            bits=64 if name in base.capture.WIDE_FIELDS else 32
            if not 0<=value<(1<<bits): raise ValueError(f'RUN field outside uint{bits}: {name}')
    return run


def qualify_planned(run):
    checks=dict(calls=run['planned_calls']>0,
        complete_wall_samples=run['planned_wall_samples']==run['planned_calls'],
        rebuilds=run['planned_rebuilds']>0,submissions=run['planned_submissions']>0,
        wall_budget=run['planned_wall_max_cycles']<=PLANNED_BUDGET_CYCLES,
        no_overruns=run['planned_budget_overruns']==0)
    return dict(diagnostics={k:run[k] for k in PLANNED_FIELDS},checks=checks,
        passed=all(checks.values()),budget_cycles=PLANNED_BUDGET_CYCLES)


def add_replan_report(report):
    rows=[]
    for uid,row in report['boards'].items():
        if 'run_output_raw' not in row: continue
        run=parse_run_output(row['run_output_raw'])
        rows.append(dict(uid=uid,**qualify_planned(run)))
    criteria=len(rows)==4 and all(r['passed'] for r in rows)
    report['planned_path_diagnostics']=rows
    report['strict_replan_qualification']=dict(criteria_passed=criteria,
        base_health_passed=bool(report['passed']),passed=bool(criteria and report['passed']),
        budget_cycles=PLANNED_BUDGET_CYCLES,
        scope='Measured planned CPU path only; not physical GPIO precision or full TDMA WCET.')
    # report.passed remains the original complete guard/health/cleanup verdict.


def trial_passed(report,checks,mode):
    return bool(not report['errors'] and not report.get('cleanup_errors') and
        report.get('queries_during_acquisition',0)==0 and len(checks)==4 and
        all(c['passed'] for c in checks) and
        (mode!='positive' or report['internal_health_passed']))


def evaluate_board(row,decoded,index,seconds,mode):
    """Closed-set trial policy; never convert arbitrary diagnostic failure to PASS."""
    g=row['guard'];a=row['internal_assessment'];ack=row['trace_arm_ack']['status']
    status=decoded['status'];records=decoded['records']
    run=parse_run_output(row['run_output_raw'])
    hz=status['tick_hz'];first=records[0]['observed_start_raw'] if records else 0
    no4=mode=='expiry' and index==3
    negative=mode!='positive'
    identity=all(g[k]==ack[k]==status[k] for k in ('capture_id','session','generation'))
    stop=row['trace_stop_ack']['status'];release=row['trace_release_ack']['status']
    common=dict(identity=identity,target=g['schema']==3 and g['target_s']==seconds,
        native=a['crc_verified'] and a['schema_ok'] and a['interval_ok'] and a['terminal'] and
            a['freeze_reason']==(8 if g['state']==3 else 1) and bool(records) and hz>0,
        trace_retirement=stop['state']==3 and stop['command']==2 and
            stop['capture_id']==g['capture_id'] and stop['request_seq']==stop['ack_seq'] and
            release['state']==0 and release['command']==3 and release['request_seq']==release['ack_seq'],
        output_identity=run['schema']==12 and run['generation']==g['output_request']==row['run_request'] and
            row['run_request']>0 and run['session']==g['session'],
        output_retired=run['state'] in (0,4) and run['pio_enabled']==0 and run['dma_busy']==0,
        output_duration=run['duration_ms']==(20000 if no4 else 0),
        output_reason=run['reason']==(7 if no4 else 1),
        output_activity=mode=='missing' or run['edges']>0)
    # Clock/counter/service faults are never explained by missing reference.
    forbidden={'SERVICE_GAP','COUNTER_RESET','CLOCK_INVALID','COUNTER_SATURATED'}
    common['native_faults']=not any(a['flags'].get(k,0) for k in forbidden)
    common['record_faults']=all(not forbidden.intersection(r['flag_names']) and
        ('FIELD_SATURATED' not in r['flag_names'] or
         r['max_success_gap_ticks']==0xffffffff) for r in records)
    common['coverage_to_decision']=a['observed_s']+0.01>=g['elapsed_ms']/1000
    if not negative:
        verdict=(g['state']==3 and g['reason_mask']==0 and g['checked_s']==seconds and
            g['passed_mask']==(1<<(seconds//60))-1 and seconds*1000<=g['elapsed_ms']<(seconds+1)*1000)
        health=a['coverage_complete'] and a['all_bins_have_success'] and a['success_gap_passed']
    elif mode=='missing':
        verdict=(g['state']==4 and g['reason_mask']&130==130 and
            not g['reason_mask']&~(MISSING_FLAGS|OUTPUT_FLAGS) and
            g['checked_s']==60 and g['passed_mask']==0 and 60000<=g['elapsed_ms']<61000 and
            g['output_reason'] in (0,1))
        health=a['success_count']==0 and a['flags']['NO_SUCCESS']>0 and a['flags']['UNBOUND']>0
    else:
        # NO4 expires at 20 s but reference transport must remain healthy to
        # its 60 s decision. Peers may lose reference only after that STOP.
        healthy_prefix=[]
        for r in records:
            end=(r['observed_end_raw']-first)/hz
            if end<=58 and r['observed_end_raw']>r['observed_start_raw']:
                healthy_prefix.append(r)
            if set(r['flag_names'])&{'NO_SUCCESS','UNBOUND','FIELD_SATURATED'}:
                if (r['observed_start_raw']-first)/hz<58: common['record_faults']=False
        health=(bool(healthy_prefix) and max((r['observed_end_raw']-first)/hz for r in healthy_prefix)>=49 and
            all(r['success_count']>0 and r['max_success_gap_ticks']<=hz for r in healthy_prefix))
        if no4:
            verdict=(g['state']==4 and g['checked_s']==60 and g['passed_mask']==0 and
                60000<=g['elapsed_ms']<61000 and g['output_reason']==7 and g['output_state'] in (0,4) and
                g['reason_mask']==((1<<22)|(1<<23)))
        elif g['state']==3:
            verdict=(g['reason_mask']==0 and g['checked_s']==seconds and
                g['passed_mask']==(1<<(seconds//60))-1 and seconds*1000<=g['elapsed_ms']<(seconds+1)*1000)
        else:
            # Only the first checkpoint after NO4's STOP is an accepted
            # propagated failure; unrelated read/identity/clock flags fail.
            verdict=(g['state']==4 and g['checked_s']==120 and g['passed_mask']==1 and
                120000<=g['elapsed_ms']<121000 and g['reason_mask']!=0 and
                not g['reason_mask']&~(MISSING_FLAGS|OUTPUT_FLAGS) and
                bool(g['reason_mask']&(2|128|(1<<17))) and g['output_reason'] in (0,1))
    common.update(verdict=verdict,health=health,
        target_seal=(g['state']!=3 or (status['state']==3 and status['reason']==8 and
            a['observed_s']>=seconds and a['observed_s']<seconds+1)),
        stop_policy=all(g[k]==(1 if g['state']==4 else 0) for k in
            ('stop_accepted','ring_retired','output_retired')))
    return dict(uid=row['uid'],checks=common,passed=all(common.values()),
        endpoint={k:run[k] for k in ('generation','session','state','reason','duration_ms','edges','pio_enabled','dma_busy')})


class Probe(m.Probe):
    def command(self,board,text,unused=None):
        prefix=base.trace+':SUMMary:'
        if text.upper().startswith(prefix.upper()):
            role='ORIGin' if board==self.boards[0] else 'PHASe'
            cid=text.split(' ',1)[1].split(',')[0]
            text=f'{base.trace}:GUARd:{role} {cid},{self.opt.seconds}'
            base.follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[text.split()[0].upper()]=1
        if self.opt.expire_output and board==self.boards[3] and text.upper().startswith('SYSTEM:VDC:OUTPUT:RUN '):
            assert text.endswith(',0')
            text=text[:-1]+'20000'
        return super().command(board,text,unused)

    def configure(self):
        super().configure()
        self.report['plan']['run_status_schema']=12
        self.report['guard_plan']=dict(target_s=self.opt.seconds,
            checkpoints_s=list(range(60,self.opt.seconds+1,60)),no_reference=self.opt.no_reference,expire_output=self.opt.expire_output,
            runtime_queries=0,scope=False,autonomous_local_stop_on_failure=True,
            simultaneous_four_board_stop=False,
            script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        self.save()

    def acquire(self):
        self.block_queries=True;self.barrier=False;self.scope=SimpleNamespace(quiet=True)
        try: base.external.startup.parallel_start(self)
        finally: self.scope=None
        time.sleep(.2)
        if not self.opt.no_reference:
            epoch=int(time.time()) & 0xffffffff
            grant=int(self.command(self.boards[0],f'CALibration:ORIGin:TRIAL {epoch},8192,256,0').strip('"'))
            assert grant and not grant&1
            self.report['trial_epoch']=grant
        else:
            self.report['trial_intentionally_omitted']=True
        self.save()
        begin=time.monotonic()
        base.helper.quiet_wait(self.opt.seconds+2)
        self.report['quiet_elapsed_s']=time.monotonic()-begin

    def collect(self):
        assert self.barrier and not self.block_queries
        for board in self.boards:
            raw=self.command(board,base.trace+':GUARd?')
            values=[int(v.strip().strip('"')) for v in raw.split(',')]
            assert len(values)==len(FIELDS)
            self.report['boards'][board.address]['guard_raw']=raw
            self.report['boards'][board.address]['guard']=dict(zip(FIELDS,values))
            self.save()
        super().collect()
        self.report['internal_health_passed']=self.report['passed']
        mode='missing' if self.opt.no_reference else 'expiry' if self.opt.expire_output else 'positive'
        checks=[]
        for index,board in enumerate(self.boards):
            row=self.report['boards'][board.address]
            row['uid']=board.address
            # Re-decode the actual downloaded bytes. The verdict never relies
            # only on mutable report booleans or a stale decoded JSON file.
            data=(self.opt.out/f'NO{index+1}'/'summary.ram.bin').read_bytes()
            decoded=base.helper.decode(data,row['trace_capture_id'])
            checks.append(evaluate_board(row,decoded,index,self.opt.seconds,mode))
        self.report['guard_checks']=checks
        self.report['output_endpoints']=[dict(uid=c['uid'],**c['endpoint']) for c in checks]
        self.report['expected_missing_reference']=self.opt.no_reference
        self.report['expected_output_expiry']=self.opt.expire_output
        self.report['passed']=trial_passed(self.report,checks,mode)
        add_replan_report(self.report)
        self.save()

    def run(self):
        try:
            super().run()
        finally:
            # A restore failure may occur after collect. Recompute qualification
            # only after inherited cleanup has finalized the health verdict.
            if 'strict_replan_qualification' in self.report:
                q=self.report['strict_replan_qualification']
                q['base_health_passed']=bool(self.report['passed'])
                q['passed']=bool(q['criteria_passed'] and self.report['passed'])
            self.report['conclusion_scope']=('Internal SRAM reference/follow coverage and diagnostic '
                'and output engine checkpoint verdicts. FAIL requests local retirement; PASS leaves the ring running '
                'until final host STOP. No runtime queries or scope acquisition; actual GPIO precision '
                'is unqualified; engine state is not GPIO evidence. A negative injection pass means the expected '
                'selected failure and retirement were observed, not healthy DPLL operation.')
            self.save()


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=60);p.add_argument('--child',action='store_true')
    p.add_argument('--no-reference',action='store_true');p.add_argument('--expire-output',action='store_true');a=p.parse_args()
    assert not (a.no_reference and a.expire_output)
    assert 60<=a.seconds<=600 and a.seconds%60==0
    if a.child:
        opt=argparse.Namespace(receipt=ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json',out=a.out,
            baseline=ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json',
            tap_baseline=ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json',
            period_ns=1000000,scope=False,check_only=False,seconds=a.seconds,summary_interval_ms=10000,
            no_reference=a.no_reference,expire_output=a.expire_output)
        probe=Probe(opt);probe.run()
        print(json.dumps(dict(passed=probe.report['passed'],errors=probe.report['errors'],
            strict_replan_qualification=probe.report.get('strict_replan_qualification'))))
        return 0 if probe.report['passed'] else 2
    receipt=base.external.base.trial.hil.validate_receipt(ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json')
    child=[sys.executable,str(Path(__file__).resolve()),'--out',str(a.out/'capture'),'--seconds',str(a.seconds),'--child']
    if a.no_reference: child.append('--no-reference')
    if a.expire_output: child.append('--expire-output')
    runner=base.external.base.ResumeTrial(base.external.base.trial.hil.SerialBackend(),receipt,a.out,(24000,32000,16000),
        child,output_delays=(0,-28,-88,-116),capture_timeout=a.seconds+180)
    result=runner.run()
    print(json.dumps({k:result[k] for k in ('passed','errors','cleanup_errors','restore_needed','duration_s')}))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
