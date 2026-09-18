"""Direct TIMER1 conversions checked against rational arithmetic."""
from fractions import Fraction
import math
import random
import subprocess

import pytest

from test_vdc_command_owner import compile_executable

U64=(1<<64)-1
HARNESS=r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include "vdc_timer1_coordinate.h"
int main(void) {
    uint64_t a,b,out,lo,hi;uint32_t mode,hz,up;
    while(scanf("%"SCNu32" %"SCNu32" %"SCNu64" %"SCNu64" %"SCNu32,
        &mode,&hz,&a,&b,&up)==5) {
        out=lo=hi=77;
        const bool ok=mode==0 ? vdc_timer1_ticks_to_ns(hz,a,up,&out) :
            mode==1 ? vdc_timer1_ns_to_ticks(hz,a,up,&out) :
            vdc_timer1_interval_to_ns(hz,a,b,&lo,&hi);
        if(!ok)assert(out==77 && lo==77 && hi==77);
        printf("%u %"PRIu64" %"PRIu64" %"PRIu64"\n",ok,out,lo,hi);
    }
    uint64_t sentinel=5;
    assert(!vdc_timer1_ticks_to_ns(250000000,1,false,NULL));
    assert(!vdc_timer1_ns_to_ticks(250000000,1,false,NULL));
    assert(!vdc_timer1_interval_to_ns(250000000,1,2,&sentinel,&sentinel));
    assert(sentinel==5);
}
'''


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp('timer1-coordinate'),'coordinate',HARNESS)


def test_rational_conversion_and_overflow(exe):
    rng=random.Random(0x71AE1)
    cases=[]
    for _ in range(3000):
        cases.append((rng.randrange(3),rng.choice((0,1,3,125000000,250000000,333333333,500000000,500000001)),
                      rng.randrange(U64+1),rng.randrange(U64+1),rng.randrange(2)))
    for hz in (1,3,250000000,500000000):
        for value in (0,1,3,4,999,1000,10**9,U64//4,U64//4+1,U64):
            for mode in range(3):
                for up in (0,1):
                    cases.append((mode,hz,value,value,up))
    data=''.join(' '.join(map(str,c))+'\n' for c in cases)
    result=subprocess.run([str(exe)],input=data,text=True,capture_output=True,timeout=30)
    assert result.returncode==0,result.stderr
    rows=[tuple(map(int,line.split())) for line in result.stdout.splitlines()]
    assert len(rows)==len(cases)
    for (mode,hz,a,b,up),got in zip(cases,rows):
        expected=None
        if 0<hz<=500000000:
            if mode<2:
                ratio=Fraction(10**9,hz) if mode==0 else Fraction(hz,10**9)
                value=(math.ceil if up else math.floor)(a*ratio)
                if value<=U64:expected=(1,value,77,77)
            elif a<=b:
                lo=math.floor(Fraction(a*10**9,hz));hi=math.ceil(Fraction(b*10**9,hz))
                if hi<=U64:expected=(1,77,lo,hi)
        assert got==(expected or (0,77,77,77)),((mode,hz,a,b,up),got,expected)
