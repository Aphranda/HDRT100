"""Real pioasm output + C encoder; decoded instruction execution is the oracle."""
from collections import deque
import ctypes as C
from pathlib import Path
import hashlib
import json
import os
import random
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'components/sync_io/src'
ENCODER = ROOT / 'components/sync_io/inc/sync_pulse_stream_encode.h'
U32, U64 = (1 << 32) - 1, (1 << 64) - 1
PROGRAM = 'sync_pulse_stream_out1'


class Words(C.Structure):
    _fields_ = [('high_count', C.c_uint32), ('low_count', C.c_uint32)]


@pytest.fixture(scope='module')
def candidate(tmp_path_factory):
    d = tmp_path_factory.mktemp('pio-candidate')
    cc = os.environ.get('HOST_CC') or shutil.which('gcc') or 'D:/Microsoft/mingw64/bin/gcc.exe'
    asm = os.environ.get('PIOASM') or shutil.which('pioasm') or str(
        Path.home() / '.pico-sdk/tools/2.2.0/pioasm/pioasm.exe')
    header = d / (PROGRAM + '.pio.h')
    proc = subprocess.run([asm, '-o', 'c-sdk', str(HERE / (PROGRAM + '.pio')), str(header)],
                          capture_output=True, text=True, timeout=30)
    (d / 'assemble.log').write_text(proc.stdout + proc.stderr, encoding='utf-8')
    assert proc.returncode == 0, proc.stderr
    text = header.read_text(encoding='utf-8')
    body = text.split(PROGRAM + '_program_instructions[] = {')[1].split('};')[0]
    code = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]{4}),', body)]
    wrap_lo = int(re.search(r'#define ' + PROGRAM + r'_wrap_target (\d+)', text)[1])
    wrap_hi = int(re.search(r'#define ' + PROGRAM + r'_wrap (\d+)', text)[1])
    source = d / 'wrapper.c'
    source.write_text('#include "' + ENCODER.as_posix() + '"\n'
        'bool encode(uint64_t origin,bool first,uint64_t prev,uint64_t rise,uint64_t fall,'
        'sync_pulse_stream_words_t *out) {return sync_pulse_stream_encode(origin,first,prev,rise,fall,out);}\n',
        encoding='utf-8')
    libpath = d / ('encode.dll' if os.name == 'nt' else 'encode.so')
    result = subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared',
                             str(source), '-o', str(libpath)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    lib = C.CDLL(str(libpath))
    lib.assembled_header = header
    lib.encode.argtypes = [C.c_uint64, C.c_bool, C.c_uint64, C.c_uint64, C.c_uint64, C.POINTER(Words)]
    lib.encode.restype = C.c_bool
    return lib, code, wrap_lo, wrap_hi


class Machine:
    """Small interpreter of assembled PIO opcodes (not the timing formulas).

    SET effects are timestamped by the instruction's start tick. Each decoded
    instruction takes one tick. Blocking PULL does not advance PC. JMP x--/y--
    tests pre-decrement register and decrements modulo 2^32 on either outcome.
    Fast-forward of self JMP repeats this same transition counter+1 times.
    """
    def __init__(self, candidate, origin=0, offset=0):
        _, code, lo, hi = candidate
        self.code = {i+offset: ((w & ~31) | ((w & 31)+offset)) if w >> 13 == 0 else w
                     for i, w in enumerate(code)}
        self.lo, self.hi = lo+offset, hi+offset
        self.pc = self.lo
        self.tick = origin
        self.x = self.y = self.osr = 0
        self.pin = 0
        self.enabled = True
        self.fifo = deque()
        self.edges = []
        self.stalls = []
        self.pending_dma = deque()
        self.dma_transferred = 0

    def submit_finite(self, words):
        # One finite DMA transfer; freeze read buffer. There is no ring, reload,
        # auto-chain or completion handler that can replay this source.
        self.pending_dma.extend(tuple(words))

    def step(self, fast=False):
        if not self.enabled:
            self.tick += 1
            return
        while self.pending_dma and len(self.fifo) < 8:
            self.fifo.append(self.pending_dma.popleft())
            self.dma_transferred += 1
        instruction = self.code[self.pc]
        assert instruction & 0x1f00 == 0, 'no side-set or delay in this program'
        opcode = instruction >> 13
        nxt = self.lo if self.pc == self.hi else self.pc+1
        cost = 1
        if opcode == 4:
            assert instruction & 0xff == 0xa0, 'only unconditional blocking PULL'
            if not self.fifo:
                self.stalls.append((self.tick, self.pc, self.pin))
                self.tick += 1
                return
            self.osr = self.fifo.popleft()
        elif opcode == 3:
            dest, bits = (instruction >> 5) & 7, instruction & 31
            assert bits == 0, '32-bit OUT'
            assert dest in (1, 2)
            if dest == 1: self.x = self.osr
            else: self.y = self.osr
            self.osr = 0
        elif opcode == 0:
            cond, target = (instruction >> 5) & 7, instruction & 31
            assert cond in (2, 4)
            before = self.x if cond == 2 else self.y
            if fast and target == self.pc:
                cost = before + 1
                after = U32
            else:
                after = (before - 1) & U32
                if before: nxt = target
            if cond == 2: self.x = after
            else: self.y = after
        elif opcode == 7:
            assert ((instruction >> 5) & 7) == 0, 'SET pins'
            pin = instruction & 31
            assert pin in (0, 1)
            if pin != self.pin: self.edges.append((self.tick, pin))
            self.pin = pin
        else:
            raise AssertionError(('unexpected opcode', hex(instruction)))
        self.pc = nxt
        self.tick += cost

    def run_edges(self, count, fast=False):
        for _ in range(200000):
            if len(self.edges) == count: return
            self.step(fast)
        raise AssertionError('instruction execution bound exhausted')

    def stop_force_low(self):
        self.enabled = False
        if self.pin: self.edges.append((self.tick, 0))
        self.pin = 0
        self.pending_dma.clear()
        self.fifo.clear()

    def restart(self, origin):
        assert not self.enabled and not self.pin and not self.pending_dma and not self.fifo
        self.tick, self.pc = origin, self.lo
        self.x = self.y = self.osr = 0
        self.enabled = True


