"""Execute the production local-to-raw enclosure against a rational oracle.

Exercises overflow, full-width coordinates and fractional timer uncertainty;
no hardware precision or runtime admission claim is implied.
"""
from dataclasses import dataclass
from pathlib import Path
import random, subprocess
from fractions import Fraction
import pytest
from test_vdc_command_owner import compile_executable

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists())
U64=(1<<64)-1
B=1_000_000_000
@dataclass(frozen=True)
class C:
 raw_before:int;raw_after:int;local:int;hz:int;target:int

def oracle(c):
 if not (c.hz and c.hz<=500_000_000 and c.raw_after>=c.raw_before and c.raw_after<U64):return None
 if c.local>U64-1000 or c.target<c.local+1000 or c.target-c.local>2_000_000_000:return None
 d=c.target-c.local
 # Open real-valued target-raw interval corners, then integer-ceiling image.
 low=Fraction(c.raw_before)+Fraction((d-1000)*c.hz,B)
 high=Fraction(c.raw_after+1)+Fraction(d*c.hz,B)
 lo=low.numerator//low.denominator+1
 hi=-((-high.numerator)//high.denominator)
 if lo>U64 or hi>U64 or hi<lo:return None
 return lo,hi

HARNESS=r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "vdc_future_raw.h"
int main(void){uint64_t rb,ra,l,t;uint32_t hz;vdc_local_raw_bracket_t z={1,2};assert(!vdc_timestamp_bridge_local_to_raw(NULL,0,&z));assert(z.lo==1&&z.hi==2);while(scanf("%"SCNu64" %"SCNu64" %"SCNu64" %"SCNu32" %"SCNu64,&rb,&ra,&l,&hz,&t)==5){vdc_timestamp_clock_bridge_t b={.raw_before=rb,.raw_after=ra,.local_ns=l,.tick_hz=hz},copy=b;vdc_local_raw_bracket_t x={UINT64_C(0xaaaaaaaaaaaaaaaa),UINT64_C(0xbbbbbbbbbbbbbbbb)},old=x;assert(!vdc_timestamp_bridge_local_to_raw(&b,t,NULL));bool ok=vdc_timestamp_bridge_local_to_raw(&b,t,&x);assert(!memcmp(&b,&copy,sizeof b));if(!ok){assert(!memcmp(&x,&old,sizeof x));puts("0");}else printf("1 %"PRIu64" %"PRIu64"\n",x.lo,x.hi);}return 0;}
'''
@pytest.fixture(scope='module')
def exe(tmp_path_factory):
 return compile_executable(tmp_path_factory.mktemp('vdc-future-raw'),'clock',HARNESS)

def run(exe,cases):
 inp=''.join('%d %d %d %d %d\n'%tuple(vars(c).values()) for c in cases)
 p=subprocess.run([str(exe)],input=inp,text=True,capture_output=True,timeout=30);assert p.returncode==0,p.stderr
 got=[tuple(map(int,x.split())) for x in p.stdout.splitlines()]
 assert len(got)==len(cases)
 for c,row in zip(cases,got):
  want=oracle(c);assert row==((0,) if want is None else (1,*want)),(c,row,want)
 return got

def test_edges(exe):
 cs=[C(100,100,10_000,250_000_000,11_000),C(100,105,10_000,250_000_000,11_000),
 C(0,U64-1,0,1,1000),C(U64-100,U64-1,0,500_000_000,U64),
 C(0,0,0,500_000_000,2_000_000_000),C(0,0,0,500_000_001,1000),
 C(0,0,0,1,999),C(0,0,U64,1,U64),C(0,U64,0,1,1000)]
 run(exe,cs)

def test_random(exe):
 r=random.Random(0xC10C);cs=[]
 for _ in range(5000):
  l=r.getrandbits(64); d=r.randrange(0,2_000_001_000);t=l+d if l<=U64-d else r.getrandbits(64)
  cs.append(C(r.getrandbits(64),r.getrandbits(64),l,r.choice([1,12_000_000,250_000_000,500_000_000,500_000_001]),t))
 run(exe,cs)

def test_fractional_timer_phase_and_local_read_samples(exe):
 cs=[C(100,100+span,10000,hz,10000+d) for span in (0,1,29)
     for hz in (1,12_000_000,249_999_937,250_000_000,500_000_000)
     for d in (1000,1001,1999,2000,1_000_000_001,2_000_000_000)]
 rows=run(exe,cs)
 for c,row in zip(cs,rows):
  assert row[0]
  for q in (Fraction(c.raw_before),Fraction(c.raw_before+c.raw_after,2),Fraction(c.raw_after)+Fraction(999999,1000000)):
   for x in (Fraction(c.local),Fraction(c.local)+Fraction(1,3),Fraction(c.local)+Fraction(999999999,1000000)):
    exact=q+(c.target-x)*c.hz/B
    first=-((-exact.numerator)//exact.denominator)
    assert row[1]<=first<=row[2],(c,q,x,first,row)
 # Without the raw-read fractional tick allowance, this admissible bridge
 # reaches the target one tick later than raw_after+ceil(d*hz/B).
 c=C(100,100,10000,250_000_000,11000)
 q=Fraction(100)+Fraction(3,4);x=Fraction(10000)
 exact=q+(c.target-x)*c.hz/B
 assert -((-exact.numerator)//exact.denominator)==351
 assert c.raw_after+(1000*c.hz+B-1)//B==350

def test_full_width_coordinates_and_max_horizon(exe):
 run(exe,[C(U64-251,U64-251,U64-1000,250_000_000,U64),
  C(U64-250,U64-250,U64-1000,250_000_000,U64),
  C(0,0,U64-1000,1,U64),C(0,0,U64-999,1,U64),
  C(0,0,0,500_000_000,2_000_000_001),C(0,0,0,0,1000)])
