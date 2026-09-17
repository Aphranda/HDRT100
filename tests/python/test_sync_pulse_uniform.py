"""Actual assembled fixed-width PIO, C encoder, backend and finite FIFO model.

The DMA model uses a stated bounded write latency. It proves encoding/inventory
geometry, not the target bus latency, GPIO timing, continuity or lock quality.
"""
from collections import deque
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess

import pytest
from test_sync_io_run_output import executable

ROOT = Path(__file__).resolve().parents[2]
U32, U64 = (1 << 32)-1, (1 << 64)-1
ENCODER = ROOT/'components/sync_io/inc/sync_pulse_uniform_encode.h'


def assembled(path, program):
    text = path.read_text(encoding='utf-8')
    body = text.split(program+'_program_instructions[] = {')[1].split('};')[0]
    return [int(word,16) for word in re.findall(r'0x([0-9a-fA-F]{4}),',body)]


class FifoMachine:
    """Decode actual instructions with finite 4/8-word FIFO and paced DMA.

    Each freed FIFO slot admits a DMA word after dma_latency ticks; writes are
    serialized at that interval. The model records the final write tick and
    refuses replacing a DMA source while unread source words remain.
    """
    def __init__(self, code, *, high_count=0, origin=0, offset=0,
                 fifo_capacity=8, dma_latency=4):
        self.code = {i+offset: ((word & ~31) | ((word & 31)+offset)) if word >> 13 == 0 else word
                     for i,word in enumerate(code)}
        self.lo, self.hi = offset, offset+len(code)-1
        self.pc, self.tick = offset, origin
        self.x = self.y = self.osr = 0
        self.isr = high_count
        self.pin = 0
        self.enabled = True
        self.capacity, self.latency = fifo_capacity, dma_latency
        self.fifo, self.pending = deque(), deque()
        self.dma_next, self.retired_at = origin, None
        self.edges, self.stalls, self.retirements = [], [], []
        self.max_fifo = 0

    def submit(self, words, preload=0):
        assert not self.pending, 'live DMA source cannot be overwritten'
        assert len(self.fifo)+preload <= self.capacity
        self.fifo.extend(tuple(words[:preload]))
        self.max_fifo=max(self.max_fifo,len(self.fifo))
        self.pending = deque(words[preload:])
        self.dma_next = self.tick+self.latency
        self.retired_at = self.tick if not self.pending else None

    def service_dma(self, until):
        while self.pending and len(self.fifo)<self.capacity and self.dma_next<=until:
            self.fifo.append(self.pending.popleft())
            self.max_fifo = max(self.max_fifo,len(self.fifo))
            if not self.pending:
                self.retired_at = self.dma_next
                self.retirements.append(self.retired_at)
            self.dma_next += self.latency

    def step(self):
        assert self.enabled
        self.service_dma(self.tick)
        instruction = self.code[self.pc]
        assert instruction & 0x1f00 == 0
        opcode = instruction >> 13
        nxt = self.lo if self.pc==self.hi else self.pc+1
        cost = 1
        if opcode==4:
            assert instruction & 0xff == 0xa0
            if not self.fifo:
                self.stalls.append((self.tick,self.pc,self.pin))
                self.tick += 1
                return
            was_full=len(self.fifo)==self.capacity
            self.osr = self.fifo.popleft()
            if was_full:
                self.dma_next = max(self.dma_next,self.tick+self.latency)
        elif opcode==3:
            dest,bits = (instruction >> 5)&7,instruction&31
            assert bits==0 and dest in (1,2)
            if dest==1: self.x=self.osr
            else: self.y=self.osr
            self.osr=0
        elif opcode==5:
            dest,operation,src = (instruction >> 5)&7,(instruction >> 3)&3,instruction&7
            assert dest==2 and operation==0 and src==6, 'only MOV Y, ISR'
            self.y=self.isr
        elif opcode==0:
            cond,target = (instruction >> 5)&7,instruction&31
            assert cond in (2,4) and target==self.pc
            cost=(self.x if cond==2 else self.y)+1
            if cond==2: self.x=U32
            else: self.y=U32
        elif opcode==7:
            assert (instruction >> 5)&7==0
            pin=instruction&31
            assert pin in (0,1)
            if self.pin!=pin: self.edges.append((self.tick,pin))
            self.pin=pin
        else: raise AssertionError(hex(instruction))
        self.service_dma(self.tick+cost-1)
        self.pc=nxt
        self.tick+=cost

    def run_edges(self, count):
        for _ in range(20000):
            if len(self.edges)==count: return
            self.step()
        raise AssertionError('bounded PIO execution exhausted')

    def stop(self):
        self.enabled=False
        if self.pin: self.edges.append((self.tick,0))
        self.pin=0
        self.pending.clear()
        self.fifo.clear()