def encode(candidate, origin, first, prev, rise, fall):
    out = Words(0xA53C19D7, 0x12CBAFA9)
    before = bytes(out)
    ok = candidate[0].encode(origin, first, prev, rise, fall, C.byref(out))
    if not ok:
        assert bytes(out) == before
        return None
    return out.high_count, out.low_count


def test_assembled_length_and_all_pair_paths(candidate):
    assert len(candidate[1]) == 8
    # Enumerate actual machine with raw FIFO words independently of encoder.
    for low in range(10):
        for high in range(10):
            for offset in (0, 7, 24):
                m = Machine(candidate, origin=31, offset=offset)
                m.submit_finite([high, low, high, low])
                m.run_edges(4)
                first_rise, first_fall, next_rise, next_fall = [t for t, _ in m.edges]
                assert first_rise-31 == low+5
                assert first_fall-first_rise == high+2
                assert next_rise-first_fall == low+6
                assert next_fall-next_rise == high+2


def test_real_c_encoder_1000_random_absolute_sequences_on_instructions(candidate):
    rng = random.Random(0x501025)
    for n in range(1000):
        origin = rng.randrange(0, U64-1000000)
        m = Machine(candidate, origin, rng.randrange(25))
        previous = origin
        expected = []
        stream = []
        for i in range(rng.randrange(1, 30)):
            rise = previous + rng.randrange(6, 300)
            fall = rise + rng.randrange(2, 100)
            pair = encode(candidate, origin, i == 0, previous, rise, fall)
            assert pair is not None
            stream.extend(pair)
            expected.extend([(rise, 1), (fall, 0)])
            previous = fall
        m.submit_finite(stream)
        m.run_edges(len(expected))
        assert m.edges == expected and not m.stalls
        for _ in range(50): m.step()
        assert m.pin == 0 and len(m.edges) == len(expected)
        assert m.dma_transferred == len(stream) and not m.pending_dma


@pytest.mark.parametrize('first', [True, False])
def test_count_maximum_u64_boundaries_and_no_arithmetic_wrap(candidate, first):
    overhead = 5 if first else 6
    origin, previous = 0, 1
    reference = origin if first else previous
    for low_count in (0, 1, U32-1, U32):
        for high_count in (0, 1, U32-1, U32):
            rise, fall = reference+low_count+overhead, reference+low_count+overhead+high_count+2
            pair = encode(candidate, origin, first, previous, rise, fall)
            assert pair == (high_count, low_count)
            m = Machine(candidate, reference if first else 0)
            if not first:
                # Start just after prior falling SET, at wrapped PC zero.
                m.tick = previous+1
            m.submit_finite(pair)
            m.run_edges(2, fast=True)
            assert m.edges == [(rise, 1), (fall, 0)]
    origin = U64-100
    rise, fall = U64-2, U64
    pair = encode(candidate, origin, first, origin+1, rise, fall)
    assert pair is not None
    m = Machine(candidate, origin if first else origin+2)
    m.submit_finite(pair)
    m.run_edges(2)
    assert m.edges == [(rise, 1), (fall, 0)]


