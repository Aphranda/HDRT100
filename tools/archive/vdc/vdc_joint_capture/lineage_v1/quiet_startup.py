"""Existing quiet capture with optional pre-START management sampling omitted.

The omitted model is explicitly None, never a fabricated SCPI response. All
configuration, owner ACK, identity, STOP/CRC/RELEASE and restore checks remain.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT))
base_path=ROOT/'out/HardwareAcceptance/20260919/internal-summary-probe.py'
spec=importlib.util.spec_from_file_location('startup_quiet_base',base_path)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class Probe(m.Probe):
    def configure(self):
        self.report['startup_adapter']=dict(path=str(Path(__file__).resolve()),
            sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            base_sha256=hashlib.sha256(base_path.read_bytes()).hexdigest(),
            omitted_optional_queries=[])
        self.configuring_without_model_sample=True
        try:
            super().configure()
        finally:
            self.configuring_without_model_sample=False
        assert self.report['model_before_start'] is None
        assert len(self.report['startup_adapter']['omitted_optional_queries'])==1
        self.save()

    def command(self,board,text,unused=None):
        if getattr(self,'configuring_without_model_sample',False) and text==m.follow.parent.MODEL:
            assert board==self.boards[0]
            skipped=self.report['startup_adapter']['omitted_optional_queries']
            assert not skipped, 'Unexpected additional pre-START model consumer'
            skipped.append(dict(uid=board.address,command=text,transmitted=False,
                reason='Optional management snapshot is not a START prerequisite; native evidence is read after STOP.'))
            return None
        return super().command(board,text,unused)


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=60);p.add_argument('--child',action='store_true')
    a=p.parse_args();m.helper.validate_duration(a.seconds,10000)
    if a.child:
        opt=argparse.Namespace(receipt=ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json',out=a.out,
            baseline=ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json',
            tap_baseline=ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json',
            period_ns=1000000,scope=False,check_only=False,seconds=a.seconds,summary_interval_ms=10000)
        probe=Probe(opt);probe.run()
        print(json.dumps(dict(passed=probe.report['passed'],errors=probe.report['errors'])))
        return 0 if probe.report['passed'] else 2
    receipt=m.external.base.trial.hil.validate_receipt(ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json')
    runner=m.external.base.ResumeTrial(m.external.base.trial.hil.SerialBackend(),receipt,a.out,(24000,32000,16000),
        [sys.executable,str(Path(__file__).resolve()),'--out',str(a.out/'capture'),'--seconds',str(a.seconds),'--child'],
        output_delays=(0,-28,-88,-116),capture_timeout=a.seconds+180)
    result=runner.run()
    print(json.dumps({k:result[k] for k in ('passed','errors','cleanup_errors','restore_needed','duration_s')}))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
