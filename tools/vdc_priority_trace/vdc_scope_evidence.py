"""Bounded host-only persistence for frozen scope records."""
from concurrent.futures import ThreadPoolExecutor
import copy
import hashlib
import json
import math
import time


class EvidenceWriter:
    def __init__(self, capacity=4):
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='scope-evidence')
        self.pending = []
        self.capacity = capacity
        self.closed = False
        self.errors = []

    def _reap(self):
        while self.pending and self.pending[0].done():
            try:
                self.pending.pop(0).result()
            except Exception as exc:
                self.errors.append(repr(exc))

    def check(self):
        self._reap()
        self._raise_errors()

    def _raise_errors(self):
        if self.errors:
            raise RuntimeError('Evidence persistence failed: ' + '; '.join(self.errors))

    def submit(self, folder, report, blocks):
        if self.closed:
            raise RuntimeError('Evidence writer is closed')
        # A preceding write may fail during acquisition. Still retain this
        # newly captured record before reporting the sticky failure.
        self._reap()
        if len(self.pending) >= self.capacity:
            raise RuntimeError('Evidence write queue full; evidence cannot be dropped')
        snapshot = copy.deepcopy(report)
        payloads = dict(blocks)  # Byte strings are immutable.
        self.pending.append(self.executor.submit(self._write, folder, snapshot, payloads))
        self._raise_errors()

    @staticmethod
    def _write(folder, report, blocks):
        started = time.monotonic_ns()
        for name, raw in blocks.items():
            with (folder / name).open('xb') as stream:
                stream.write(raw)
        report['persistence'] = dict(completed=True, started_ns=started,
                                      ended_ns=time.monotonic_ns())
        (folder / 'scope.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        if 'phase' in report:
            (folder / 'phase.json').write_text(json.dumps(report['phase'], indent=2), encoding='utf-8')

    def close(self):
        self.closed = True
        self.executor.shutdown(wait=True)
        self._reap()
        self._raise_errors()


def capture_raw(scope, reader):
    """Same validated 650k prefix, with no file I/O in the VISA owner."""
    if scope.quiet or not scope.report.get('armed_ns'):
        raise RuntimeError('No fresh acquisition admission')
    query = lambda command: reader.Scope.query(scope, command)
    if query(':TRIG:STAT?') != 'STOP':
        raise ValueError('Scope is not frozen')
    scope.report['sample_rate'] = query(':ACQ:SRAT?')
    memory = int(float(query(':ACQ:MDEP?')))
    if memory != scope.points or memory != 1000000:
        raise ValueError('Unexpected acquisition memory')
    count = 650000
    scope.report.update(acquisition_memory_points=memory, contiguous_export_range=[1, count],
        time_formula='x_origin + (index - x_reference) * x_increment; start=1')
    scope.write(':WAV:MODE RAW')
    scope.write(':WAV:FORM BYTE')
    axis = None
    for channel in range(1, 5):
        scope.write(f':WAV:SOUR CHAN{channel}')
        scope.write(':WAV:STAR 1')
        scope.write(f':WAV:STOP {count}')
        settings = {q: query(q) for q in
                    (':WAV:SOUR?', ':WAV:MODE?', ':WAV:FORM?', ':WAV:STAR?', ':WAV:STOP?')}
        if (settings[':WAV:SOUR?'] != f'CHAN{channel}' or settings[':WAV:MODE?'] != 'RAW'
                or settings[':WAV:FORM?'] != 'BYTE' or int(settings[':WAV:STAR?']) != 1
                or int(settings[':WAV:STOP?']) != count):
            raise ValueError('RAW source/range readback mismatch')
        preamble = query(':WAV:PRE?')
        p = list(map(float, preamble.split(',')))
        if (len(p) != 10 or not all(math.isfinite(v) for v in p) or p[:2] != [0, 2]
                or p[3] != 1 or p[7] <= 0 or int(p[2]) != count
                or not math.isclose(p[4], 2e-9, rel_tol=1e-6)
                or p[5]-p[6]*p[4] >= -1e-5 or p[5]+(count-1-p[6])*p[4] <= .00101):
            raise ValueError('RAW preamble or two-edge coverage mismatch')
        if axis is not None and axis != tuple(p[2:7]):
            raise ValueError('Channel time axes differ')
        axis = tuple(p[2:7])
        if query(':TRIG:STAT?') != 'STOP':
            raise ValueError('Acquisition changed during export')
        scope.write(':WAV:DATA?')
        started = time.monotonic_ns()
        raw = scope.scope.read_raw()
        ended = time.monotonic_ns()
        name = f'ch{channel}-00000001.block'
        scope._raw_blocks[name] = raw  # Preserve malformed responses as evidence too.
        reader.decode_block(raw, count)
        scope.report['channels'].append(dict(channel=channel, preamble=preamble,
            sample_count=count, read_started_ns=started, read_ended_ns=ended,
            blocks=[dict(start=1, stop=count, path=name, readback=settings,
                         sha256=hashlib.sha256(raw).hexdigest())]))
    if query(':TRIG:STAT?') != 'STOP' or not query(':SYST:ERR?').startswith(('0,', '+0,')):
        raise ValueError('Scope final state or error queue invalid')
    scope.report['capture_complete'] = True


def analyze_memory(report, blocks, decode_block):
    import numpy as np
    edges = []
    if [row['channel'] for row in report['channels']] != [1, 2, 3, 4]:
        raise ValueError('Four ordered channels required')
    for row in report['channels']:
        p = list(map(float, row['preamble'].split(',')))
        arrays = []
        for block in row['blocks']:
            raw = blocks[block['path']]
            if hashlib.sha256(raw).hexdigest() != block['sha256']:
                raise ValueError('RAW checksum mismatch')
            arrays.append(np.frombuffer(decode_block(raw, block['stop']-block['start']+1), dtype=np.uint8))
        voltage = (np.concatenate(arrays).astype(float)-p[8]-p[9])*p[7]
        ix = np.flatnonzero((voltage[:-1] < 2.5) & (voltage[1:] >= 2.5))
        when = p[5]+(ix+(2.5-voltage[ix])/(voltage[ix+1]-voltage[ix])-p[6])*p[4]
        when = when[np.r_[True, np.diff(when) > 1e-5]] if len(when) else when
        if len(when) != 2:
            raise ValueError(f'Expected two actual rising edges on CH{row["channel"]}, got {len(when)}')
        edges.append(when)
    delta = [[float(signal[np.argmin(abs(signal-t))]-t)*1e9 for t in edges[0]] for signal in edges[1:]]
    return dict(edges_s=[v.tolist() for v in edges], relative_ns=delta,
                pairing='nearest periodic edge; absolute ordinal unproven',
                large_phase_ambiguity=any(abs(d)>100000 for a in delta for d in a),
                within_100ns=all(abs(d)<=100 for a in delta for d in a),
                within_50ns=all(abs(d)<=50 for a in delta for d in a))
