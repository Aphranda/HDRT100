"""Focused four-board T2 bench probe, not a P3 or action-completion receipt.

Promoted unchanged from out/t2-local-cursor-20260927/probe.py, the last chain
that published a T2 position fragment. Usage:
    python tools/sync_trigger/t2_bench_probe.py [OUT_DIR] [--actions] [--scope] [--late-source]
"""
import json
import sys
import time
import struct
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.common.scpi.scpi_serial import SerialSession

PAIRS = [('COM5', '0010071E65B5CB38'), ('COM4', 'FB276192BEF9CCE1'),
         ('COM3', '2BD5090FE009FA2A'), ('COM6', 'A1E549202D18ED6A')]
OUT = (Path(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else
       ROOT / 'out' / 'HardwareAcceptance' / time.strftime('%Y%m%d') / time.strftime('t2-bench-probe-%H%M%S'))
OUT.mkdir(parents=True, exist_ok=False)
sessions = {}
rows = []
scope_mode = '--scope' in sys.argv
actions_mode = '--actions' in sys.argv
late_source = '--late-source' in sys.argv
scope = None
old_output = {}

def cmd(port, text, expected=None):
    for attempt in range(3 if '?' in text else 1):
        raw = sessions[port].execute(text, require_match=False)
        row = dict(port=port, command=text, raw=raw, host=time.monotonic(),attempt=attempt)
        rows.append(row)
        with (OUT/'commands.jsonl').open('a', encoding='utf-8') as f:
            f.write(json.dumps(row)+'\n')
        if '<timeout>' not in raw: break
        report.setdefault('query_retries',[]).append(row)
        time.sleep(.05)
    normalized = raw.strip().strip('"').lstrip(',')
    if '<timeout>' in raw or (normalized.startswith('-') and ',' in normalized) or (expected is not None and normalized != expected):
        raise RuntimeError(str(row))
    return normalized

def barrier():
    for p in sessions:
        cmd(p, 'SYSTem:TDMA:RING:STOP')
    for p in sessions:
        v = [int(x) for x in cmd(p, 'SYSTem:TDMA:RING:STATus?').split(',')]
        assert v[0] == 0 and v[1] == 0 and v[-1] == v[-2], v

report = {'scope':'T2_PREDICTION_ACTION_RECEIPTS' if actions_mode else 'LOCAL_CURSOR_ONLY',
          'passed':False, 'errors':[]}
try:
    for p, uid in PAIRS:
        sessions[p] = SerialSession(p,115200,3,.05,read_timeout_s=.02).open()
        assert uid in cmd(p, '*IDN?')
        cmd(p, 'SYSTem:FW:BUILD?')
    barrier()
    if actions_mode:
        for p,uid in PAIRS:
            cmd(p,'TRIGger:REServation:T2:STOP','1,0')
            cmd(p,'SYSTem:REFMEM:SYNC:TDMA:ABORt')
    if scope_mode:
        from tools.vdc_priority_trace.vdc_priority_joint_capture import make_scope_class
        scope=make_scope_class()(OUT/'scope-setup',scale=.0002,points=1000000,offset=.0008)
        scope.trigger_source='CHAN1'
        scope.prepare()
        scope.enable_async_evidence()
    generation = int(time.time())
    report['session'] = generation
    receipt=json.loads((ROOT/'config/hardware_acceptance/p3_acceptance_receipt.json').read_text(encoding='utf-8'))
    for i,(p,uid) in enumerate(PAIRS):
        cmd(p,'SYSTem:TDMA:LOAD:MASK 27','27')
        cmd(p,'SYSTem:TDMA:FLIGHT:MODE 1')
        cmd(p,'SYSTem:TDMA:FLIGHT:MODE?','2')
        for suffix in ['FOLLow:PHASe','FOLLow','SYNC','MATCH']:
            cmd(p,'SYSTem:VDC:PRIORity:'+suffix+' 0','0')
        cmd(p,'SYSTem:VDC:FEEDback:REFerence 0')
        cmd(p,'SYSTem:VDC:FEEDback:LOCALfollow 0')
        cmd(p,'SYSTem:VDC:FEEDback:SESSion 0')
        cmd(p,'SYSTem:TDMA:RING:DIAGnostic 1','1')
        cmd(p,'SYSTem:TDMA:RING:GEOMetry 0','0')
        cmd(p,'SYSTem:SYNC:VDC:DPLL:PROVisional 1')
        cmd(p,'SYSTem:SYNC:VDC:DPLL:PROVisional?')
        cmd(p,'SYSTem:VDC:FEEDback:SESSion '+str(generation))
        cmd(p,'TRIGger:REServation:GRID:CLEar','1,0')
        cmd(p,f'TRIGger:REServation:GRID:CONFigure {generation},{generation},0,1000000,2000','1,0')
        cmd(p,'SYSTem:TDMA:FLIGHT:FIFO:RESet')
    if actions_mode:
        def crc(words):
            return zlib.crc32(b''.join(struct.pack('<I',v&0xffffffff) for v in words))
        deployment=[];words=[generation]
        for i,(p,uid) in enumerate(PAIRS):
            row=[int(uid,16),i,1 if i in (0,2) else 0,[0,7,8,1][i],1 if i==2 else 0]
            deployment.extend(row)
            words.extend([row[0]&0xffffffff,row[0]>>32,*row[1:]])
        deployment_crc=crc(words)
        templates=[[1,generation,1,1,1,1,-2],
                   [2,generation,2,2,2000,4,0],
                   [3,generation,3,1,1,1,-1]]
        words=[generation,len(templates)]
        for row in templates:words.extend([*row[:6],row[6]&0xffffffff,(row[6]>>32)&0xffffffff])
        template_crc=crc(words)
        for i,(p,uid) in enumerate(PAIRS):
            cmd(p,'TRIGger:REServation:TEMPLate:RELease','1,0')
            cmd(p,'TRIGger:REServation:TEMPLate:CLEar','1,0')
            cmd(p,'TRIGger:REServation:DEPloyment:STAGe '+','.join(map(str,[generation,deployment_crc,*deployment])),'1,0')
            cmd(p,f'TRIGger:REServation:DEPloyment:ACTivate {generation},{deployment_crc}','1,0')
            # T2 STOP clears the cursor; recreate with a fresh grid generation.
            cmd(p,f'TRIGger:REServation:GRID:CONFigure {generation},{generation+1},0,1000000,2000','1,0')
            cmd(p,'TRIGger:REServation:TEMPLate:INSTall '+','.join(map(str,[generation,template_crc,len(templates),*[v for row in templates for v in row]])),'1,0')
            cmd(p,'TRIGger:REServation:TEMPLate:ACTivate','1,0')
            # Debug run keeps the first target beyond model startup while
            # retaining a five degree pre-roll and three independent positions.
            cmd(p,f'TRIGger:REServation:T2:CONFigure {generation},0,14,{int(i==0)},5000,5000,950,5000,10000,15000','1,0')
            cmd(p,f'TRIGger:REServation:T2:STARt {generation},0','1,0')
        cmd(PAIRS[0][0],'TRIGger:REServation:COUNter:CLEar','1,0')
        cmd(PAIRS[0][0],'TRIGger:REServation:COUNter:STARt','1,0')
        cmd(PAIRS[0][0],'TRIGger:REServation:SOURce:RELease','1,0')
        cmd(PAIRS[0][0],'TRIGger:REServation:SOURce:CONFigure 1000','1,0')
        cmd(PAIRS[0][0],'TRIGger:REServation:SOURce?')
    # Leave the shared flight FIFO empty for the position reset. T2 publishes
    # its own normal VDC/T2 frames; a diagnostic seed would hold TX_READY and
    # intentionally backpressure the position descriptor.
    cmd(PAIRS[0][0],'SYSTem:TDMA:RING:BURSt 0','0')
    cmd(PAIRS[0][0],'SYSTem:VDC:PRIORity:SYNC '+str(generation),str(generation))
    for p,uid in PAIRS[1:]:
        cmd(p,'SYSTem:VDC:PRIORity:MATCH '+str(generation),str(generation))
        cmd(p,'SYSTem:VDC:PRIORity:FOLLow 1','1')
        cmd(p,'SYSTem:VDC:PRIORity:FOLLow:PHASe 1','1')
    if scope_mode:
        for i,(p,uid) in enumerate(PAIRS):
            old_output[p]=(cmd(p,'SYSTem:VDC:OUTPut:TIMing?'),cmd(p,'SYSTem:VDC:OUTPut:DELay?'))
            cmd(p,'SYSTem:VDC:OUTPut:TIMing 24000,32000,16000')
            cmd(p,'SYSTem:VDC:OUTPut:DELay '+str([0,-28,-88,-116][i]))
            assert int(cmd(p,'SYSTem:VDC:OUTPut:RUN 1000000,2000,0'))>0
    for p,uid in reversed(PAIRS):
        cmd(p,'SYSTem:TDMA:RING:ARM','OK')
        cmd(p,'SYSTem:TDMA:RING:ARM:STATus?','1')
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda x: cmd(x[0],'SYSTem:TDMA:RING:START','OK'),PAIRS))
    if actions_mode and not late_source:
        cmd(PAIRS[0][0],'TRIGger:REServation:SOURce:STARt','1,0')
    time.sleep(.2)
    grant=cmd(PAIRS[0][0],f'CALibration:ORIGin:TRIAL {generation},8192,256,0')
    assert int(grant)>0 and not int(grant)&1, grant
    time.sleep(12)
    if actions_mode and late_source:
        cmd(PAIRS[0][0],'TRIGger:REServation:SOURce:STARt','1,0')
    for p,uid in PAIRS:
        cmd(p,'SYSTem:TDMA:RING:STATus?')
        cmd(p,'SYSTem:VDC:FEEDback:MODel?')
        cmd(p,'SYSTem:TDMA:SCHEDule?')
        cmd(p,'SYSTem:TDMA:LOAD:MASK 91','91')
    samples=[]
    report['scope_windows']=[]
    begun=time.monotonic()
    for n in range(12 if scope_mode or actions_mode else 4):
        interval=5 if scope_mode else 1 if actions_mode else 2
        time.sleep(max(0,begun+(n+1)*interval-time.monotonic()))
        if scope_mode:
            capture=scope.fresh_snapshot(OUT/f'scope-{n+1:02d}')
            report['scope_windows'].append(capture.get('phase',{}))
            print('scope',n+1,capture.get('phase',{}),flush=True)
        sample={}
        for p,uid in PAIRS:
            sample[p]=cmd(p,'TRIGger:REServation:T2:CURSor?')
            cmd(p,'TRIGger:REServation:TIMing?')
            if actions_mode:
                cmd(p,'TRIGger:REServation:T2?')
                cmd(p,'TRIGger:REServation:T2:ACTion?')
                cmd(p,'SYSTem:REFMEM:SYNC:FLIGHT:POSition?')
            schedule=[int(v) for v in cmd(p,'SYSTem:TDMA:SCHEDule?').split(',')]
            if schedule[5]&64:
                report['errors'].append(f'{p}: SYNC_TRIGGER quarantined; phase={schedule[85:96]}')
        samples.append(sample)
    report['samples']=samples
    if actions_mode:
        report['actions']={p:cmd(p,'TRIGger:REServation:T2:ACTion?') for p,uid in PAIRS}
        cmd(PAIRS[0][0],'TRIGger:REServation:COUNter?')
        cmd(PAIRS[2][0],'TRIGger:REServation:GATEway?')
        for p,uid in PAIRS[1:]:
            a=list(map(int,report['actions'][p].split(',')))
            assert a[2]==7 and a[3]==0 and a[4]==0,(p,a)
    # SCPI result, error, configured, valid; all samples must remain valid.
    parsed={p:[[int(v) for v in sample[p].split(',')] for sample in samples] for p,_ in PAIRS}
    for p,values in parsed.items():
        assert all(v[:4]==[1,0,1,1] for v in values),(p,values)
        assert all(b[15]>a[15] for a,b in zip(values,values[1:])),(p,values)
    report['passed']=not report['errors']