def test_rejected_short_negative_or_unrepresentable_timing_preserves_words(candidate):
    cases = [(0, True, 0, 4, 20), (0, True, 0, 5, 6), (0, False, 10, 15, 20),
             (100, False, 99, 120, 130), (100, True, 0, 99, 120),
             (0, False, 100, 99, 120), (0, True, 0, 10, 9),
             (0, True, 0, U32+6, U32+8), (0, False, 0, U32+7, U32+9),
             (0, True, 0, 5, 5+U32+3), (U64, True, 0, 0, 2)]
    for case in cases: assert encode(candidate, *case) is None
    assert not candidate[0].encode(0, True, 0, 5, 7, None)


@pytest.mark.parametrize('partial', [[], [7]])
def test_either_pull_can_starve_only_while_low_and_resume_invalidates_absolute_origin(candidate, partial):
    m = Machine(candidate)
    m.submit_finite(partial)
    for _ in range(100): m.step()
    assert not m.edges and m.pin == 0
    assert all(level == 0 for _, _, level in m.stalls)
    assert m.pc == (0 if not partial else 2)
    m.submit_finite([7, 11] if not partial else [11])
    m.run_edges(2)
    assert m.edges[0][0] > 11+5  # A late pair cannot retain its old deadline.
    assert m.edges[1][0]-m.edges[0][0] == 7+2
    for _ in range(100): m.step()
    assert len(m.edges) == 2 and m.pin == 0


def test_fifo_empty_after_high_start_cannot_hold_high_or_replay_finite_block(candidate):
    m = Machine(candidate)
    m.submit_finite([31, 0])
    m.run_edges(1)
    assert not m.fifo and not m.pending_dma and m.pin == 1
    m.run_edges(2)
    assert m.edges == [(5, 1), (38, 0)]
    for _ in range(1000): m.step()
    assert m.edges == [(5, 1), (38, 0)] and m.dma_transferred == 2
    assert m.pc == 0 and m.pin == 0


@pytest.mark.parametrize('at_high', [False, True])
def test_raw_pause_is_not_safe_deadline_preserving_stop_and_restart_is_new_run(candidate, at_high):
    m = Machine(candidate)
    m.submit_finite([31, 5, 99, 99])
    if at_high: m.run_edges(1)
    else:
        for _ in range(4): m.step()
    m.enabled = False
    pc, level, tick = m.pc, m.pin, m.tick
    for _ in range(100): m.step()
    assert (m.pc, m.pin, m.tick) == (pc, level, tick+100)
    assert m.pin == int(at_high)  # disable alone freezes a high output.
    m.stop_force_low()
    old = list(m.edges)
    assert not m.fifo and not m.pending_dma
    m.restart(1000)
    m.submit_finite(encode(candidate, 1000, True, 0, 1010, 1020))
    m.run_edges(len(old)+2)
    assert m.edges == old+[(1010, 1), (1020, 0)]


def test_submitted_prefix_immutable_and_new_model_only_changes_unsubmitted_tail(candidate):
    origin = 100
    prefix = list(encode(candidate, origin, True, 0, 120, 125))
    prefix += list(encode(candidate, origin, False, 125, 145, 150))
    m = Machine(candidate, origin)
    m.submit_finite(prefix)
    prefix[:] = [0, 0, 0, 0]  # Model/planner reuse cannot rewrite copied DMA storage.
    m.run_edges(1)
    # Future model proposes a different interval, anchored to last COMMITTED
    # falling edge 150, not first observed edge 120 or current execution tick.
    tail = encode(candidate, origin, False, 150, 180, 183)
    m.submit_finite(tail)
    m.run_edges(6)
    assert m.edges == [(120, 1), (125, 0), (145, 1), (150, 0), (180, 1), (183, 0)]
    assert m.dma_transferred == 6


def test_generated_init_is_disabled_divider_one_masked_low_and_failure_is_closed(candidate, tmp_path):
    hardware = tmp_path / 'hardware'
    hardware.mkdir()
    (hardware / 'pio.h').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef unsigned uint;
