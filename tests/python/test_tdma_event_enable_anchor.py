"""Execute the actual device-only TDMA enable anchor with MMIO read hooks."""
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
U64 = (1 << 64) - 1

HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hardware/pio.h"
#include "hardware/structs/timer.h"
static timer_hw_t bank;
static pio_hw_t pio_bank;
static uint32_t samples[6], reads, enables, clocks, fences;
static uint32_t expected_mask, expected_hz;
static bool current[2];
static char sequence[32];
static size_t sequence_count;

static void record(char kind) {
    assert(sequence_count+1u<sizeof(sequence));
    sequence[sequence_count++]=kind;
    sequence[sequence_count]=0;
}

timer_hw_t *anchor_timer_read_hook(void) {
    assert(reads<6u && clocks==1u);
    assert(enables==(reads<3u ? 0u : 1u));
    assert(fences==(reads<3u ? 1u : 3u));
    /* Every pointer evaluation advances one raw MMIO read. Wrong H/L order
     * reads the poisoned field instead of the supplied value. */
    const bool low=reads==2u || reads==3u;
    bank.timerawh=low ? UINT32_C(0xbaadbeef) : samples[reads];
    bank.timerawl=low ? samples[reads] : UINT32_C(0xbaadbeef);
    record(low ? 'L' : 'H');
    ++reads;
    return &bank;
}

bool vdc_timestamp_clock_is_current(uint32_t hz) {
    assert(clocks<2u && hz==expected_hz);
    assert(reads==(clocks ? 6u : 0u));
    assert(enables==clocks && fences==(clocks ? 4u : 0u));
    record('C');
    return current[clocks++];
}

void pio_enable_sm_mask_in_sync(PIO pio, uint32_t mask) {
    assert(pio==&pio_bank && mask==expected_mask);
    assert(reads==3u && clocks==1u && enables==0u && fences==2u);
    ++enables;
    record('E');
}