except Exception as exc:
    report['errors'].append(repr(exc))
finally:
    if actions_mode and PAIRS[0][0] in sessions:
        try: cmd(PAIRS[0][0],'TRIGger:REServation:SOURce:STOP')
        except Exception as exc: report['errors'].append('SOURCE STOP '+repr(exc))
    if scope_mode:
        for p in sessions:
            try: cmd(p,'SYSTem:VDC:OUTPut:RUN:STOP')
            except Exception as exc: report['errors'].append('OUTPUT STOP '+p+': '+repr(exc))
    for p in sessions:
        try: cmd(p,'SYSTem:TDMA:RING:STOP')
        except Exception as exc: report['errors'].append('STOP '+p+': '+repr(exc))
    for p in sessions:
        try:
            stopped=[int(v) for v in cmd(p,'SYSTem:TDMA:RING:STATus?').split(',')]
            assert stopped[0]==stopped[1]==0 and stopped[-1]==stopped[-2],stopped
            cmd(p,'SYSTem:VDC:PRIORity:FOLLow:PHASe:STATus?')
        except Exception as exc: report['errors'].append('STATUS '+p+': '+repr(exc))
    for p,(timing,delay) in old_output.items():
        try:
            cmd(p,'SYSTem:VDC:OUTPut:TIMing '+timing)
            cmd(p,'SYSTem:VDC:OUTPut:DELay '+delay)
        except Exception as exc: report['errors'].append('RESTORE '+p+': '+repr(exc))
    if actions_mode:
        if PAIRS[0][0] in sessions:
            try:
                report['stopped_counter']=cmd(PAIRS[0][0],'TRIGger:REServation:COUNter?')
                report['stopped_source']=cmd(PAIRS[0][0],'TRIGger:REServation:SOURce?')
            except Exception as exc: report['errors'].append('SOURCE READBACK '+repr(exc))
        for p in sessions:
            try:
                cmd(p,'TRIGger:REServation:T2:STOP')
                cmd(p,'TRIGger:REServation:TEMPLate:RELease')
            except Exception as exc: report['errors'].append('T2 STOP '+p+': '+repr(exc))
        if PAIRS[0][0] in sessions:
            try: cmd(PAIRS[0][0],'TRIGger:REServation:SOURce:RELease')
            except Exception as exc: report['errors'].append('SOURCE RELEASE '+repr(exc))
    if scope:
        try: scope.finish_evidence();scope.close()
        except Exception as exc: report['errors'].append('SCOPE CLOSE: '+repr(exc))
    for s in sessions.values(): s.close()
    if report['errors']: report['passed']=False
    (OUT/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report))
sys.exit(0 if report['passed'] else 1)