@pytest.fixture(scope='module')
def uniform(executable):
    d=executable.parent
    source=d/'uniform_encoder.c'
    source.write_text('#include "'+ENCODER.as_posix()+'"\n'
        'bool encode(uint64_t origin,bool first,uint64_t previous,uint64_t rise,uint64_t fall,'
        'uint32_t high,uint32_t *word) {return sync_pulse_uniform_encode(origin,first,previous,rise,fall,high,word);}\n',encoding='utf-8')
    cc=os.environ.get('HOST_CC') or shutil.which('gcc') or 'D:/Microsoft/mingw64/bin/gcc.exe'
    libpath=d/('uniform_encoder.dll' if os.name=='nt' else 'uniform_encoder.so')
    proc=subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared',str(source),'-o',str(libpath)],
                        capture_output=True,text=True,timeout=30)
    assert proc.returncode==0,proc.stderr
    lib=C.CDLL(str(libpath))
    lib.encode.argtypes=[C.c_uint64,C.c_bool,C.c_uint64,C.c_uint64,C.c_uint64,C.c_uint32,C.POINTER(C.c_uint32)]
    lib.encode.restype=C.c_bool
    return lib,assembled(d/'sync_pulse_uniform_out1.pio.h','sync_pulse_uniform_out1')


def encode(lib, origin, first, previous, rise, fall, high):
    result=C.c_uint32(0xa5a5a5a5)
    valid=lib.encode(origin,first,previous,rise,fall,high,C.byref(result))
    if not valid:
        assert result.value==0xa5a5a5a5
        return None
    return result.value


def test_uniform_opcode_cycles_and_fixed_ISR(uniform):
    _,code=uniform
    assert len(code)==7
    for low in range(10):
        for high in range(10):
            for offset in (0,12,25):
                m=FifoMachine(code,high_count=high,origin=31,offset=offset)
                m.submit([low,low],preload=2)
                m.run_edges(4)
                t=[tick for tick,_ in m.edges]
                assert t[0]==31+low+4 and t[1]-t[0]==high+2
                assert t[2]-t[1]==low+5 and t[3]-t[2]==high+2
                assert m.isr==high and not m.stalls


def test_uniform_real_encoder_random_sequences(uniform):
    lib,code=uniform
    rng=random.Random(0x600105)
    for _ in range(600):
        origin=rng.randrange(0,U64-1000000)
        high=rng.randrange(2,100)
        m=FifoMachine(code,high_count=high-2,origin=origin,offset=rng.randrange(26),dma_latency=1)
        previous=origin
        words,expected=[],[]
        for i in range(rng.randrange(1,33)):
            rise=previous+rng.randrange(5,400)
            fall=rise+high
            words.append(encode(lib,origin,i==0,previous,rise,fall,high))
            assert words[-1] is not None
            expected.extend(((rise,1),(fall,0)))
            previous=fall
        m.submit(words,preload=1)
        m.run_edges(len(expected))
        assert m.edges==expected and not m.stalls and not m.pending


@pytest.mark.parametrize('first',[True,False])
def test_uniform_u32_u64_boundaries_and_rejections(uniform,first):
    lib,code=uniform
    origin=100
    previous=101
    reference=origin if first else previous
    overhead=4 if first else 5
    for low in (0,1,U32-1,U32):
        for high in (2,500,U32):
            rise=reference+low+overhead
            fall=rise+high
            assert encode(lib,origin,first,previous,rise,fall,high)==low
            m=FifoMachine(code,high_count=high-2,origin=reference if first else reference+1)
            m.submit([low],preload=1);m.run_edges(2)
            assert m.edges==[(rise,1),(fall,0)]
    assert encode(lib,U64-100,first,U64-99,U64-2,U64,2) is not None
    for args in ((origin,first,previous,reference+overhead-1,reference+overhead+1,2),
                 (origin,first,previous,reference+overhead+U32+1,reference+overhead+U32+3,2),
                 (origin,first,previous,200,199,2),
                 (origin,first,previous,U64-1,0,2),
                 (origin,first,previous,200,203,2),
                 (origin,first,previous,200,200,0),
                 (origin,first,previous,200,201,1)):
        assert encode(lib,*args) is None
    assert encode(lib,100,False,99,200,202,2) is None
    assert not lib.encode(100,True,0,200,202,2,None)


BASE_CASES=['generation','async_stop','fault_starved','fault_pause','fault_source','fault_hz',
    'fault_divider','fault_expired','fault_dma_ahb','fault_dma_read','fault_dma_write',
    'enable_start_failure','enable_cancel_final','enable_pc_not_started','enable_pc_past_guard',
    'enable_pc_upper_valid','enable_pc_first_high','enable_raw_backwards','enable_raw_too_wide',
    'enable_raw_u64_max','enable_exact_guard','enable_inside_guard','enable_observation_order',
    'enable_observed_failure','enable_after_failure','enable_both_failure','enable_observed_wrap',
    'enable_after_wrap','enable_observed_backwards_then_forward','enable_after_backwards',
    'enable_pc_below_program','enable_cancel_at_pc','inactive_submit','inactive_release',
    'prepare_reserve','prepare_workspace','prepare_claim','prepare_load','prepare_arm','prepare_pio','prepare_pio_init']