static void anchor_fence_hook(int order) {
    assert(order==__ATOMIC_SEQ_CST);
    assert(fences<4u);
    assert(reads==(fences==0u ? 0u : fences==3u ? 6u : 3u));
    assert(clocks==1u && enables==(fences>=2u ? 1u : 0u));
    ++fences;
    record('F');
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
#define __atomic_thread_fence(order) anchor_fence_hook(order)
#include "tdma_event_enable_anchor.h"
#undef __atomic_thread_fence

int main(void) {
    unsigned before_current,after_current,pointers;
    while (scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                 " %" SCNu32 " %u %u %u %" SCNu32 " %" SCNu32,
                 &samples[0],&samples[1],&samples[2],&samples[3],&samples[4],
                 &samples[5],&before_current,&after_current,&pointers,&expected_mask,&expected_hz)==11) {
        current[0]=before_current!=0u; current[1]=after_current!=0u;
        reads=enables=clocks=fences=0u;sequence_count=0u;sequence[0]=0;
        uint64_t before=UINT64_C(0x1122334455667788),after=UINT64_C(0x8877665544332211);
        const uint64_t old_before=before,old_after=after;
        bool ok=tdma_event_enable_anchor_capture(&pio_bank,expected_mask,expected_hz,
            pointers==1u ? NULL : &before,pointers==2u ? NULL : pointers==3u ? &before : &after);
        assert(reads==6u && enables==1u && clocks==2u && fences==4u);
        assert(strcmp(sequence,"CFHHLFEFLHHFC")==0);
        if (!ok) assert(before==old_before && after==old_after);
        printf("%u %" PRIu64 " %" PRIu64 "\n",ok ? 1u : 0u,before,after);
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def anchor_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("tdma-enable-anchor")
    files = {
        "pico/platform.h": '#pragma once\n#define __not_in_flash_func(name) name\n',
        "hardware/pio.h": '#pragma once\n#include <stdint.h>\ntypedef struct { uint32_t ctrl; } pio_hw_t;\ntypedef pio_hw_t *PIO;\nvoid pio_enable_sm_mask_in_sync(PIO, uint32_t);\n',
        "hardware/structs/timer.h": '#pragma once\n#include <stdint.h>\ntypedef struct { volatile uint32_t timerawh, timerawl; } timer_hw_t;\ntimer_hw_t *anchor_timer_read_hook(void);\n#define timer1_hw (anchor_timer_read_hook())\n',
        "anchor.c": HARNESS,
    }
    for name, content in files.items():
        path=directory/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler="D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "Host compiler required to execute production device helper"
    exe=directory/("anchor.exe" if os.name=="nt" else "anchor")
    command=[compiler,"-std=c11","-O2","-Wall","-Wextra","-Werror","-DPICO_ON_DEVICE=1",
             f"-I{directory}",f"-I{ROOT/'components/tdma/inc'}",
             f"-I{ROOT/'components/vdc_domain/inc'}",str(directory/'anchor.c'),"-o",str(exe)]
    result=subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert result.returncode==0,result.stdout+result.stderr
    return exe


def case(before, after, *, first_current=1, last_current=1, pointers=0, mask=14, hz=250000000):
    return (before>>32,before>>32,before&0xffffffff,
            after&0xffffffff,after>>32,after>>32,
            first_current,last_current,pointers,mask,hz)


def run_cases(executable, cases):
    result=subprocess.run([str(executable)],input="\n".join(" ".join(map(str,c)) for c in cases)+"\n",
                          capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+result.stderr
    rows=[tuple(map(int,line.split())) for line in result.stdout.splitlines()]
    assert len(rows)==len(cases)
    for c,row in zip(cases,rows):
        lower=(c[0]<<32)|c[2];upper=(c[1]<<32)|c[3]
        ok=c[0]==c[4] and c[1]==c[5] and c[6] and c[7] and c[8]==0 and upper>lower
        assert row==((1,lower,upper) if ok else (0,0x1122334455667788,0x8877665544332211)),(c,row)
    return rows


def test_clock_checks_stay_outside_enable_enclosure(anchor_executable):
    cases=[case(100,120,first_current=a,last_current=b,mask=mask,hz=hz)
           for a in (0,1) for b in (0,1) for mask in (2,6,14)
           for hz in (125000000,250000000)]
    run_cases(anchor_executable,cases)


def test_single_enable_even_with_invalid_output_objects(anchor_executable):
    run_cases(anchor_executable,[case(100,120,pointers=p,first_current=a,last_current=b)
                                for p in (1,2,3) for a in (0,1) for b in (0,1)])


def test_incoherent_high_words_reject_without_retry(anchor_executable):
    cases=[]
    for index in (0,1,4,5):
        c=list(case(0x1234567800000010,0x1234567800000020))
        c[index]^=1
        cases.append(c)
    cases += [case(0xffffffff,0x100000000),case(U64,0)]
    assert all(row[0]==0 for row in run_cases(anchor_executable,cases))


def test_coherent_positive_intervals_preserve_full_64_bit_values(anchor_executable):
    cases=[case(0,1),case(0xfffffffe,0xffffffff),
           case(0x1234567800000010,0x12345678fffffff0),case(U64-1,U64)]
    assert all(row[0]==1 for row in run_cases(anchor_executable,cases))


@pytest.mark.parametrize('wrap_after',range(1,6))
@pytest.mark.parametrize('high',[0x12345678,0xffffffff])
def test_rollover_at_every_overlapping_read_boundary_rejects(anchor_executable,wrap_after,high):
    # Chronological physical timer values, with a wrap in each of the five
    # gaps, including across enable. The production helper still enables once
    # and leaves both output objects untouched when an observation tears.
    samples=[]
    for index in range(6):
        tick=(high<<32)+0xffffffff-wrap_after+index+1
        tick &= U64
        samples.append((tick&0xffffffff) if index in (2,3) else (tick>>32))
    assert run_cases(anchor_executable,[tuple(samples)+(1,1,0,14,250000000)])[0][0]==0


def test_equal_reversed_and_full_counter_wrap_rejected(anchor_executable):
    cases=[case(0,0),case(100,100),case(U64,U64),case(101,100),
           case(0x100000000,0xffffffff),case(U64,0)]
    assert all(row[0]==0 for row in run_cases(anchor_executable,cases))


def test_random_full_width_enclosures_preserve_exact_endpoints(anchor_executable):
    rng=random.Random(0xA6C40)
    cases=[]
    for _ in range(500):
        before=rng.randrange(U64);after=rng.randrange(before+1,U64+1)
        cases.extend([case(before,after),case(after,before)])
        coherent_after=(before&~0xffffffff)|rng.randrange((before&0xffffffff)+1,1<<32)
        cases.append(case(before,coherent_after))
    run_cases(anchor_executable,cases)
