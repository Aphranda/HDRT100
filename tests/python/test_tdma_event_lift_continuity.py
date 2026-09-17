"""Full production feed across PIO X wrap, independent instruction-count oracle.

PIO spends two cycles per X decrement and an extra cycle when zero falls
through its reload jump. A first edge costs one cycle; each later capture
costs five. The oracle counts cumulative instructions, never calls lift.
DMA batches intentionally supply no FIFO-empty witness in the main case.
"""
import hashlib
import json
import random
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable

MOD = 1 << 32
PERIOD = 2 * MOD + 1
U64 = (1 << 64) - 1


def pio_event(decrements, ordinal):
    return (MOD - 1 - decrements) % MOD, 2*decrements + decrements//MOD + 1 + 5*ordinal


@pytest.fixture(scope="module")
def continuity_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp("event-continuity"), "event_continuity",
        HARNESS, [ROOT / "components/tdma/src/tdma_event_observer.c"])


def stream(count=60000, empty=0, anchor=1000, width=10, step=187500):
    rows=[]
    for ordinal in range(count):
        rx,elapsed_rx=pio_event(50+ordinal*step,ordinal)
        tx,elapsed_tx=pio_event(51+ordinal*step,ordinal)
        rows.append((ordinal,rx,tx,elapsed_rx,elapsed_tx,anchor+elapsed_tx+20,empty,0,0,0))
    return anchor-width//2,anchor+width//2,rows


def run(exe,name,case):
    lo,hi,rows=case
    data=f"{lo} {hi} {len(rows)}\n"+"\n".join(" ".join(map(str,row)) for row in rows)+"\n"
    result=subprocess.run([str(exe)],input=data,text=True,capture_output=True,timeout=60)
    (exe.parent/f"{name}.json").write_text(json.dumps(dict(command=[str(exe)],
        input_sha256=hashlib.sha256(data.encode()).hexdigest(),events=len(rows),first=rows[0],last=rows[-1],
        returncode=result.returncode,stdout=result.stdout,stderr=result.stderr),indent=2),encoding="utf-8")
    assert result.returncode==0,result.stdout+result.stderr
    return result.stdout


def test_continuous_dma_without_empty_witness_crosses_two_wraps(continuity_executable):
    case=stream()
    assert case[2][-1][3]>2*PERIOD
    run(continuity_executable,"two_wraps",case)


@pytest.mark.parametrize("mask", [0,1,2,4,7])
def test_lane_empty_witnesses_do_not_change_elapsed_math(continuity_executable,mask):
    run(continuity_executable,f"empty_mask_{mask}",stream(48000,empty=mask))


def test_high_absolute_clock_crosses_two_wraps_without_uint64_add_wrap(continuity_executable):
    run(continuity_executable,"near_uint64",stream(anchor=U64-3*PERIOD,width=2))


@pytest.mark.parametrize("seed", [0xE771,0xF19])
def test_varying_pio_decrements_and_dma_read_latency(continuity_executable,seed):
    rng=random.Random(seed);rows=[];n=50
    for ordinal in range(20000):
        if ordinal:n+=rng.randrange(500,1000000)
        rx,erx=pio_event(n,ordinal);tx,etx=pio_event(n+1,ordinal)
        rows.append((ordinal,rx,tx,erx,etx,1000+etx+rng.randrange(10,500),0,0,0,0))
    assert rows[-1][3]>2*PERIOD
    run(continuity_executable,f"variable_{seed}",(995,1005,rows))


@pytest.mark.parametrize("first", [False,True])
def test_genuine_unobserved_full_wrap_remains_ambiguous(continuity_executable,first):
    lo,hi,rows=stream(1,anchor=1000,width=0)
    ordinal=0 if first else 1
    rx,erx=pio_event(MOD+550,ordinal);tx,etx=pio_event(MOD+551,ordinal)
    gap=(ordinal,rx,tx,erx,etx,1000+etx+20,0,0,0,11)
    run(continuity_executable,f"true_gap_{first}",(lo,hi,[gap] if first else rows+[gap]))


@pytest.mark.parametrize("extra", [-1,0,1])
def test_exact_second_candidate_threshold(continuity_executable,extra):
    lo,hi,rows=stream(1,anchor=1000,width=0)
    rx,erx=pio_event(550,1);tx,etx=pio_event(551,1)
    observed=1000+erx+PERIOD+extra
    rows.append((1,rx,tx,erx,etx,observed,0,0,0,11 if extra>=0 else 0))
    run(continuity_executable,f"threshold_{extra}",(lo,hi,rows))


@pytest.mark.parametrize("post", [False,True])
@pytest.mark.parametrize("bit", [1,2,4,8,16,32])
def test_faults_still_retire_entire_epoch_after_wrap(continuity_executable,post,bit):
    lo,hi,rows=stream(23000)
    final=list(rows[-1]);final[7 if not post else 8]=bit;final[9]=4 if post else 3
    rows[-1]=tuple(final)
    run(continuity_executable,f"fault_{post}_{bit}",(lo,hi,rows))


