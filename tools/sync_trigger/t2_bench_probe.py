"""Focused four-board T2 bench probe, not a P3 or action-completion receipt.

Promoted unchanged from the out-tree 20260927 T2 local-cursor probe snapshot, the
last chain that published a T2 position fragment. Usage:
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

# Field order of TRIGger:REServation:T2? (scpi_cmd_trigger_t2_q); each u64 is one field.
T2_FIELDS = (['ok', 'last_error', 'configured', 'active', 'origin', 'source_slot',
              'target_mask', 'run_id', 'last_sync_sequence', 'published_mask',
              'received_mask', 'descriptor_reject_count', 'transport_reject_count',
              'late_arrival_count',
              'late_last_current_ms', 'late_last_expiry_sync',
              'promise_target_ms_1', 'promise_expiry_1', 'promise_target_ms_2',
              'promise_expiry_2', 'promise_target_ms_3', 'promise_expiry_3',
              'ack_stale_mask', 'ack_incomplete_mask', 'ack_own_in_mask',
              'event_count', 'event_dropped_count', 'frozen_mask',
              'freeze_mismatch_count', 'freeze_fallback_count', 'freeze_missing_local_count'] +
             [f'{n}{i}' for i in range(3) for n in ('freeze_version', 'freeze_local_version',
                                                     'freeze_local_age')] +
             ['last_counter_count', 'pulse_index', 'fine_slice_index', 'announced_mask'] +
             [f'{n}{i}' for i in range(3) for n in ('target_count', 'threshold_count')] +
             ['predictor_error', 'core0_prepare_calls', 'core0_preview_saved',
              'core0_prepare_last_reason', 'segment_cursor_max', 'segment_receive_max',
              'segment_deadlines_max', 'freeze_complete_mask', 'freeze_abandoned_mask',
              'freeze_resend_count', 'freeze_abandoned_count',
              'fallback_promoted_count'])


def t2_page(raw):
    """Name the TRIGger:REServation:T2? reply; integers where possible."""
    values = raw.strip().strip('"').lstrip(',').split(',')
    return {name: int(value) if value.lstrip('-').isdigit() else value
            for name, value in zip(T2_FIELDS, values)}

# TX/RX budget evidence (actions mode): flight-sync, follower RX station,
# flight FIFO counters, T2 position transport and the origin seat evidence per
# sample; STOP-only priority lanes after the run.
BUDGET_QUERIES = ('SYSTem:REFMEM:SYNC:FLIGHT?', 'SYSTem:TDMA:PROFile:RX?',
                  'SYSTem:TDMA:FLIGHT:FIFO?', 'SYSTem:REFMEM:SYNC:FLIGHT:POSition?',
                  'SYSTem:TDMA:SEAT:COUNters?', 'SYSTem:TDMA:SEAT:HEALth?')
STOPPED_QUERIES = ('SYSTem:VDC:PRIORity:RX?', 'SYSTem:TDMA:FLIGHT:PRIORity?')
RX_STATION_STATES = ('idle', 'requested', 'building', 'ready', 'cancelled')


def ints(raw):
    out = []
    for value in raw.strip().strip('"').lstrip(',').split(','):
        try: out.append(int(value))
        except ValueError: out.append(None)
    return out


def budget(first, last, seconds):
    """Per-board deltas over the sampling window (field order: SCPI handlers)."""
    flight0, flight1 = ints(first[0]), ints(last[0])
    rx0, rx1 = ints(first[1]), ints(last[1])
    fifo0, fifo1 = ints(first[2]), ints(last[2])
    pos0, pos1 = ints(first[3]), ints(last[3])
    seat0 = ints(first[4]) if len(first) > 4 and first[4] is not None else []
    seat1 = ints(last[4]) if len(last) > 4 and last[4] is not None else []
    health1 = ints(last[5]) if len(last) > 5 and last[5] is not None else []
    d = lambda a, b, i: b[i] - a[i] if i < len(a) and i < len(b) and None not in (a[i], b[i]) else None
    row = {'next_seq32_delta': d(flight0, flight1, 9),
           'position_tx_staged_delta': d(pos0, pos1, 3),
           'position_rx_complete_delta': d(pos0, pos1, 10),
           'position_rx_reject_delta': d(pos0, pos1, 11),
           'fifo_rx_publish_delta': d(fifo0, fifo1, 10),
           'fifo_rx_publish_drop_delta': d(fifo0, fifo1, 12)}
    if len(rx1) > 21 and rx1[0] is not None:
        captures = d(rx0, rx1, 7)
        row['rx_captures_delta'] = captures
        row['rx_capture_period_ms'] = round(1000 * seconds / captures, 3) if captures else None
        row['rx_capture_gap_max_ms'] = round(1000 * rx1[9] / rx1[1], 3) if rx1[1] else None
        row['rx_station_polls_delta'] = {n: d(rx0, rx1, 12 + 2 * i) for i, n in enumerate(RX_STATION_STATES)}
        row['rx_station_age_max_ms'] = {n: round(rx1[13 + 2 * i] / 1e6, 3)
                                        for i, n in enumerate(RX_STATION_STATES) if i}
        row['rx_drop_delta'] = [d(rx0, rx1, i) for i in range(22, len(rx1))]
    if len(pos1) > 15 and pos1[0] is not None:
        # POSition? tail: first-class lane accept/duplicate/reject/owned plus the
        # bounded receive-preservation counters (held, drained, drain failures).
        # The legacy rx_complete_count no longer moves on this path.
        row['position_lane_accept_delta'] = d(pos0, pos1, 12)
        row['position_lane_duplicate_delta'] = d(pos0, pos1, 13)
        row['position_lane_reject_delta'] = d(pos0, pos1, 14)
        row['position_lane_owned_last'] = pos1[15]
        row['position_lane_hold_delta'] = d(pos0, pos1, 16)
        row['position_lane_drain_delta'] = d(pos0, pos1, 17)
        row['position_lane_drain_fail_delta'] = d(pos0, pos1, 18)
        row['position_lane_ack_override_delta'] = d(pos0, pos1, 19)
        row['position_lane_hold_expired_delta'] = d(pos0, pos1, 20)
        row['position_lane_hold_dropped_delta'] = d(pos0, pos1, 21)
        row['position_lane_diag_refused_delta'] = d(pos0, pos1, 22)
        row['position_lane_decode_reject_delta'] = d(pos0, pos1, 23)
        # Appended after the fields above, so their indices stay stable: queued
        # copies retired because the wire delivered the same record first.
        row['position_lane_hold_delivered_delta'] = d(pos0, pos1, 24)
    if len(health1) >= 2 and health1[0] is not None:
        # SEAT:HEALth?: the seat verdict at the end of the window (ok, reason bits)
        # followed by the raw evidence; a bench run is readable from these two.
        row['seat_health_ok'] = health1[0]
        row['seat_health_reason_mask'] = health1[1]
        if len(health1) > 10:
            row['seat_health_first_class_starved'] = health1[9]
            row['seat_health_first_class_deferred'] = health1[10]
        if len(health1) > 21:
            row['seat_health_decode_reject'] = health1[21]
        if len(health1) > 23:
            # Reason bit 8 and the two consumer-side loss facts behind it: the
            # origin abandoning a freeze, and a follower asked to freeze a
            # position it never had. Bit 8 sets when either is non-zero.
            row['seat_health_first_class_missed'] = 1 if (health1[1] or 0) & (1 << 8) else 0
            row['seat_health_freeze_abandoned'] = health1[22]
            row['seat_health_freeze_missing_local'] = health1[23]
    if len(seat1) >= 9:
        # SEAT:COUNters?: origin rotation and first-class seat service facts.
        row['seat'] = {
            'opportunity_delta': d(seat0, seat1, 0),
            'priority_delta': d(seat0, seat1, 1),
            'first_class_delta': d(seat0, seat1, 2),
            'priority_yield_delta': d(seat0, seat1, 3),
            'first_class_reject_delta': d(seat0, seat1, 4),
            'first_class_wait_max': seat1[5],
            'first_class_stage_refused_delta': d(seat0, seat1, 6),
            'first_class_retired_waiting_delta': d(seat0, seat1, 7),
            'first_class_wait_violation_delta': d(seat0, seat1, 8),
            'seat_starved_delta': d(seat0, seat1, 9),
            'first_class_starved_delta': d(seat0, seat1, 10),
            'first_class_deferred_delta': d(seat0, seat1, 11)}
    return row

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
            cmd(p,'TRIGger:REServation:T2:UNMOUNT','1,0')
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
            cmd(p,f'TRIGger:REServation:T2:CONFigure {generation},0,14,{int(i==0)},5000,5000,950,0,0,360000,1000,1000','1,0')
            cmd(p,f'TRIGger:REServation:T2:MOUNT {generation},0','1,0')
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
                sample.setdefault('t2', {})[p] = t2_page(cmd(p,'TRIGger:REServation:T2?'))
                cmd(p,'TRIGger:REServation:T2:ACTion?')
                sample.setdefault('budget', {})[p] = [cmd(p, q) for q in BUDGET_QUERIES]
                sample['budget_host'] = time.monotonic()
            schedule=[int(v) for v in cmd(p,'SYSTem:TDMA:SCHEDule?').split(',')]
            if schedule[5]&64:
                report['errors'].append(f'{p}: SYNC_TRIGGER quarantined; phase={schedule[85:96]}')
        samples.append(sample)
        # Origin emission cadence on the reference: published_generation vs
        # boundary_sequence over wall time separates the 1.5 ms (Core1 phase)
        # and 4 ms (ring throttle) hypotheses for publication opportunities.
        runtime=cmd(PAIRS[0][0],'READ:CALibration:ORIGin:RUNTIme?').split(',')
        if len(runtime)>13 and runtime[0].strip('"')=='ORIGINRUNTIME':
            report.setdefault('origin_cadence',[]).append({'host':time.monotonic(),
                'published_generation':int(runtime[7]),'boundary_sequence':int(runtime[13])})
    report['samples']=samples
    cadence=report.get('origin_cadence',[])
    if len(cadence)>1:
        a,b=cadence[0],cadence[-1]; seconds=b['host']-a['host']
        published=b['published_generation']-a['published_generation']
        boundaries=b['boundary_sequence']-a['boundary_sequence']
        report['origin_cadence_summary']={'seconds':round(seconds,3),
            'published_delta':published,'boundary_delta':boundaries,
            'publish_interval_ms':round(1000*seconds/published,4) if published else None,
            'boundary_interval_ms':round(1000*seconds/boundaries,4) if boundaries else None}
    if actions_mode and len(samples) > 1:
        seconds = samples[-1]['budget_host'] - samples[0]['budget_host']
        report['tx_rx_budget'] = {'seconds': round(seconds, 3), **{
            p: budget(samples[0]['budget'][p], samples[-1]['budget'][p], seconds) for p, _ in PAIRS}}
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
            for q in STOPPED_QUERIES:
                try: report.setdefault('stopped_lanes', {}).setdefault(p, {})[q] = cmd(p, q)
                except Exception as exc: report['errors'].append('LANES '+p+': '+repr(exc))
        for p in sessions:
            try:
                cmd(p,'TRIGger:REServation:T2:UNMOUNT')
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