@pytest.mark.parametrize('name',['uniform_base:'+name for name in BASE_CASES]+
    ['uniform_invalid:'+name for name in ('zero','over_max','null','width_prepared','width_running',
                                         'wrap','ordinal','model','too_far')]+['uniform_prepare_gates'])
def test_uniform_actual_backend_guards(executable,name):
    result=subprocess.run([str(executable),name],capture_output=True,text=True,timeout=15)
    (executable.parent/(name.replace(':','_')+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stdout+result.stderr


@pytest.mark.parametrize('name',['one','four','ten','max'])
def test_uniform_actual_backend_words_execute_contiguously(executable,uniform,name):
    result=subprocess.run([str(executable),'uniform_stream_'+name],capture_output=True,text=True,timeout=15)
    assert result.returncode==0,result.stdout+result.stderr
    words=list(map(int,result.stdout.split()))
    assert len(words)==17
    m=FifoMachine(uniform[1],high_count=998,origin=1000000,offset=12)
    m.submit(words,preload=1);m.run_edges(34)
    assert m.edges==[(1100000+i*250000+level*1000,1-level) for i in range(17) for level in (0,1)]
    assert not m.stalls and m.isr==998
    m.step()
    assert m.stalls[-1][2]==0 and m.pin==0
    m.stop()
    assert not m.enabled and not m.pending and not m.fifo


@pytest.mark.parametrize('capacity',[4,8])
@pytest.mark.parametrize('count',[1,4,10,16])
@pytest.mark.parametrize('kind',['paired','uniform'])
def test_source_retirement_inventory_geometry(executable,uniform,capacity,count,kind):
    period=250000
    high=500
    origin=0
    # A steady suffix follows a preceding fall at zero. Source is submitted
    # before the PULL at tick 1. The first pulse is one period after that fall.
    rise=period-high
    expected=[(rise+i*period+phase*high,1-phase) for i in range(count) for phase in (0,1)]
    if kind=='uniform':
        words=[encode(uniform[0],origin,False,0,rise,rise+high,high)]*count
        code=uniform[1]
        per_edge,preload,overhead=1,0,1
    else:
        words=[high-2,period-high-6]*count
        code=assembled(executable.parent/'sync_pulse_stream_out1.pio.h','sync_pulse_stream_out1')
        per_edge,preload,overhead=2,0,3
    m=FifoMachine(code,high_count=high-2,origin=1,fifo_capacity=capacity,dma_latency=4)
    # Give the source the normal pre-PULL lead. Preloading a full available
    # inventory models DMA that has filled FIFO before the current pair starts.
    preload=min(capacity,len(words))
    m.submit(words,preload=preload)
    m.run_edges(2*count)
    assert m.edges==expected and not m.stalls and m.max_fifo<=capacity
    runway=expected[-1][0]-m.retired_at
    if count>capacity//per_edge+1:
        ideal=(capacity//per_edge+1)*period-overhead
        # Paired PULLs are two cycles apart; with serialized 4-cycle writes
        # the second word completes six cycles after its PULL. Uniform needs
        # only one write and therefore incurs the stated four-cycle latency.
        assert runway==ideal-(4 if per_edge==1 else 6)
    else:
        assert runway>0
    report={'kind':kind,'count':count,'fifo_capacity':capacity,'words_per_edge':per_edge,
            'source_last_write_tick':m.retired_at,'last_fall_tick':expected[-1][0],
            'runway_ticks':runway,'runway_us':runway/250,'assumed_dma_write_latency_ticks':4,
            'hardware_evidence':False,'max_fifo_occupancy':m.max_fifo}
    (executable.parent/f'geometry-{kind}-{count}-{capacity}.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


def test_finite_dma_source_not_replaced_and_low_safe_starvation(uniform):
    m=FifoMachine(uniform[1],high_count=498)
    m.submit([250000]*16,preload=1)
    with pytest.raises(AssertionError,match='live DMA'): m.submit([0])
    m.run_edges(32)
    old=list(m.edges)
    m.step()
    assert m.stalls[-1][2]==0 and m.edges==old
    m.stop()
    assert not m.enabled and not m.pin and not m.pending and not m.fifo


def test_record_uniform_source_hashes(executable,uniform):
    paths=[ENCODER,ROOT/'components/sync_io/src/sync_pulse_uniform_out1.pio',
           executable.parent/'sync_pulse_uniform_out1.pio.h',Path(__file__)]
    report={'assembled_instructions':uniform[1],'hardware_opened':False,
            'limitations':['DMA model assumes stated bounded bus write latency.',
                          'No physical continuity, output precision, WCET or lock qualification.'],
            'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    (executable.parent/'uniform-model-evidence.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