@pytest.mark.parametrize("case", ["stop_restart", "lower_overflow", "upper_clip", "empty_previous",
                                  "stale_anchor", "partial_join", "four_records"])
def test_owner_boundaries_and_tentative_state(continuity_executable,case):
    result=subprocess.run([str(continuity_executable),case],text=True,capture_output=True,timeout=30)
    (continuity_executable.parent/f"{case}.json").write_text(json.dumps(dict(returncode=result.returncode,
        stdout=result.stdout,stderr=result.stderr),indent=2),encoding="utf-8")
    assert result.returncode==0,result.stdout+result.stderr


HARNESS=r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdma_event_observer.h"
#undef assert
#define assert(condition) do { if(!(condition)) { \
 fprintf(stderr,"assertion failed line %d: %s\n",__LINE__,#condition);exit(1); \
} } while(0)
static uint32_t wire(uint32_t v)
{ return (v>>24)|((v>>8)&0xff00u)|((v<<8)&0xff0000u)|(v<<24); }
static void special(const char *name);
int main(int argc,char **argv)
{
    if(argc>1){special(argv[1]);return 0;}
    uint64_t start_lo,start_hi;uint32_t count;
    assert(scanf("%"SCNu64" %"SCNu64" %"SCNu32,&start_lo,&start_hi,&count)==3);
    tdma_event_config_t config={.pio_hz=250000000u,.epoch_limit_cycles=UINT64_MAX,
      .join_timeout_cycles=500000u,.min_frame_cycles=1000u,.min_tx_delay_cycles=0u,.max_tx_delay_cycles=8u};
    tdma_event_observer_t observer;tdma_event_observer_init(&observer);
    assert(tdma_event_observer_start(&observer,&config,1u,(tdma_event_interval_t){start_lo,start_hi},0u));
    uint64_t checksum=0;bool saw_empty=false;
    for(uint32_t i=0;i<count;++i) {
      uint32_t ordinal,rx,tx,mask,pre,post,reason;uint64_t erx,etx,observed;
      assert(scanf("%"SCNu32" %"SCNu32" %"SCNu32" %"SCNu64" %"SCNu64" %"SCNu64
         " %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32,
         &ordinal,&rx,&tx,&erx,&etx,&observed,&mask,&pre,&post,&reason)==10);
      tdma_event_batch_t batch={.epoch=1u,.observed={observed-10u,observed},
        .pre_faults=pre,.post_faults=post,.count={2u,2u,1u},.empty_mask=(uint8_t)mask};
      batch.words[0][0]=rx;batch.words[0][1]=UINT32_MAX-ordinal;
      batch.words[1][0]=tx;batch.words[1][1]=UINT32_MAX-ordinal;
      batch.words[2][0]=wire(100u+ordinal);
      tdma_event_record_t out[TDMA_EVENT_MAX_RECORDS];memset(out,0xa5,sizeof(out));
      const size_t produced=tdma_event_observer_feed(&observer,&batch,out);
      saw_empty=saw_empty || mask!=0u;
      if(reason) {
        assert(!produced && observer.state==TDMA_EVENT_INVALID && observer.reason==(tdma_event_reason_t)reason);
        assert(observer.fault_bits==(pre|post));
        for(unsigned k=0;k<TDMA_EVENT_STREAMS;++k)assert(!observer.pending_count[k]);
        assert(tdma_event_observer_feed(&observer,&batch,out)==0u);continue;
      }
      if(produced!=1u || observer.state!=TDMA_EVENT_ACTIVE) {
        fprintf(stderr,"event=%u expected_rx=%"PRIu64" state=%u reason=%u fault=%u produced=%zu\n",
          ordinal,erx,(unsigned)observer.state,(unsigned)observer.reason,observer.fault_bits,produced);return 1;
      }
      assert(out[0].rx_elapsed_cycles==erx && out[0].tx_elapsed_cycles==etx);
      assert(out[0].ordinal==ordinal && out[0].sequence==100u+ordinal && out[0].epoch==1u);
      assert(out[0].start_bounds.lo<=start_lo+(start_hi-start_lo)/2u &&
             out[0].start_bounds.hi>=start_lo+(start_hi-start_lo)/2u);
      assert(out[0].diagnostic_only && out[0].physical_first_unproved && out[0].identity_unproved &&
        !out[0].timestamp_valid && !out[0].dpll_eligible);
      assert(observer.reads_last==5u && !observer.fault_bits);
      if(!saw_empty)for(unsigned k=0;k<2u;++k) {
        assert(observer.last_empty[k]==start_lo && observer.previous[k].age.lo==start_lo);
      }
      checksum^=erx+etx+ordinal;
    }
    printf("events=%u joined=%"PRIu64" state=%u reason=%u checksum=%"PRIu64"\n",count,observer.joined,
      (unsigned)observer.state,(unsigned)observer.reason,checksum);
    return 0;
}
static tdma_event_batch_t pair(uint32_t epoch,uint32_t ordinal,uint32_t n,uint64_t now)
{
    tdma_event_batch_t b={.epoch=epoch,.observed={now,now},.count={2u,2u,1u}};
    b.words[0][0]=UINT32_MAX-n;b.words[1][0]=UINT32_MAX-n-1u;
    b.words[0][1]=b.words[1][1]=UINT32_MAX-ordinal;b.words[2][0]=wire(100u+ordinal);return b;
}
static void special(const char *name)
{
    tdma_event_config_t cfg={.pio_hz=250000000u,.epoch_limit_cycles=UINT64_MAX,
        .join_timeout_cycles=500000u,.min_frame_cycles=1000u,.max_tx_delay_cycles=8u};
    tdma_event_observer_t o;tdma_event_observer_init(&o);
    assert(tdma_event_observer_start(&o,&cfg,1u,(tdma_event_interval_t){1000u,1000u},0u));
    tdma_event_record_t out[4];
    tdma_event_batch_t b=pair(1u,0u,50u,1200u);
    assert(tdma_event_observer_feed(&o,&b,out)==1u);
    if(!strcmp(name,"stop_restart")) {
        tdma_event_observer_stop(&o);assert(o.state==TDMA_EVENT_STOPPED && !o.elapsed[0] && !o.elapsed[1]);
        assert(!tdma_event_observer_feed(&o,&b,out));
        assert(!tdma_event_observer_start(&o,&cfg,1u,(tdma_event_interval_t){2000u,2000u},0u));
        assert(tdma_event_observer_start(&o,&cfg,2u,(tdma_event_interval_t){2000u,2000u},0u));
        b=pair(2u,0u,50u,2200u);assert(tdma_event_observer_feed(&o,&b,out)==1u);
        assert(out[0].epoch==2u && out[0].ordinal==0u && out[0].rx_elapsed_cycles==101u);
        b.epoch=1u;assert(!tdma_event_observer_feed(&o,&b,out) && o.reason==TDMA_EVENT_BAD_EPOCH);return;
    }
    if(!strcmp(name,"partial_join")) {
        b=pair(1u,1u,550u,2200u);tdma_event_batch_t tail=b;
        b.count[1]=b.count[2]=0u;assert(!tdma_event_observer_feed(&o,&b,out));
        assert(o.state==TDMA_EVENT_ACTIVE && o.joined==1u && o.pending_count[0]==2u);
        tail.count[0]=0u;assert(tdma_event_observer_feed(&o,&tail,out)==1u);
        assert(out[0].rx_elapsed_cycles==1106u && out[0].tx_elapsed_cycles==1108u);return;
    }
    if(!strcmp(name,"four_records")) {
        b=(tdma_event_batch_t){.epoch=1u,.observed={5300u,5300u},.count={8u,8u,4u}};
        for(unsigned i=0;i<4u;++i) {
            b.words[0][i*2]=UINT32_MAX-550u-500u*i;b.words[1][i*2]=b.words[0][i*2]-1u;
            b.words[0][i*2+1]=b.words[1][i*2+1]=UINT32_MAX-1u-i;b.words[2][i]=wire(101u+i);
        }
        assert(tdma_event_observer_feed(&o,&b,out)==4u && o.reads_last==20u);
        for(unsigned i=0;i<4u;++i)assert(out[i].rx_elapsed_cycles==1106u+1005u*i);
        return;
    }
    /* Deliberate internal corruption exercises arithmetic defense unreachable
     * from accepted public feeds; never presented as a hardware scenario. */
    b=pair(1u,1u,550u,2200u);
    tdma_event_reason_t expected=TDMA_EVENT_ABSOLUTE_TIME;
    if(!strcmp(name,"lower_overflow")) {o.anchor_bounds.lo=UINT64_MAX-100u;o.anchor_bounds.hi=UINT64_MAX;expected=TDMA_EVENT_TIME_OVERFLOW;}
    else if(!strcmp(name,"upper_clip")) {
        o.anchor_bounds.hi=UINT64_MAX;
        assert(tdma_event_observer_feed(&o,&b,out)==1u);
        assert(out[0].rx_elapsed_cycles==1106u && out[0].tx_elapsed_cycles==1108u);return;
    } else if(!strcmp(name,"empty_previous"))o.previous[0].age=(tdma_event_interval_t){1200u,1300u};
    else if(!strcmp(name,"stale_anchor"))o.anchor_bounds=(tdma_event_interval_t){10000u,10000u};
    else assert(0);
    assert(!tdma_event_observer_feed(&o,&b,out) && o.reason==expected && o.state==TDMA_EVENT_INVALID);
    for(unsigned i=0;i<TDMA_EVENT_STREAMS;++i)assert(!o.pending_count[i]);
}
'''
