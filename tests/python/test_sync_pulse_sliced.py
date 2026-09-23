"""Assembled compact RUN PIO, production C encoding and real backend words.

No hardware claim: FIFO pacing and instruction cycles are host observations.
"""
import ctypes as C
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest
from test_sync_io_run_output import executable
from test_sync_pulse_uniform import FifoMachine, assembled

ROOT = Path(__file__).resolve().parents[2]
U32 = 2**32-1


class Configuration(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        'period_ticks', 'interval_ticks', 'high_ticks', 'marker_ticks',
        'subdivisions', 'geometry')] + [('tail_ticks', C.c_uint64)]


class SlicedMachine(FifoMachine):
    def step(self):
        self.service_dma(self.tick)
        ins = self.code[self.pc]
        assert not ins & 0x1f00
        op = ins >> 13
        nxt = self.lo if self.pc == self.hi else self.pc+1
        cost = 1
        if op == 4:
            assert ins & 0xff == 0xa0
            if not self.fifo:
                self.stalls.append((self.tick, self.pc, self.pin))
                self.tick += 1
                return
            full = len(self.fifo) == self.capacity
            self.osr = self.fifo.popleft()
            if full:
                self.dma_next = max(self.dma_next, self.tick+self.latency)
        elif op == 3:
            dst, bits = (ins >> 5) & 7, (ins & 31) or 32
            value = self.osr & ((1 << bits)-1)
            self.osr >>= bits
            if dst == 1: self.x = value
            elif dst == 2: self.y = value
            else: assert dst == 3  # NULL
        elif op == 5:
            assert ((ins >> 5) & 7, (ins >> 3) & 3, ins & 7) == (7, 0, 6)
            self.osr = self.isr
        elif op == 0:
            cond, target = (ins >> 5) & 7, ins & 31
            if cond == 0: nxt = target
            else:
                assert cond in (2, 4)
                value = self.x if cond == 2 else self.y
                if target == self.pc:
                    cost, value = value+1, U32
                else:
                    if value: nxt = target
                    value = (value-1) & U32
                if cond == 2: self.x = value
                else: self.y = value
        elif op == 7:
            dst, value = (ins >> 5) & 7, ins & 31
            if dst == 2: self.y = value
            else:
                assert dst == 0 and value in (0, 1)
                if value != self.pin: self.edges.append((self.tick, value))
                self.pin = value
        else: raise AssertionError(hex(ins))
        self.service_dma(self.tick+cost-1)
        self.pc, self.tick = nxt, self.tick+cost

    def run_edges(self, count):
        for _ in range(count*12+100):
            if len(self.edges) == count: return
            self.step()
        raise AssertionError('instruction bound exhausted')


@pytest.fixture(scope='module')
def sliced(executable):
    d = executable.parent
    source = d/'sliced_encoder.c'
    source.write_text('#include "'+(ROOT/'components/sync_io/inc/sync_pulse_sliced_encode.h').as_posix()+'"\n'
        'bool conf(uint32_t hz,uint32_t p,uint32_t i,uint32_t h,uint32_t m,sync_pulse_sliced_config_t *c)'
        '{return sync_pulse_sliced_configure(hz,p,i,h,m,c);}\n'
        'bool enc(const sync_pulse_sliced_config_t *c,uint64_t o,bool f,uint64_t p,uint64_t r,uint64_t e,uint32_t *w)'
        '{return sync_pulse_sliced_encode(c,o,f,p,r,e,w);}\n', encoding='utf-8')
    cc = os.environ.get('HOST_CC') or shutil.which('gcc') or 'D:/Microsoft/mingw64/bin/gcc.exe'
    dll = d/('sliced.dll' if os.name == 'nt' else 'sliced.so')
    proc = subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', str(source), '-o', str(dll)],
                          capture_output=True, text=True, timeout=30)
    assert proc.returncode == 0, proc.stderr
    lib = C.CDLL(str(dll))
    lib.conf.argtypes = [C.c_uint32]*5+[C.POINTER(Configuration)]
    lib.conf.restype = C.c_bool
    lib.enc.argtypes = [C.POINTER(Configuration), C.c_uint64, C.c_bool, C.c_uint64, C.c_uint64, C.c_uint64, C.POINTER(C.c_uint32)]
    lib.enc.restype = C.c_bool
    code = assembled(d/'sync_pulse_sliced_out1.pio.h', 'sync_pulse_sliced_out1')
    assert len(code) == 20
    return lib, code


def config(lib, p=250000, i=125, h=25, m=50):
    c = Configuration()
    assert lib.conf(250000000, p, i, h, m, C.byref(c))
    return c


