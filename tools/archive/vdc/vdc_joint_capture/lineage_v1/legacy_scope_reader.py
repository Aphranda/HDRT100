"""CH1-triggered frozen four-channel record, with a quiet acquisition barrier."""
import hashlib
import json
from pathlib import Path
import time
import math
from tools.scope_dpll_capture.scope_dpll_capture import DEFAULT_RESOURCE

RESOURCE = DEFAULT_RESOURCE


def decode_block(raw, expected):
    if len(raw) < 3 or raw[:1] != b'#' or not raw[1:2].isdigit():
        raise ValueError('Invalid binary header')
    digits = int(raw[1:2])
    if not digits or not raw[2:2+digits].isdigit():
        raise ValueError('Invalid definite block length')
    count = int(raw[2:2+digits]); begin = 2+digits
    if count != expected or len(raw) < begin+count or raw[begin+count:].strip(b'\r\n'):
        raise ValueError('Wrong waveform block length or suffix')
    return raw[begin:begin+count]


class Scope:
    def __init__(self, folder, instrument=None, scale=.02, points=1000000, offset=0.0):
        self.scale=scale; self.points=points; self.offset=offset
        self.folder = Path(folder); self.folder.mkdir(parents=True, exist_ok=True)
        self.rm = None
        if instrument is None:
            import pyvisa
            self.rm = pyvisa.ResourceManager(); instrument = self.rm.open_resource(RESOURCE)
        self.scope = instrument
        self.scope.timeout = 10000; self.scope.chunk_size = 1048576
        self.quiet = False
        self.report = dict(resource=RESOURCE, wiring='CH1..CH4 = NO1..NO4, 1x probes',
            commands=[], channels=[], capture_complete=False,
            voltage_formula='(code - y_origin - y_reference) * y_increment',
            time_formula='x_origin + (start-1 + index - x_reference) * x_increment',
            physical_lock_qualified=False)

    def save(self):
        (self.folder/'scope.json').write_text(json.dumps(self.report, indent=2), encoding='utf-8')

    def write(self, command):
        self.report['commands'].append(dict(command=command, start_ns=time.monotonic_ns()))
        self.scope.write(command); time.sleep(.08); self.save()

    def query(self, command):
        if self.quiet: raise RuntimeError('Scope query forbidden inside board acquisition')
        row=dict(command=command, start_ns=time.monotonic_ns()); self.report['commands'].append(row)
        try:
            row['raw']=self.scope.query(command).strip(); return row['raw']
        finally:
            row['end_ns']=time.monotonic_ns(); self.save()

    def prepare(self):
        self.report['identity']=self.query('*IDN?')
        if 'HDO4A244301137' not in self.report['identity']: raise ValueError('Wrong scope')
        self.write(':STOP')
        if self.query(':TRIG:STAT?') != 'STOP': raise ValueError('Scope not stopped')
        for ch in range(1,5):
            for cmd in (f':CHAN{ch}:DISP 1', f':CHAN{ch}:PROB 1', f':CHAN{ch}:COUP DC',
                        f':CHAN{ch}:SCAL 2', f':CHAN{ch}:OFFS 0'): self.write(cmd)
        for cmd in (':ACQ:TYPE NORM', ':TIM:MODE MAIN', f':TIM:SCAL {self.scale}', f':TIM:OFFS {self.offset}', f':ACQ:MDEP {self.points}',
                    ':TRIG:MODE EDGE', ':TRIG:EDGE:SOUR CHAN1', ':TRIG:EDGE:SLOP POS',
                    ':TRIG:EDGE:LEV 1.5', ':TRIG:SWE NORM'): self.write(cmd)
        keys=[':TRIG:MODE?', ':TRIG:EDGE:SOUR?', ':TRIG:EDGE:SLOP?', ':TRIG:EDGE:LEV?',
              ':TRIG:SWE?', ':TIM:SCAL?', ':TIM:OFFS?', ':ACQ:MDEP?', ':ACQ:TYPE?']
        keys += [f':CHAN{ch}:{key}?' for ch in range(1,5) for key in ('PROB','DISP','COUP','SCAL','OFFS')]
        settings={key:self.query(key) for key in keys};self.report['settings']=settings
        assert settings[':TRIG:EDGE:SOUR?']=='CHAN1' and settings[':TRIG:EDGE:SLOP?']=='POS'
        assert float(settings[':ACQ:MDEP?'])==self.points
        assert math.isclose(float(settings[':TIM:SCAL?']),self.scale,rel_tol=1e-9)
        assert math.isclose(float(settings[':TIM:OFFS?']),self.offset,abs_tol=1e-10)
        assert all(float(settings[f':CHAN{ch}:PROB?'])==1 for ch in range(1,5))
        if not self.query(':SYST:ERR?').startswith(('0,','+0,')):raise ValueError('Scope setup error')

    def arm(self):
        self.write(':SING')
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            status=self.query(':TRIG:STAT?')
            if status=='WAIT': break
            time.sleep(.03)
        else:raise TimeoutError('No new WAIT; previous STOP is not a capture')
        if self.query(':TRIG:SWE?')!='SING' or self.query(':TRIG:EDGE:SOUR?')!='CHAN1':
            raise ValueError('Armed source/mode changed')
        self.report['armed_ns']=time.monotonic_ns();self.save()

    def export(self):
        if self.quiet:raise RuntimeError('Export before all-board STOP')
        if not self.report.get('armed_ns'):raise RuntimeError('No proven new single acquisition arm')
        if self.query(':TRIG:STAT?')!='STOP':raise RuntimeError('No completed single trigger')
        self.report['stopped_ns']=time.monotonic_ns()
        self.report['sample_rate']=self.query(':ACQ:SRAT?')
        axis=None
        for ch in range(1,5):
            for cmd in (f':WAV:SOUR CHAN{ch}', ':WAV:MODE RAW', ':WAV:FORM BYTE'):
                self.write(cmd)
            pre=self.query(':WAV:PRE?'); fields=list(map(float,pre.split(',')))
            if len(fields)!=10 or fields[0:2]!=[0,2] or fields[3]!=1:
                raise ValueError('Not one frozen RAW BYTE acquisition')
            total=int(float(self.query(':ACQ:MDEP?')));assert total==self.points
            # PRE points reflects the selected transfer range on HDO4404.
            if not 0 < int(fields[2]) <= total:raise ValueError('Invalid selected transfer size')
            if not all(math.isfinite(x) for x in fields) or fields[4]<=0 or fields[7]<=0:
                raise ValueError('Invalid waveform scaling')
            current_axis=tuple(fields[2:7])
            if axis is not None and current_axis!=axis:raise ValueError('Different channel time axes')
            axis=current_axis
            row=dict(channel=ch,preamble=pre,sample_count=total,blocks=[])
            self.report['channels'].append(row)
            for start in range(1,total+1,1000000):
                stop=min(start+999999,total)
                self.write(f':WAV:STAR {start}');self.write(f':WAV:STOP {stop}')
                readback={q:self.query(q) for q in (':WAV:SOUR?',':WAV:MODE?',':WAV:FORM?',':WAV:STAR?',':WAV:STOP?')}
                assert readback[':WAV:SOUR?']==f'CHAN{ch}' and readback[':WAV:MODE?']=='RAW'
                assert readback[':WAV:FORM?']=='BYTE'
                assert int(float(readback[':WAV:STAR?']))==start and int(float(readback[':WAV:STOP?']))==stop
                if self.query(':TRIG:STAT?')!='STOP':raise ValueError('Acquisition changed during export')
                self.write(':WAV:DATA?');raw=self.scope.read_raw()
                name=f'ch{ch}-{start:08d}.block';(self.folder/name).write_bytes(raw)
                decode_block(raw,stop-start+1)
                row['blocks'].append(dict(start=start,stop=stop,path=name,readback=readback,
                    sha256=hashlib.sha256(raw).hexdigest()));self.save()
        assert self.query(':TRIG:STAT?')=='STOP'
        assert self.query(':SYST:ERR?').startswith(('0,','+0,'))
        self.report['capture_complete']=True;self.save()

    def close(self):
        self.save();self.scope.close()
        if self.rm is not None:self.rm.close()
