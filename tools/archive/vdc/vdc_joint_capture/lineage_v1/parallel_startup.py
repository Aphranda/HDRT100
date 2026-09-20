"""Arm scope once after a quiet settling interval; no queries during RUN."""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
BASE = ROOT/'out/HardwareAcceptance/20260918/dpll-phase-center-r1/capture_late_scope_schema10.py'
spec = importlib.util.spec_from_file_location('late_arm_base', BASE)
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

class LongScope(base.ConfigurableScope):
    def __init__(self, *args, **kwargs):
        kwargs['points'] = 50000000
        super().__init__(*args, **kwargs)

    def write(self, command):
        if command.startswith(':TIM:SCAL '):
            # Bench evidence: reassert MAIN after RUN; verify completion while
            # boards are STOP. Do not query or reconfigure inside acquisition.
            if self.quiet:
                raise RuntimeError('Timebase setup forbidden during board RUN')
            for cmd in (':TRIG:SWE NORM', ':RUN', ':TIM:MODE MAIN', command, ':TIM:MODE MAIN'):
                base.scope_reader.Scope.write(self, cmd)
                if self.query('*OPC?') != '1':
                    raise RuntimeError('Scope setup command incomplete')
                if not self.query(':SYST:ERR?').startswith(('0,','+0,')):
                    raise RuntimeError('Scope setup command rejected')
            return
        super().write(command)

base.ConfigurableScope = LongScope

def parallel_start(probe):
    backend = base.capture.phase.follow.parent.base.BACKEND
    boards = tuple(probe.boards)
    if len(boards) != 4 or len({b.address for b in boards}) != 4 or len({b.port for b in boards}) != 4:
        raise ValueError('Four distinct identities and ports required')
    if set(b.address for b in boards) != set(base.trial.hil.IDS):
        raise ValueError('Four-board allowlist mismatch')
    if time.monotonic() > probe.deadline or len(probe.report['actions']) + 4 > 2000:
        raise TimeoutError('Bounded work budget exhausted before START')
    connections = []
    for board in boards:
        connection = backend._PERSISTENT_CONNECTIONS.get(board.address)
        if connection is None or connection.session is None or connection.session.ser is None:
            raise RuntimeError('Preverified persistent session required; no reopen')
        if connection.board != board:
            raise RuntimeError('Connection identity mismatch')
        connections.append(connection)
    if len({id(c.session.ser) for c in connections}) != 4:
        raise RuntimeError('Serial transports must be distinct')
    assert probe.block_queries and not probe.barrier and probe.scope.quiet
    probe.report['parallel_start_intents'] = [dict(uid=b.address,port=b.port,command='SYSTem:TDMA:RING:START') for b in boards]
    probe.save()
    def execute(board, connection):
        row = dict(uid=board.address,port=board.port,command='SYSTem:TDMA:RING:START',
                   start_ns=time.monotonic_ns(),utc=datetime.now(timezone.utc).isoformat(),
                   dispatch='one preverified connection per worker')
        try:
            # No discovery, mutation of connection maps, or shared report writes.
            row['raw'] = connection.command(row['command'])
        except Exception as exc:
            row['error'] = f'{type(exc).__name__}: {exc}'
        finally:
            row['end_ns'] = time.monotonic_ns()
        return row
    with ThreadPoolExecutor(max_workers=4) as executor:
        pending = [executor.submit(execute,b,c) for b,c in zip(boards,connections)]
        rows = [f.result() for f in pending]
    # All serial transactions joined before logging or any STOP cleanup.
    rows.sort(key=lambda row:row['start_ns'])
    probe.report['actions'].extend(rows)
    with (probe.opt.out/'actions.jsonl').open('a',encoding='utf-8') as stream:
        for row in rows:
            stream.write(json.dumps(row,ensure_ascii=False)+'\n')
    probe.report['parallel_start_joined_ns'] = time.monotonic_ns()
    probe.save()
    if any(row.get('error') or row.get('raw','').strip().strip('"') != 'OK' for row in rows):
        raise RuntimeError('At least one START lacks explicit OK; all workers joined; STOP before TRIAL')
    return rows


class LateProbe(base.Probe):
    def configure(self):
        super().configure()
        self.report['plan'].update(scope_arm_delay_s=0, quiet_run_seconds=11,
            scope_window='Single CH1 trigger armed before all START; requested -0.1..+0.9 s at 50 Mpts; frozen PRE is authoritative; host delay is not an absolute event timestamp',
            scope_arm_verification='STOP and new WAIT/SING verified before START; completion and PRE verified after STOP')
        self.report['tool_sources'].append(dict(path=str(Path(__file__)),
            sha256=base.capture.phase.follow.parent.base.sha(Path(__file__))))
        self.save()

    def acquire(self):
        self.scope.arm()
        self.report['scope_armed_before_start_ns'] = time.monotonic_ns()
        self.block_queries = True
        self.barrier = False
        self.scope.quiet = True
        parallel_start(self)
        time.sleep(.2)
        epoch = int(time.time()) & 0xffffffff
        grant = int(self.command(self.boards[0],
            f"CALibration:ORIGin:TRIAL {epoch},8192,256,{self.report['clock_hz']*20}").strip('"'))
        if not grant or grant & 1:
            raise RuntimeError('Temporary origin trial rejected')
        self.report['trial_epoch'] = grant
        begun = time.monotonic()
        self.report['quiet_wait_started_monotonic'] = begun
        self.save()
        time.sleep(11)
        self.report['quiet_wait_elapsed_s'] = time.monotonic()-begun

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--child', action='store_true')
    parser.add_argument('--output-delay', default='0,0,0,0')
    args = parser.parse_args()
    try:
        output_delays = tuple(int(value.strip()) for value in args.output_delay.split(','))
    except ValueError as exc:
        raise SystemExit('--output-delay requires four signed decimal integers') from exc
    if len(output_delays) != 4 or any(not -(1 << 31) <= value < (1 << 31) for value in output_delays):
        raise SystemExit('--output-delay requires four signed 32-bit integers')
    receipt_path = ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json'
    if args.child:
        opt = argparse.Namespace(receipt=receipt_path, out=args.out,
            baseline=ROOT/'out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/stopped-baseline.json',
            tap_baseline=ROOT/'out/HardwareAcceptance/20260916/dpll-event-recovery-r1/capture-r2/input-probe.json',
            period_ns=1000000, scope=False, check_only=False, seconds=11,
            profile=(24000,32000,16000), scope_offset=.4, scope_scale=.1)
        probe = LateProbe(opt)
        probe.run()
        print(json.dumps(dict(passed=probe.report['passed'], errors=probe.report['errors'])))
        return 0 if probe.report['passed'] else 2
    receipt = base.trial.hil.validate_receipt(receipt_path)
    runner = base.ResumeTrial(base.trial.hil.SerialBackend(), receipt, args.out,
        (24000,32000,16000), [sys.executable, str(Path(__file__).resolve()),
        '--out', str(args.out/'capture'), '--child'], output_delays=output_delays, capture_timeout=420)
    runner.report.update(script_sha256=base.capture.phase.follow.parent.base.sha(Path(__file__)),
        source_tree_sha256=receipt['source_tree_sha256'], output_delay_requested_ns=list(output_delays),
        scope_arm_delay_s=0, quiet_acquisition_seconds=11, scope_trigger='CH1 rising',
        scope_scale=.1, scope_offset=.4, scope_points=50000000)
    runner.save()
    report = runner.run()
    print(json.dumps({k:report[k] for k in ('passed','errors','cleanup_errors','restore_needed','duration_s')}))
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