def machine(code, c, **kwargs):
    patched = list(code)
    assert patched[16] == 0xe040  # assembled SET Y, 0
    patched[16] |= c.high_ticks-3
    return SlicedMachine(patched, high_count=c.geometry, **kwargs)


def encode(lib, c, origin, first, previous, rise):
    word = C.c_uint32(0xa5a5a5a5)
    ok = lib.enc(C.byref(c), origin, first, previous, rise, rise+c.marker_ticks, C.byref(word))
    if not ok:
        assert word.value == 0xa5a5a5a5
        return None
    return word.value


def expected(c, markers):
    result = []
    for rise in markers:
        result += [(rise, 1), (rise+c.marker_ticks, 0)]
        for k in range(1, c.subdivisions-1):
            at = rise+k*c.interval_ticks
            result += [(at, 1), (at+c.high_ticks, 0)]
    return result


@pytest.mark.parametrize('cycles', [4, 8])
def test_real_backend_words_marker_and_1998_slices(executable, sliced, cycles):
    lib, code = sliced
    proc = subprocess.run([str(executable), 'sliced:stream' if cycles == 4 else 'sliced:refill'], capture_output=True, text=True, timeout=15)
    assert proc.returncode == 0, proc.stderr
    words = list(map(int, proc.stdout.split()))
    assert len(words) == cycles  # One word per synchronization cycle, never per slice.
    c = config(lib)
    m = machine(code, c, origin=1000000, offset=12, dma_latency=8)
    m.submit(words, preload=1)
    want = expected(c, [1100000+k*250001 for k in range(cycles)])
    m.run_edges(len(want))
    assert m.edges == want and not m.stalls and not m.pending
    for _ in range(16): m.step()
    assert m.pin == 0 and m.stalls and all(pin == 0 for _, _, pin in m.stalls)
    assert len(m.edges) == len(want)  # No replay after finite source exhaustion.


def test_rate_boundary_and_cross_block_reanchoring(sliced):
    lib, code = sliced
    c = config(lib)
    rng = random.Random(9283)
    origin = 2**32-1000
    rises = [origin+100000]
    for _ in range(31): rises.append(rises[-1]+250000+rng.randint(-12, 12))
    m = machine(code, c, origin=origin, dma_latency=12)
    words = [encode(lib, c, origin, k == 0, rises[k-1] if k else 0, r) for k, r in enumerate(rises)]
    # Two independently retired source blocks; second is offered while FIFO
    # still has future cycles, before an underflow could shift marker phase.
    m.submit(words[:16], preload=1)
    while m.pending: m.step()
    m.submit(words[16:])
    want = expected(c, rises)
    m.run_edges(len(want))
    assert m.edges == want and not m.stalls
    for k in range(len(rises)-1):
        span = rises[k+1]-rises[k]
        # Exact rational oracle bounds the fixed-tick intermediate boundaries.
        error_numerator = abs((c.subdivisions-2)*(c.interval_ticks*c.subdivisions-span))
        assert error_numerator < 12*c.subdivisions+1
    # Quality outside 12 ticks is observable, not a reason to suppress output.
    for error in (-217, -13, 13, 10000, 250000):
        assert encode(lib, c, origin, False, rises[0], rises[0]+250000+error) is not None
    # Real hardware encoding limit: previous final slice falls at +249775,
    # then eight low instruction ticks are needed before the next marker.
    for error in (-218, -225, -1000):
        assert encode(lib, c, origin, False, rises[0], rises[0]+250000+error) is None


def test_geometry_bounds_and_minimum_repeat(sliced):
    lib, code = sliced
    for n in (3, 4, 17, 2050):
        c = config(lib, p=n*125)
        words = [encode(lib, c, 0, True, 0, 1000), encode(lib, c, 0, False, 1000, 1000+n*125)]
        m = machine(code, c, offset=0)
        m.submit(words, preload=1)
        want = expected(c, [1000, 1000+n*125])
        m.run_edges(len(want))
        assert m.edges == want
    for args in [(250000000, 250001, 125, 25, 50), (250000000, 250, 125, 25, 50),
                 (250000000, 2051*125, 125, 25, 50), (250000000, 250000, 125, 35, 50),
                 (250000000, 250000, 125, 25, 66), (250000000, 250000, 125, 25, 25),
                 (250000000, 100000, 50, 25, 50), (200000000, 250000, 125, 25, 50)]:
        c = Configuration(); C.memset(C.byref(c), 0xa5, C.sizeof(c)); before = bytes(c)
        assert not lib.conf(*args, C.byref(c)) and bytes(c) == before