typedef void *PIO;
typedef struct {uint lo,hi,pin,count,shift,autopull,threshold,join,div,frac;} pio_sm_config;
struct pio_program {const uint16_t *instructions; uint length; int origin; uint pio_version;};
#define PIO_FIFO_JOIN_TX 1u
static unsigned calls[32], used, directions;
static uint32_t pin_latch = UINT32_MAX;
static bool enabled = true;
static int init_result;
static pio_sm_config received;
static unsigned received_pc;
static void note(unsigned x) {assert(used<32);calls[used++]=x;}
static pio_sm_config pio_get_default_sm_config(void) {pio_sm_config c={0};return c;}
static void sm_config_set_wrap(pio_sm_config *c,uint l,uint h) {c->lo=l;c->hi=h;}
static void sm_config_set_set_pins(pio_sm_config *c,uint p,uint n) {c->pin=p;c->count=n;}
static void sm_config_set_out_shift(pio_sm_config *c,bool r,bool a,uint n) {c->shift=r;c->autopull=a;c->threshold=n;}
static void sm_config_set_fifo_join(pio_sm_config *c,uint n) {c->join=n;}
static void sm_config_set_clkdiv_int_frac(pio_sm_config *c,uint d,uint f) {c->div=d;c->frac=f;}
static void pio_sm_set_enabled(PIO p,uint s,bool e) {(void)p;assert(s==2);enabled=e;note(1);}
static int pio_sm_init(PIO p,uint s,uint pc,const pio_sm_config *c) {(void)p;assert(s==2);assert(!enabled);received=*c;received_pc=pc;note(2);return init_result;}
static void pio_sm_clear_fifos(PIO p,uint s) {(void)p;assert(s==2&&!enabled);note(3);}
static void pio_sm_restart(PIO p,uint s) {(void)p;assert(s==2&&!enabled);note(4);}
static void pio_sm_clkdiv_restart(PIO p,uint s) {(void)p;assert(s==2&&!enabled);note(5);}
static void pio_sm_set_pins_with_mask(PIO p,uint s,uint v,uint m) {(void)p;assert(s==2&&!enabled);pin_latch=(pin_latch&~m)|(v&m);note(6);}
static void pio_sm_set_consecutive_pindirs(PIO p,uint s,uint pin,uint n,bool out) {(void)p;assert(s==2&&pin==16&&n==1&&out);assert(!(pin_latch&(1u<<16)));++directions;note(7);}
static void pio_gpio_init(PIO p,uint pin) {(void)p;assert(pin==16&&!enabled);assert(directions==1);note(8);}
''', encoding='utf-8')
    source = tmp_path / 'init_harness.c'
    source.write_text('#include "' + candidate[0].assembled_header.as_posix() + '"\n' + r'''
int main(void)
{
    assert(sync_pulse_stream_out1_program.length==8);
    assert(sync_pulse_stream_out1_program.origin==-1);
    assert(sync_pulse_stream_out1_program_init((PIO)1,2,24,16));
    assert(used==8&&!enabled&&pin_latch==(UINT32_MAX^(1u<<16)));
    for(unsigned i=0;i<8;++i)assert(calls[i]==i+1);
    assert(received.lo==24&&received.hi==31&&received_pc==24);
    assert(received.pin==16&&received.count==1&&received.div==1&&received.frac==0);
    assert(received.shift==1&&!received.autopull&&received.threshold==32&&received.join==PIO_FIFO_JOIN_TX);
    used=0;
    assert(!sync_pulse_stream_out1_program_init((PIO)1,4,0,16));
    assert(!sync_pulse_stream_out1_program_init((PIO)1,2,25,16));
    assert(!sync_pulse_stream_out1_program_init((PIO)1,2,0,32));
    assert(used==0);
    init_result=-1;enabled=true;
    assert(!sync_pulse_stream_out1_program_init((PIO)1,2,0,16));
    assert(used==2&&!enabled); /* no enable, GPIO claim or FIFO work after failure */
    return 0;
}
''', encoding='utf-8')
    cc = os.environ.get('HOST_CC') or shutil.which('gcc') or 'D:/Microsoft/mingw64/bin/gcc.exe'
    exe = tmp_path / ('init.exe' if os.name == 'nt' else 'init')
    result = subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                             '-I'+str(tmp_path),str(source),'-o',str(exe)],
                            capture_output=True,text=True,timeout=30)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
    assert result.returncode == 0, result.stdout+result.stderr


def test_record_artifact_hashes_and_qualification_limits(candidate):
    report = {'schema': 'SYNC_PULSE_STREAM_PIO_CANDIDATE_V1',
              'assembled_words': candidate[1], 'length': len(candidate[1]),
              'input_words': ['high_count', 'low_count'], 'divider': 1,
              'cycle_definition': 'instruction execution slot start; not CPU enable store timestamp',
              'equations': {'first_rise': 'origin+low+5', 'steady_rise': 'previous_fall+low+6',
                            'fall': 'rise+high+2'},
              'limits': ['simulation does not establish enable-to-PIO phase or GPIO pad propagation',
                         'starvation is low-safe but shifts absolute deadlines',
                         'raw disable while high leaves high; owner STOP must force low',
                         'owner must abort DMA, flush, reject replay, rebase on restart',
                         'DMA transferred is not the same as PIO emitted',
                         'buffer immutability, finite no-reload DMA and model-tail admission are owner obligations',
                         'no hardware run, physical-lock or WCET claim'],
              'files': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                        (HERE / (PROGRAM+'.pio'), candidate[0].assembled_header, ENCODER, Path(__file__))}}
    (candidate[0].assembled_header.parent/'candidate-evidence.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
