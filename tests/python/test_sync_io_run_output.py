"""Execute actual continuous-output backend with bounded hardware mocks.

The real encoder and generated production PIO helper are compiled unchanged.
Actual emitted DMA/FIFO words are also interpreted as assembled PIO instructions.
Mocks cover peripheral calls and persona/arena admission, not physical timing.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess

import pytest

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'components/sync_io/src/sync_io_run_output.c'
from test_sync_pulse_stream import Machine


@pytest.fixture(scope='module')
def executable(tmp_path_factory):
    d=tmp_path_factory.mktemp('runtime')
    (d/'backend_mocks.h').write_text(MOCKS_H,encoding='utf-8')
    (d/'backend_harness.c').write_text(HARNESS_C,encoding='utf-8')
    for n in ['board_config.h','hardware/clocks.h','hardware/dma.h','hardware/gpio.h',
              'hardware/pio.h','hardware/timer.h','pico/platform.h','pico/types.h']:
        p=d/n;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text('#include "'+(d/'backend_mocks.h').as_posix()+'"\n',encoding='utf-8')
    cc=os.environ.get('HOST_CC') or shutil.which('gcc') or 'D:/Microsoft/mingw64/bin/gcc.exe'
    asm=os.environ.get('PIOASM') or shutil.which('pioasm') or str(
        Path.home()/'.pico-sdk/tools/2.2.0/pioasm/pioasm.exe')
    for program in ('sync_pulse_stream_out1','sync_pulse_uniform_out1'):
        assembled=d/(program+'.pio.h')
        built=subprocess.run([asm,'-o','c-sdk',str(ROOT/'components/sync_io/src'/(program+'.pio')),
                              str(assembled)],capture_output=True,text=True,timeout=30)
        assert built.returncode==0,built.stderr
    exe=d/('backend.exe' if os.name=='nt' else 'backend')
    before=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(d),
         '-I'+str(ROOT/'components/sync_io/inc'),'-I'+str(ROOT/'components/tdma/inc'),
         '-DRUN_SOURCE="'+SOURCE.as_posix()+'"',str(d/'backend_harness.c'),'-o',str(exe)]
    result=subprocess.run(cmd,capture_output=True,text=True,timeout=45)
    (d/'compile.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stderr
    assert before==hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'source changed during compile'
    (d/'tested-source.json').write_text(json.dumps({'source':str(SOURCE),'sha256':before,
        'compile_command':cmd,'scope':'actual source; mock hardware/persona/arena admission; no hardware operations'},indent=2)+'\n',encoding='utf-8')
    return exe


CASES=['gates','prepare_reserve','prepare_workspace','prepare_claim','prepare_load','prepare_arm','prepare_pio',
       'retirement','generation','async_stop','fault_starved','fault_pause','fault_source','fault_hz',
       'fault_divider','fault_expired','invalid_zero_model','invalid_ordinal','invalid_overlap',
       'invalid_short_high','invalid_late','invalid_too_far','fault_dma_ahb','fault_dma_read','fault_dma_write',
       'enable_start_failure','enable_cancel_final','enable_pc_not_started','enable_pc_past_guard',
       'enable_raw_backwards','enable_raw_too_wide','enable_raw_u64_max',
       'inactive_submit','inactive_release','enable_exact_guard','enable_inside_guard',
       'enable_observation_order','enable_observed_failure','enable_after_failure',
       'enable_both_failure','enable_observed_wrap','enable_after_wrap',
       'enable_observed_backwards_then_forward','enable_after_backwards',
       'enable_pc_upper_valid','enable_pc_below_program','enable_cancel_at_pc',
       'diagnostics','diagnostics_invalid_raw','diagnostics_saturation',
       'count_one','count_max','count_zero','count_over_max','count_null',
       'count_invalid_last_prepared','count_invalid_last_running','count_async_stop']
CASES += ['continuous_600', 'continuous_rollback', 'continuous_submit_rollback',
          'continuous_dma', 'continuous_clock', 'continuous_saturation']


@pytest.mark.parametrize('name',CASES)
def test_actual_backend_case(executable,name):
    result=subprocess.run([str(executable),name],capture_output=True,text=True,timeout=15)
    (executable.parent/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stdout+result.stderr


def test_real_backend_dma_fifo_words_execute_on_assembled_program(executable):
    result=subprocess.run([str(executable),'retirement'],capture_output=True,text=True,timeout=15)
    assert result.returncode==0,result.stderr
    words=list(map(int,result.stdout.split()))
    assert len(words)==16
    import re
    text=(executable.parent/'sync_pulse_stream_out1.pio.h').read_text(encoding='utf-8')
    body=text.split('sync_pulse_stream_out1_program_instructions[] = {')[1].split('};')[0]
    code=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{4}),',body)]
    m=Machine((None,code,0,7),origin=1000000,offset=12)
    m.submit_finite(words);m.run_edges(16,fast=True)
    expected=[]
    for first in (1100000,2100000):
        for i in range(4):expected.extend([(first+i*250000,1),(first+i*250000+1000,0)])
    assert m.edges==expected
    for _ in range(50):m.step()
    assert len(m.edges)==16 and m.pin==0 and not m.pending_dma


@pytest.mark.parametrize('name',['count_one','count_max'])
def test_counted_blocks_execute_contiguous_edges_on_real_pio(executable,name):
    # Real backend output is one pulse then sixteen, or sixteen then one.
    # This covers the CPU-only first pair, both DMA counts, and the boundary
    # between independently submitted finite sources on one raw tick axis.
    result=subprocess.run([str(executable),name],capture_output=True,text=True,timeout=15)
    assert result.returncode==0,result.stdout+result.stderr
    words=list(map(int,result.stdout.split()))
    assert len(words)==34
    import re
    text=(executable.parent/'sync_pulse_stream_out1.pio.h').read_text(encoding='utf-8')
    body=text.split('sync_pulse_stream_out1_program_instructions[] = {')[1].split('};')[0]
    code=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{4}),',body)]
    machine=Machine((None,code,0,7),origin=1000000,offset=12)
    machine.submit_finite(words);machine.run_edges(34,fast=True)
    expected=[]
    for i in range(17):
        rise=1100000+i*250000
        expected.extend([(rise,1),(rise+1000,0)])
    assert machine.edges==expected
    for _ in range(50):machine.step()
    assert machine.pin==0 and len(machine.edges)==34 and not machine.pending_dma


def test_acquire_and_state_rejection_precede_runtime_generation_access():
    # Ordinary scalar reads are invisible to peripheral mocks. Retain this
    # structural dominance check alongside the compiled forbidden-call cases:
    # a false result alone would not detect the old cross-owner data race.
    from test_vdc_command_owner import function_body
    source=SOURCE.read_text(encoding='utf-8')
    can=function_body(source,'sync_io_run_output_can_submit_core1')
    compact=''.join(can.split())
    acquire=compact.index('__atomic_load_n(&s_state,__ATOMIC_ACQUIRE)')
    prepared=compact.index('state!=SYNC_IO_RUN_OUTPUT_PREPARED')
    running=compact.index('state!=SYNC_IO_RUN_OUTPUT_RUNNING')
    lease=compact.index('sync_io_core_run_output_held(&s_run)')
    generation=compact.index('generation!=s_run.generation')
    assert acquire < prepared < running < lease < generation
    assert '&&' in compact[prepared:running]
    assert '||' in compact[running:lease]
    release=''.join(function_body(source,'sync_io_run_output_release').split())
    assert release.index('__atomic_load_n(&s_state,__ATOMIC_ACQUIRE)!=SYNC_IO_RUN_OUTPUT_RETIRED') < release.index('generation!=s_run.generation')
    assert release.index('generation!=s_run.generation') < release.index('sync_io_persona_manager_release')


def test_first_low_reencode_uses_the_shared_pio_cycle_constant():
    from test_vdc_command_owner import function_body
    body=''.join(function_body(SOURCE.read_text(encoding='utf-8'),
                              'sync_io_run_output_submit_count_core1').split())
    # Cover both range admission and final FIFO word. A repeated literal
    # silently drifts if PIO timing is revised while the encoder is updated.
    assert 'first_low_overhead=uniform?SYNC_PULSE_UNIFORM_FIRST_LOW_OVERHEAD:SYNC_PULSE_STREAM_FIRST_LOW_OVERHEAD' in body
    assert 'edges[0].rising_tick-before-first_low_overhead>UINT32_MAX' in body
    assert 'pio_sm_put(RUN_PIO,RUN_SM,(uint32_t)(edges[0].rising_tick-before-first_low_overhead))' in body


MOCKS_H = r'''
#ifndef BACKEND_MOCKS_H
#define BACKEND_MOCKS_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "sync_io_persona_manager.h"
typedef unsigned uint;
#define __not_in_flash_func(n) n
#define PICO_PIO_VERSION 0
#define PIO_FIFO_JOIN_TX 1u
#define PIO_FDEBUG_TXSTALL_LSB 24u
#define DMA_CH0_CTRL_TRIG_EN_BITS 1u
#define DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS (1u<<31)
#define DMA_CH0_CTRL_TRIG_READ_ERROR_BITS (1u<<30)
#define DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS (1u<<29)
#define DMA_SIZE_32 2u
#define DMA_IRQ_0 0
#define GPIO_OUT 1
#define GPIO_FUNC_SIO 5
#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 1u
#define clk_sys 0u
#define BOARD_SYS_CLOCK_HZ 250000000u
#define BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM 1u
#define BOARD_SYNC_OUTPUT_BASE_PIN 16u
typedef struct {uint32_t clkdiv,addr;} mock_sm_t;
typedef struct {uint32_t ctrl,fdebug,fstat,txf[4];mock_sm_t sm[4];} mock_pio_t;
typedef mock_pio_t *PIO;
typedef struct {uint32_t pause,source,timerawh,timerawl;} mock_timer_t;
typedef struct {uint32_t transfer_count,al1_ctrl,ctrl_trig;} mock_dma_ch_t;
typedef struct {mock_dma_ch_t ch[16];uint32_t abort;} mock_dma_t;
typedef struct {uint lo,hi,pin,count,shift,autopull,threshold,join,div,frac;} pio_sm_config;
typedef struct {uint size,dreq;bool read_inc,write_inc;} dma_channel_config;
struct pio_program {const uint16_t *instructions;uint length;int origin;uint pio_version;};
static mock_pio_t hw_pio;
static mock_timer_t hw_timer;
static mock_dma_t hw_dma;
#define BOARD_SYNC_PIO_FAST (&hw_pio)
#define timer1_hw mock_timer_hw()
#define dma_hw (&hw_dma)
static uint mock_core, mock_hz=BOARD_SYS_CLOCK_HZ;
static bool mock_sm_claim, mock_dma_claim, mock_loaded, mock_busy;
static bool deny_reserve, deny_workspace, deny_claim, deny_load, deny_arm, deny_add, deny_init;
static const void *core_token, *workspace_token;
static unsigned core_releases,workspace_releases,clears,unclaims,starts;
static uint32_t fifo[32];
static unsigned fifo_count;
static uint32_t *dma_read;
static const uint32_t *last_source;
static uint32_t pin_latch=UINT32_MAX;
static unsigned io_calls,enable_calls;
static uint mock_pc=13;
static bool deny_start,cancel_at_start;
static uint64_t raw_after_enable;
static uint64_t raw_at_pc;
static bool probe_active, probe_after_pc, pause_observed, pause_after;
static bool wrap_observed, wrap_after;
static bool cancel_at_pc;
static unsigned observed_accesses, after_accesses, pc_reads;
static bool forbid_held_query;
static unsigned held_queries;
static sync_io_persona_manager_hooks_t saved_hooks;
static void *saved_context;
static bool manager_valid;
static bool use_uniform;
static uint32_t mock_isr, mock_osr;
static uint loaded_length;
static const sync_io_persona_descriptor_t descriptor={.id=SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER};
void sync_io_run_output_cancel(void);

static inline uint get_core_num(void) {return mock_core;}
static inline uint32_t clock_get_hz(uint c) {(void)c;return mock_hz;}
static inline void hw_clear_bits(uint32_t *p,uint32_t mask) {*p&=~mask;++io_calls;}
static inline void raw(uint64_t ticks) {hw_timer.timerawl=(uint32_t)ticks;hw_timer.timerawh=(uint32_t)(ticks>>32);}
/* Instrument actual production MMIO accesses, not a replacement read_raw.
 * A normal observation reads pause/source/high/low/high. Changing high on
 * its last access reproduces a torn rollover observation. */
static inline mock_timer_t *mock_timer_hw(void) {
    if(probe_active) {
        unsigned *count=probe_after_pc?&after_accesses:&observed_accesses;
        ++*count;
        if(*count==5u && (probe_after_pc?wrap_after:wrap_observed))++hw_timer.timerawh;
    }
    return &hw_timer;
}
static inline pio_sm_config pio_get_default_sm_config(void) {pio_sm_config c={0};return c;}
static inline void sm_config_set_wrap(pio_sm_config *c,uint l,uint h) {c->lo=l;c->hi=h;}
static inline void sm_config_set_set_pins(pio_sm_config *c,uint p,uint n) {c->pin=p;c->count=n;}
static inline void sm_config_set_out_shift(pio_sm_config *c,bool r,bool a,uint n) {c->shift=r;c->autopull=a;c->threshold=n;}
static inline void sm_config_set_fifo_join(pio_sm_config *c,uint n) {c->join=n;}
static inline void sm_config_set_clkdiv_int_frac(pio_sm_config *c,uint d,uint f) {c->div=d;c->frac=f;}
static inline void pio_sm_set_enabled(PIO p,uint sm,bool e) {
    ++io_calls;
    if(e){p->ctrl|=1u<<sm;++enable_calls;if(raw_after_enable)raw(raw_after_enable);
        probe_active=true;probe_after_pc=false;observed_accesses=after_accesses=pc_reads=0;
        hw_timer.pause=pause_observed;
    }else p->ctrl&=~(1u<<sm);
}
static inline int pio_sm_init(PIO p,uint sm,uint pc,const pio_sm_config *c) {
    assert(!(p->ctrl&(1u<<sm))&&pc==c->lo&&c->hi==pc+loaded_length-1u);
    assert(c->join==PIO_FIFO_JOIN_TX&&!c->autopull&&c->threshold==32u&&c->shift);
    assert(c->div==1u&&!c->frac);++io_calls;p->sm[sm].clkdiv=c->div<<16|c->frac;return deny_init?-1:0;
}
static inline void pio_sm_clear_fifos(PIO p,uint sm) {(void)p;(void)sm;fifo_count=0;++clears;++io_calls;}
static inline void pio_sm_restart(PIO p,uint sm) {(void)p;(void)sm;++io_calls;}
static inline void pio_sm_clkdiv_restart(PIO p,uint sm) {(void)p;(void)sm;++io_calls;}
static inline void pio_sm_set_pins_with_mask(PIO p,uint sm,uint v,uint m) {(void)p;(void)sm;pin_latch=(pin_latch&~m)|(v&m);++io_calls;}
static inline void pio_sm_set_consecutive_pindirs(PIO p,uint sm,uint pin,uint n,bool out) {(void)p;(void)sm;(void)pin;(void)n;(void)out;++io_calls;}
static inline void pio_gpio_init(PIO p,uint pin) {(void)p;(void)pin;++io_calls;}
static inline bool pio_sm_is_claimed(PIO p,uint sm) {(void)p;(void)sm;return mock_sm_claim;}
static inline void pio_sm_claim(PIO p,uint sm) {(void)p;(void)sm;assert(!mock_sm_claim);mock_sm_claim=true;++io_calls;}
static inline void pio_sm_unclaim(PIO p,uint sm) {(void)p;(void)sm;assert(mock_sm_claim);mock_sm_claim=false;++unclaims;++io_calls;}
static inline bool pio_can_add_program(PIO p,const struct pio_program *program) {(void)p;assert(program->length==7||program->length==8);return !deny_add;}
static inline uint pio_add_program(PIO p,const struct pio_program *program) {(void)p;loaded_length=program->length;mock_loaded=true;++io_calls;return 12;}
static inline void pio_remove_program(PIO p,const struct pio_program *program,uint offset) {(void)p;assert(offset==12&&program->length==loaded_length);mock_loaded=false;++io_calls;}
static inline uint pio_get_dreq(PIO p,uint sm,bool tx) {(void)p;assert(sm==1&&tx);return 9;}
static inline void pio_sm_put(PIO p,uint sm,uint32_t value) {
    assert(sm==1&&fifo_count<8);fifo[fifo_count++]=value;++io_calls;
    /* CPU wrote FDEBUG W1C immediately before first preload. Complete that
     * register effect at the next hardware call; a plain C field is not W1C. */
    p->fdebug=0;
}
enum { pio_isr=6u, pio_osr=7u };
static inline uint pio_encode_pull(bool ifempty,bool block) {assert(!ifempty&&block);return 0x80a0u;}
static inline uint pio_encode_mov(uint dest,uint src) {assert(dest==pio_isr&&src==pio_osr);return 0xa0c7u;}
static inline void pio_sm_exec(PIO p,uint sm,uint instruction) {
    assert(sm==1u&&!(p->ctrl&(1u<<sm))&&loaded_length==7u);++io_calls;
    if(instruction==0x80a0u) {
        assert(fifo_count==1u);mock_osr=fifo[0];fifo_count=0;
    } else {assert(instruction==0xa0c7u&&!fifo_count);mock_isr=mock_osr;}
}
static inline uint pio_sm_get_pc(PIO p,uint sm) {
    (void)p;assert(sm==1);
    /* An immediate PC-before-observation regression must fail even if the
     * mocked PC is valid. Also enforce exactly one, non-polling PC sample. */
    assert(probe_active && !probe_after_pc && !pc_reads);
    assert(observed_accesses==(pause_observed?1u:5u));
    ++pc_reads;probe_after_pc=true;hw_timer.pause=pause_after;
    if(raw_at_pc)raw(raw_at_pc);
    if(cancel_at_pc)sync_io_run_output_cancel();
    return mock_pc;
}
static inline void gpio_put(uint pin,bool v) {assert(pin==16);if(v)pin_latch|=1u<<pin;else pin_latch&=~(1u<<pin);++io_calls;}
static inline void gpio_set_dir(uint pin,uint out) {(void)pin;(void)out;++io_calls;}
static inline void gpio_set_function(uint pin,uint f) {(void)pin;(void)f;++io_calls;}
static inline bool dma_channel_is_claimed(uint ch) {assert(ch==2);return mock_dma_claim;}
static inline void dma_channel_claim(uint ch) {assert(ch==2&&!mock_dma_claim);mock_dma_claim=true;++io_calls;}
static inline void dma_channel_unclaim(uint ch) {assert(ch==2&&!mock_busy&&!(hw_dma.abort&(1u<<ch)));mock_dma_claim=false;++unclaims;++io_calls;}
static inline bool dma_channel_is_busy(uint ch) {assert(ch==2);return mock_busy;}
static inline void dma_channel_set_irq0_enabled(uint ch,bool on) {assert(ch==2&&!on);++io_calls;}
static inline void dma_channel_set_irq1_enabled(uint ch,bool on) {assert(ch==2&&!on);++io_calls;}
static inline dma_channel_config dma_channel_get_default_config(uint ch) {assert(ch==2);dma_channel_config c={0};return c;}
static inline void channel_config_set_transfer_data_size(dma_channel_config *c,uint size) {c->size=size;}
static inline void channel_config_set_read_increment(dma_channel_config *c,bool v) {c->read_inc=v;}
static inline void channel_config_set_write_increment(dma_channel_config *c,bool v) {c->write_inc=v;}
static inline void channel_config_set_dreq(dma_channel_config *c,uint dreq) {c->dreq=dreq;}
static inline void dma_channel_configure(uint ch,const dma_channel_config *c,void *to,const void *from,uint count,bool start) {
    assert(ch==2&&c->size==DMA_SIZE_32&&c->read_inc&&!c->write_inc&&c->dreq==9);
    assert(to==&hw_pio.txf[1]&&!count&&!start);dma_read=(uint32_t *)from;hw_dma.ch[ch].transfer_count=count;++io_calls;
}
static inline void dma_channel_set_read_addr(uint ch,const void *p,bool trigger) {
    assert(ch==2&&!mock_busy&&!hw_dma.ch[ch].transfer_count&&!trigger);
    dma_read=(uint32_t *)p;last_source=p;++io_calls;
}
static inline void dma_channel_set_trans_count(uint ch,uint n,bool trigger) {
    assert(ch==2&&!mock_busy&&trigger&&n>0);hw_dma.ch[ch].transfer_count=n;mock_busy=true;hw_dma.ch[ch].al1_ctrl|=1u;++io_calls;
}
static inline void dma_drain_to_fifo(void) {
    while(mock_busy&&hw_dma.ch[2].transfer_count&&fifo_count<8) {
        fifo[fifo_count++]=*dma_read++;--hw_dma.ch[2].transfer_count;
    }
    if(!hw_dma.ch[2].transfer_count)mock_busy=false;
}
static inline void fifo_consume(unsigned n) {assert(n<=fifo_count);memmove(fifo,fifo+n,(fifo_count-n)*4u);fifo_count-=n;}

bool sync_io_core_run_output_reserve(const void *token) {if(deny_reserve||core_token)return false;core_token=token;return true;}
bool sync_io_core_run_output_held(const void *token) {assert(!forbid_held_query);++held_queries;return core_token==token;}
bool sync_io_core_run_output_release(const void *token) {assert(core_token==token);core_token=NULL;++core_releases;return true;}
bool sync_io_workspace_claim(const void *token) {if(deny_workspace||workspace_token)return false;workspace_token=token;return true;}
bool sync_io_workspace_held_by(const void *token) {return workspace_token==token;}
bool sync_io_workspace_release(const void *token) {assert(workspace_token==token);workspace_token=NULL;++workspace_releases;return true;}
void sync_io_persona_manager_init(sync_io_persona_manager_t *m,const sync_io_persona_manager_hooks_t *h,void *ctx) {(void)m;saved_hooks=*h;saved_context=ctx;manager_valid=false;}
bool sync_io_persona_manager_claim(sync_io_persona_manager_t *m,sync_io_persona_id_t id,sync_io_persona_manager_handle_t *h,sync_io_persona_compatibility_t *c) {(void)m;(void)c;assert(id==SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER);if(deny_claim)return false;manager_valid=true;h->generation=1;return true;}
bool sync_io_persona_manager_load(sync_io_persona_manager_t *m,sync_io_persona_manager_handle_t *h) {(void)m;(void)h;return !deny_load&&saved_hooks.load(saved_context,&descriptor,1u<<2);}
bool sync_io_persona_manager_arm(sync_io_persona_manager_t *m,sync_io_persona_manager_handle_t *h) {(void)m;(void)h;return !deny_arm&&saved_hooks.arm(saved_context,&descriptor,1u<<2);}
bool sync_io_persona_manager_start(sync_io_persona_manager_t *m,sync_io_persona_manager_handle_t *h) {(void)m;(void)h;++starts;if(cancel_at_start)sync_io_run_output_cancel();return !deny_start;}
bool sync_io_persona_manager_handle_valid(const sync_io_persona_manager_t *m,const sync_io_persona_manager_handle_t *h) {(void)m;(void)h;return manager_valid;}
bool sync_io_persona_manager_release(sync_io_persona_manager_t *m,sync_io_persona_manager_handle_t *h) {(void)m;(void)h;assert(manager_valid);saved_hooks.cleanup(saved_context,&descriptor,1u<<2);manager_valid=false;return true;}

#endif
'''

HARNESS_C = r'''
#include "backend_mocks.h"
#include RUN_SOURCE
uint32_t sync_io_shared_workspace[SYNC_IO_SHARED_WORKSPACE_WORDS];

static sync_io_run_output_snapshot_t snap(void)
{
    sync_io_run_output_snapshot_t s;
    assert(sync_io_run_output_snapshot(&s));
    return s;
}
static uint32_t prepare(void)
{
    mock_core=0;hw_timer.source=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;raw(1000000);
    unsigned enables_before=enable_calls;
    uint32_t g=0;
    assert(use_uniform ? sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,1000,1000,&g) :
                         sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,1000,&g));
    assert(g&&snap().state==SYNC_IO_RUN_OUTPUT_PREPARED);
    assert(core_token&&workspace_token&&mock_loaded&&mock_sm_claim&&mock_dma_claim);
    assert(!hw_pio.ctrl&&!(pin_latch&(1u<<16)));
    assert(!fifo_count&&!mock_busy&&enable_calls==enables_before);
    assert(snap().fifo_words_per_edge==(use_uniform?1u:2u));
    assert(snap().fixed_high_ticks==(use_uniform?1000u:0u));
    if(use_uniform)assert(mock_isr==998u&&loaded_length==7u);
    return g;
}
static void make_edges(sync_io_run_output_edge_t e[4],uint64_t first,uint64_t ordinal,uint32_t token)
{
    for(unsigned i=0;i<4;++i)e[i]=(sync_io_run_output_edge_t){first+i*250000u,first+i*250000u+1000u,ordinal+i,token};
}
static void make_counted_edges(sync_io_run_output_edge_t *edges,unsigned count,
        uint64_t first,uint64_t ordinal,uint32_t token)
{
    for(unsigned i=0;i<count;++i)
        edges[i]=(sync_io_run_output_edge_t){first+i*250000u,first+i*250000u+1000u,ordinal+i,token};
}
static uint32_t start(sync_io_run_output_edge_t edges[4])
{
    uint32_t g=prepare();make_edges(edges,1100000,0,7);mock_core=1;
    assert(sync_io_run_output_submit_core1(g,edges));
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RUNNING&&snap().blocks==1&&snap().edges==4);
    assert(snap().anchor_before==1000000&&snap().anchor_after==1000001);
    assert(snap().start_pc==13u&&snap().program_offset==12u&&snap().start_raw_flags==3u);
    assert(snap().start_raw_observed==1000000u&&snap().start_raw_after==1000000u);
    assert(observed_accesses==5u&&after_accesses==5u&&pc_reads==1u);probe_active=false;
    assert(snap().last_falling_tick==1851000&&mock_busy&&fifo_count==(use_uniform?1u:2u));
    assert(hw_dma.ch[2].transfer_count==(use_uniform?3u:6u)&&last_source==sync_io_shared_workspace+(use_uniform?1u:2u));
    return g;
}
static void complete_stop(uint32_t g)
{
    mock_core=1;sync_io_run_output_cancel();sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING);
    hw_dma.abort=0;mock_busy=false;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRED);
    mock_core=0;assert(sync_io_run_output_release(g));
    assert(!workspace_token&&!core_token&&!mock_loaded&&!mock_sm_claim&&!mock_dma_claim);
    assert(snap().state==SYNC_IO_RUN_OUTPUT_IDLE);
}
static void gates(void)
{
    uint32_t g=0x12345678;
    mock_core=1;assert(!sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,1000,&g));
    mock_core=0;
    assert(!sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,20001,&g));
    assert(!sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ-1,1000,&g));
    assert(!sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,1000,NULL));
    mock_hz--;assert(!sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,1000,&g));mock_hz++;
    assert(!io_calls&&!core_token&&!workspace_token&&g==0x12345678);
    g=prepare();uint32_t again=0xabcdef;
    assert(!sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,1000,&again)&&again==0xabcdef);
    assert(!sync_io_run_output_release(g));
    unsigned before=io_calls;sync_io_run_output_cancel();assert(io_calls==before);
    sync_io_run_output_service_core1();assert(snap().state==SYNC_IO_RUN_OUTPUT_PREPARED);
    complete_stop(g);
}
static void prepare_failure(const char *what)
{
    if(!strcmp(what,"reserve"))deny_reserve=true;
    if(!strcmp(what,"workspace"))deny_workspace=true;
    if(!strcmp(what,"claim"))deny_claim=true;
    if(!strcmp(what,"load"))deny_load=true;
    if(!strcmp(what,"arm"))deny_arm=true;
    if(!strcmp(what,"pio"))deny_add=true;
    if(!strcmp(what,"pio_init"))deny_init=true;
    uint32_t g=0x4567;
    assert(!(use_uniform ? sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,1000,1000u,&g) :
                          sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,1000,&g)));
    assert(g==0x4567&&!core_token&&!workspace_token&&!mock_sm_claim&&!mock_dma_claim&&!mock_loaded);
    assert(snap().state==SYNC_IO_RUN_OUTPUT_IDLE&&!enable_calls);
    if(use_uniform) {
        assert(!fifo_count&&mock_isr==0u);
        deny_reserve=deny_workspace=deny_claim=deny_load=deny_arm=deny_add=deny_init=false;
        use_uniform=false;g=prepare();assert(loaded_length==8u);complete_stop(g);
    }
}
static void retirement(void)
{
    sync_io_run_output_edge_t edges[4],next[4];uint32_t g=start(edges);
    make_edges(next,2100000,4,9);
    uint32_t old[8];memcpy(old,sync_io_shared_workspace,sizeof(old));
    assert(!sync_io_run_output_can_submit_core1(g));
    assert(!sync_io_run_output_submit_core1(g,next));
    assert(!memcmp(old,sync_io_shared_workspace,sizeof(old)));
    dma_drain_to_fifo();assert(fifo_count==8&&!mock_busy&&!hw_dma.ch[2].transfer_count);
    uint32_t queued[8];memcpy(queued,fifo,sizeof(queued));
    mock_busy=true;
    assert(!sync_io_run_output_can_submit_core1(g));
    assert(!sync_io_run_output_submit_core1(g,next));
    mock_busy=false;hw_dma.ch[2].transfer_count=1;
    assert(!sync_io_run_output_can_submit_core1(g));
    assert(!sync_io_run_output_submit_core1(g,next));
    hw_dma.ch[2].transfer_count=0;
    assert(sync_io_run_output_can_submit_core1(g));
    assert(sync_io_run_output_submit_core1(g,next));
    assert(snap().source_retirements==1&&snap().blocks==2&&snap().edges==8);
    assert(snap().first_model==7&&snap().last_model==9&&snap().model_changes==1);
    assert(!memcmp(queued,fifo,sizeof(queued))&&hw_dma.ch[2].transfer_count==8);
    for(unsigned i=0;i<8;++i)printf("%u ",queued[i]);
    fifo_consume(8);dma_drain_to_fifo();assert(fifo_count==8);
    for(unsigned i=0;i<8;++i)printf("%u ",fifo[i]);
    printf("\n");
    assert(!sync_io_run_output_submit_core1(g,next)); /* duplicate ordinal */
    complete_stop(g);
}
static void stale_generation(void)
{
    sync_io_run_output_edge_t edges[4];uint32_t g=start(edges);
    assert(!sync_io_run_output_can_submit_core1(g+1));
    assert(!sync_io_run_output_submit_core1(g+1,edges));
    mock_core=0;assert(!sync_io_run_output_release(g+1));
    complete_stop(g);
    uint32_t g2=prepare();assert(g2>g);
    mock_core=1;make_edges(edges,1100000,0,7);
    assert(!sync_io_run_output_submit_core1(g,edges));
    assert(sync_io_run_output_submit_core1(g2,edges));
    complete_stop(g2);
}
static void async_stop(void)
{
    sync_io_run_output_edge_t edges[4];uint32_t g=start(edges);
    pin_latch|=1u<<16;unsigned before=io_calls,clear_before=clears;
    mock_core=0;sync_io_run_output_cancel();assert(before==io_calls);
    assert(!sync_io_run_output_release(g));
    mock_core=1;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&snap().reason==SYNC_IO_RUN_OUTPUT_CANCELLED);
    assert(!hw_pio.ctrl&&!(pin_latch&(1u<<16))&&hw_dma.abort==(1u<<2));
    assert(core_token&&workspace_token&&!core_releases&&!workspace_releases&&!unclaims);
    assert(clears==clear_before&&fifo_count==(use_uniform?1u:2u));
    mock_core=0;assert(!sync_io_run_output_release(g));
    mock_core=1;mock_busy=false;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&clears==clear_before);
    hw_dma.abort=0;mock_busy=true;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&workspace_token);
    mock_busy=false;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRED&&!fifo_count&&workspace_token);
    mock_core=0;assert(sync_io_run_output_release(g));
    assert(!core_token&&!workspace_token&&workspace_releases==1&&core_releases==1);
}
static void fault(const char *name)
{
    sync_io_run_output_edge_t edges[4];uint32_t g=start(edges);
    dma_drain_to_fifo();fifo_consume(use_uniform?4u:8u);
    unsigned expect=SYNC_IO_RUN_OUTPUT_CLOCK;
    if(!strcmp(name,"starved")){hw_pio.fdebug=1u<<(24+1);expect=SYNC_IO_RUN_OUTPUT_STARVED;}
    else if(!strcmp(name,"pause"))hw_timer.pause=1;
    else if(!strcmp(name,"source"))hw_timer.source=0;
    else if(!strcmp(name,"hz"))--mock_hz;
    else if(!strcmp(name,"divider"))hw_pio.sm[1].clkdiv=2u<<16;
    else if(!strcmp(name,"expired")){raw(snap().expires_tick);expect=SYNC_IO_RUN_OUTPUT_EXPIRED;}
    else if(!strcmp(name,"dma_ahb")){hw_dma.ch[2].ctrl_trig=DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS;expect=SYNC_IO_RUN_OUTPUT_DMA;}
    else if(!strcmp(name,"dma_read")){hw_dma.ch[2].ctrl_trig=DMA_CH0_CTRL_TRIG_READ_ERROR_BITS;expect=SYNC_IO_RUN_OUTPUT_DMA;}
    else if(!strcmp(name,"dma_write")){hw_dma.ch[2].ctrl_trig=DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;expect=SYNC_IO_RUN_OUTPUT_DMA;}
    else assert(false);
    unsigned enables=enable_calls;
    sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&snap().reason==expect);
    assert(!hw_pio.ctrl&&!(pin_latch&(1u<<16)));
    assert(!sync_io_run_output_can_submit_core1(g)&&!sync_io_run_output_submit_core1(g,edges));
    hw_dma.abort=0;mock_busy=false;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRED);
    assert(!sync_io_run_output_submit_core1(g,edges)&&enable_calls==enables);
    mock_core=0;assert(sync_io_run_output_release(g));
}
static void enable_boundary(const char *name)
{
    uint32_t g=prepare();sync_io_run_output_edge_t e[4];make_edges(e,1100000,0,7);mock_core=1;
    bool admitted=true,retiring=true;
    if(!strcmp(name,"start_failure")){deny_start=true;admitted=false;}
    else if(!strcmp(name,"cancel_final")){cancel_at_start=true;admitted=false;}
    else if(!strcmp(name,"pc_not_started"))mock_pc=12;
    else if(!strcmp(name,"pc_past_guard"))mock_pc=17;
    else if(!strcmp(name,"raw_backwards"))raw_after_enable=999999;
    else if(!strcmp(name,"raw_too_wide"))raw_after_enable=1025001;
    else if(!strcmp(name,"raw_u64_max"))raw_after_enable=UINT64_MAX;
    else if(!strcmp(name,"exact_guard"))raw_after_enable=1025000;
    else if(!strcmp(name,"inside_guard")){raw_after_enable=1024999;retiring=false;}
    else if(!strcmp(name,"observation_order")){raw_after_enable=1000007;raw_at_pc=1000011;retiring=false;}
    else if(!strcmp(name,"observed_failure")){pause_observed=true;raw_at_pc=1000008;}
    else if(!strcmp(name,"after_failure"))pause_after=true;
    else if(!strcmp(name,"both_failure")){pause_observed=pause_after=true;}
    else if(!strcmp(name,"observed_wrap")){wrap_observed=true;raw_at_pc=1000008;}
    else if(!strcmp(name,"after_wrap"))wrap_after=true;
    else if(!strcmp(name,"observed_backwards_then_forward")){raw_after_enable=999999;raw_at_pc=1000008;}
    else if(!strcmp(name,"after_backwards")){raw_after_enable=1000008;raw_at_pc=1000007;}
    else if(!strcmp(name,"pc_upper_valid")){mock_pc=use_uniform?15u:16u;retiring=false;}
    else if(!strcmp(name,"pc_first_high")){assert(use_uniform);mock_pc=16u;}
    else if(!strcmp(name,"pc_below_program"))mock_pc=11;
    else if(!strcmp(name,"cancel_at_pc")){cancel_at_pc=true;retiring=false;}
    else assert(false);
    assert(sync_io_run_output_submit_core1(g,e)==admitted);
    assert(snap().state==(retiring?SYNC_IO_RUN_OUTPUT_RETIRING:SYNC_IO_RUN_OUTPUT_RUNNING));
    assert(snap().blocks==(admitted?1u:0u)&&snap().edges==(admitted?4u:0u));
    assert(enable_calls==(admitted?1u:0u));
    assert(workspace_token&&core_token);
    if(admitted) {
        assert(observed_accesses==(pause_observed?1u:5u));
        assert(after_accesses==(pause_after?1u:5u)*(retiring?2u:1u)&&pc_reads==1u);
        assert(snap().start_pc==mock_pc&&snap().program_offset==12u);
        const unsigned flags=(!pause_observed&&!wrap_observed?1u:0u)|
                             (!pause_after&&!wrap_after?2u:0u);
        assert(snap().start_raw_flags==flags);
        const uint64_t observed=raw_after_enable?raw_after_enable:1000000u;
        const uint64_t after=raw_at_pc?raw_at_pc:observed;
        assert(snap().start_raw_observed==((flags&1u)?observed:0u));
        assert(snap().start_raw_after==((flags&2u)?after:0u));
        const bool invalid_raw=flags!=3u || !strcmp(name,"raw_backwards") ||
            !strcmp(name,"raw_u64_max") || !strcmp(name,"observed_backwards_then_forward") ||
            !strcmp(name,"after_backwards");
        const uint64_t end=raw_at_pc?raw_at_pc:(raw_after_enable?raw_after_enable:1000000u);
        assert(snap().anchor_before==1000000u);
        assert(snap().anchor_after==(invalid_raw?UINT64_MAX:end+1u));
        if(retiring)assert(snap().reason==SYNC_IO_RUN_OUTPUT_CLOCK);
    } else {
        /* Pre-enable cancel/start rejection must not reach either new raw
         * observation, PC sampling or publication of an admitted block. */
        assert(!observed_accesses&&!after_accesses&&!pc_reads&&!snap().start_raw_flags);
    }
    probe_active=false;
    if(!retiring) {
        assert(snap().anchor_after-snap().anchor_before<=25000u);
        assert(hw_pio.ctrl==(1u<<1));
        if(cancel_at_pc) {
            sync_io_run_output_service_core1();
            assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&snap().reason==SYNC_IO_RUN_OUTPUT_CANCELLED);
        }
        complete_stop(g);return;
    }
    assert(!hw_pio.ctrl&&!(pin_latch&(1u<<16)));
    assert(!sync_io_run_output_submit_core1(g,e));
    hw_dma.abort=0;mock_busy=false;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRED);
    mock_core=0;assert(sync_io_run_output_release(g));
}
static void diagnostics(void)
{
    sync_io_run_output_edge_t edges[4],next[4];uint32_t g=start(edges);
    assert(snap().submit_last_tick==1000000u&&!snap().service_observations);
    raw(1010000);sync_io_run_output_service_core1();
    raw(1060000);sync_io_run_output_service_core1();
    raw(1070000);sync_io_run_output_service_core1();
    assert(snap().service_observations==3u&&snap().service_last_gap_ticks==10000u);
    assert(snap().service_max_gap_ticks==50000u);
    dma_drain_to_fifo();make_edges(next,2100000,4,9);
    assert(sync_io_run_output_submit_core1(g,next));
    assert(snap().submit_last_tick==1070000u&&snap().submit_max_gap_ticks==70000u);
    assert(snap().submit_service_observation==3u&&snap().refill_min_margin_ticks==781000u);
    const uint64_t last=snap().last_falling_tick;
    hw_pio.sm[1].addr=12u;hw_pio.fstat=0x1234u;hw_pio.fdebug=1u<<25;
    hw_dma.ch[2].ctrl_trig=0x2345u;raw(3000000);
    sync_io_run_output_service_core1();
    sync_io_run_output_snapshot_t fault=snap();
    assert(fault.reason==SYNC_IO_RUN_OUTPUT_STARVED&&fault.retire_raw_valid);
    assert(fault.retire_raw_tick==3000000u&&fault.retire_pc==12u&&fault.retire_fstat==0x1234u);
    assert(fault.retire_fdebug==(1u<<25)&&fault.retire_pio_ctrl==(1u<<1));
    assert(fault.retire_dma_ctrl==0x2345u&&fault.retire_dma_remaining==8u);
    assert(fault.last_falling_tick==last&&fault.service_last_gap_ticks==1930000u);
    assert(fault.service_observations-fault.submit_service_observation==1u);
    hw_pio.fdebug=0;hw_pio.fstat=0;raw(4000000);complete_stop(g);
    assert(snap().retire_raw_tick==fault.retire_raw_tick&&snap().retire_fstat==fault.retire_fstat);
    assert(snap().reason==SYNC_IO_RUN_OUTPUT_STARVED);
    uint32_t next_generation=prepare();
    assert(!snap().retire_raw_valid&&!snap().retire_raw_tick&&!snap().service_observations);
    assert(!snap().service_max_gap_ticks&&!snap().submit_last_tick&&!snap().refill_min_margin_ticks);
    complete_stop(next_generation);
}
static void diagnostics_invalid_raw(void)
{
    sync_io_run_output_edge_t e[4];uint32_t g=start(e);
    hw_timer.pause=1u;sync_io_run_output_service_core1();
    assert(snap().reason==SYNC_IO_RUN_OUTPUT_CLOCK);
    assert(!snap().retire_raw_valid&&!snap().retire_raw_tick&&!snap().service_observations);
    hw_timer.pause=0u;complete_stop(g);
    assert(!snap().retire_raw_valid&&!snap().retire_raw_tick);
}
static void diagnostics_saturation(void)
{
    sync_io_run_output_edge_t e[4];uint32_t g=start(e);
    s_run.service_observations=UINT32_MAX;s_run.service_last_tick=1000000u;
    raw(1000007u);sync_io_run_output_service_core1();
    assert(snap().service_observations==UINT32_MAX&&snap().service_last_gap_ticks==7u);
    complete_stop(g);
}
static void inactive_submit(void)
{
    /* Model the state before Core0 publishes PREPARED and after Core1 gives
     * it back. The poisoned payload belongs to another owner, regardless of
     * a coincidentally matching generation supplied by the caller. */
    static const unsigned states[]={SYNC_IO_RUN_OUTPUT_IDLE,SYNC_IO_RUN_OUTPUT_RETIRING,SYNC_IO_RUN_OUTPUT_RETIRED};
    sync_io_run_output_edge_t e[4];make_edges(e,1100000,0,7);
    mock_core=1;forbid_held_query=true;
    for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i) {
        memset(&s_run,0xa5,sizeof(s_run));
        __atomic_store_n(&s_state,states[i],__ATOMIC_RELEASE);
        assert(!sync_io_run_output_can_submit_core1(0xa5a5a5a5u));
        assert(!sync_io_run_output_submit_core1(0xa5a5a5a5u,e));
        sync_io_run_output_snapshot_t frozen=s_run;
        assert(!sync_io_run_output_can_submit_core1(0));
        assert(!memcmp(&frozen,&s_run,sizeof(s_run))&&!held_queries&&!io_calls);
    }
}
static void inactive_release(void)
{
    static const unsigned states[]={SYNC_IO_RUN_OUTPUT_IDLE,SYNC_IO_RUN_OUTPUT_PREPARED,SYNC_IO_RUN_OUTPUT_RUNNING,SYNC_IO_RUN_OUTPUT_RETIRING};
    mock_core=0;
    for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i) {
        memset(&s_run,0xa5,sizeof(s_run));
        __atomic_store_n(&s_state,states[i],__ATOMIC_RELEASE);
        sync_io_run_output_snapshot_t frozen=s_run;
        assert(!sync_io_run_output_release(0xa5a5a5a5u));
        assert(!sync_io_run_output_release(0));
        assert(!memcmp(&frozen,&s_run,sizeof(s_run))&&!core_releases&&!workspace_releases&&!io_calls);
    }
}
static void invalid_edges(const char *name)
{
    uint32_t g=prepare();sync_io_run_output_edge_t e[4];make_edges(e,1100000,0,7);mock_core=1;
    if(!strcmp(name,"zero_model"))e[2].model_token=0;
    if(!strcmp(name,"ordinal"))e[2].ordinal=e[1].ordinal;
    if(!strcmp(name,"overlap"))e[1].rising_tick=e[0].falling_tick+4;
    if(!strcmp(name,"short_high"))e[0].falling_tick=e[0].rising_tick+1;
    if(!strcmp(name,"late"))e[0].rising_tick=1000001;
    if(!strcmp(name,"too_far")){e[3].rising_tick=UINT64_MAX-1000;e[3].falling_tick=UINT64_MAX;}
    uint32_t old[8];memcpy(old,sync_io_shared_workspace,sizeof(old));unsigned before=io_calls;
    assert(!sync_io_run_output_submit_core1(g,e));
    assert(!memcmp(old,sync_io_shared_workspace,sizeof(old))&&!enable_calls&&io_calls==before);
    assert(snap().blocks==0&&snap().state==SYNC_IO_RUN_OUTPUT_PREPARED);
    complete_stop(g);
}
static void drain_counted_words(unsigned expected)
{
    unsigned emitted=0;
    do {
        dma_drain_to_fifo();
        for(unsigned i=0;i<fifo_count;++i)printf("%u ",fifo[i]);
        emitted+=fifo_count;
        fifo_consume(fifo_count);
    } while(mock_busy);
    assert(emitted==expected&&!hw_dma.ch[2].transfer_count);
}
static void counted_stream(unsigned count)
{
    const unsigned other=count==1u?SYNC_IO_RUN_OUTPUT_MAX_EDGES:1u;
    assert(SYNC_IO_RUN_OUTPUT_MAX_EDGES==16u);
    uint32_t g=prepare();mock_core=1;
    sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_MAX_EDGES];
    make_counted_edges(edges,count,1100000,0,7);
    for(unsigned i=0;i<SYNC_IO_RUN_OUTPUT_MAX_WORDS+1u;++i)sync_io_shared_workspace[i]=0xa5a5a5a5u;
    assert(sync_io_run_output_submit_count_core1(g,edges,count));
    probe_active=false;
    assert(snap().schema==SYNC_IO_RUN_OUTPUT_SCHEMA&&snap().state==SYNC_IO_RUN_OUTPUT_RUNNING);
    assert(snap().blocks==1u&&snap().edges==count&&snap().last_ordinal==count-1u);
    assert(snap().last_falling_tick==1101000u+(count-1u)*250000u);
    assert(snap().anchor_before==1000000u&&snap().anchor_after==1000001u);
    assert(fifo_count==2u&&hw_dma.ch[2].transfer_count==2u*count-2u);
    assert(mock_busy==(count>1u));
    if(count==1u)assert(last_source==NULL);
    else assert(last_source==sync_io_shared_workspace+2u);
    assert(sync_io_shared_workspace[2u*count]==0xa5a5a5a5u);
    uint32_t frozen[SYNC_IO_RUN_OUTPUT_MAX_WORDS+1u];
    memcpy(frozen,sync_io_shared_workspace,sizeof(frozen));
    if(mock_busy) {
        unsigned before=io_calls;
        assert(!sync_io_run_output_submit_count_core1(g,edges,count));
        assert(!memcmp(frozen,sync_io_shared_workspace,sizeof(frozen))&&io_calls==before);
    }
    drain_counted_words(2u*count);
    make_counted_edges(edges,other,1100000u+count*250000u,count,9);
    unsigned before=io_calls;
    mock_busy=true;
    assert(!sync_io_run_output_submit_count_core1(g,edges,other));
    mock_busy=false;hw_dma.ch[2].transfer_count=1u;
    assert(!sync_io_run_output_submit_count_core1(g,edges,other));
    assert(!memcmp(frozen,sync_io_shared_workspace,sizeof(frozen))&&io_calls==before);
    hw_dma.ch[2].transfer_count=0u;
    assert(sync_io_run_output_submit_count_core1(g,edges,other));
    assert(snap().source_retirements==1u&&snap().blocks==2u&&snap().edges==count+other);
    assert(snap().first_ordinal==0u&&snap().last_ordinal==count+other-1u);
    assert(snap().first_model==7u&&snap().last_model==9u&&snap().model_changes==1u);
    assert(hw_dma.ch[2].transfer_count==2u*other&&last_source==sync_io_shared_workspace);
    assert(sync_io_shared_workspace[2u*other]==frozen[2u*other]);
    drain_counted_words(2u*other);printf("\n");
    before=io_calls;
    assert(!sync_io_run_output_submit_count_core1(g,edges,other));
    assert(io_calls==before); /* duplicate ordinals never reach hardware */
    complete_stop(g);
}
static void counted_invalid(const char *name)
{
    const bool running=!strcmp(name,"invalid_last_running");
    uint32_t g;
    if(running) {
        sync_io_run_output_edge_t first[4];g=start(first);dma_drain_to_fifo();
    } else {g=prepare();mock_core=1;}
    sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_MAX_EDGES];
    make_counted_edges(edges,SYNC_IO_RUN_OUTPUT_MAX_EDGES,running?2100000u:1100000u,running?4u:0u,9);
    uint32_t count=SYNC_IO_RUN_OUTPUT_MAX_EDGES;
    if(!strcmp(name,"zero"))count=0;
    if(!strcmp(name,"over_max"))count=SYNC_IO_RUN_OUTPUT_MAX_EDGES+1u;
    if(!strncmp(name,"invalid_last_",13))edges[count-1u].falling_tick=edges[count-1u].rising_tick+1u;
    uint32_t frozen[SYNC_IO_RUN_OUTPUT_MAX_WORDS+1u],queued[8];
    memcpy(frozen,sync_io_shared_workspace,sizeof(frozen));memcpy(queued,fifo,sizeof(queued));
    const unsigned before=io_calls,queued_count=fifo_count;
    const sync_io_run_output_snapshot_t old=snap();
    assert(!sync_io_run_output_submit_count_core1(g,!strcmp(name,"null")?NULL:edges,count));
    const sync_io_run_output_snapshot_t after=snap();
    assert(!memcmp(frozen,sync_io_shared_workspace,sizeof(frozen)));
    assert(!memcmp(queued,fifo,sizeof(queued))&&fifo_count==queued_count);
    assert(!memcmp(&old,&after,sizeof(old))&&io_calls==before);
    assert(!mock_busy&&!hw_dma.ch[2].transfer_count);
    complete_stop(g);
}
static void counted_async_stop(void)
{
    uint32_t g=prepare();mock_core=1;
    sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_MAX_EDGES];
    make_counted_edges(edges,SYNC_IO_RUN_OUTPUT_MAX_EDGES,1100000u,0,7);
    assert(sync_io_run_output_submit_count_core1(g,edges,SYNC_IO_RUN_OUTPUT_MAX_EDGES));
    probe_active=false;
    uint32_t frozen[SYNC_IO_RUN_OUTPUT_MAX_WORDS];
    memcpy(frozen,sync_io_shared_workspace,sizeof(frozen));
    const unsigned before=io_calls,clear_before=clears;
    mock_core=0;sync_io_run_output_cancel();assert(io_calls==before);
    assert(!sync_io_run_output_release(g));
    mock_core=1;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&snap().reason==SYNC_IO_RUN_OUTPUT_CANCELLED);
    assert(snap().retire_dma_remaining==2u*SYNC_IO_RUN_OUTPUT_MAX_EDGES-2u);
    assert(!hw_pio.ctrl&&!(pin_latch&(1u<<16))&&hw_dma.abort==(1u<<2));
    assert(!sync_io_run_output_submit_count_core1(g,edges,SYNC_IO_RUN_OUTPUT_MAX_EDGES));
    assert(!memcmp(frozen,sync_io_shared_workspace,sizeof(frozen))&&clears==clear_before);
    mock_busy=false;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&workspace_token);
    hw_dma.abort=0u;mock_busy=true;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRING&&workspace_token);
    mock_busy=false;sync_io_run_output_service_core1();
    assert(snap().state==SYNC_IO_RUN_OUTPUT_RETIRED&&!fifo_count&&workspace_token);
    mock_core=0;assert(sync_io_run_output_release(g));
    assert(!workspace_token&&!core_token&&!mock_dma_claim&&!mock_sm_claim);
}
static void uniform_stream(unsigned count)
{
    const unsigned other=17u-count;
    use_uniform=true;uint32_t g=prepare();mock_core=1;
    sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_MAX_EDGES];
    make_counted_edges(edges,count,1100000u,0u,7u);
    for(unsigned i=0;i<SYNC_IO_RUN_OUTPUT_MAX_WORDS+1u;++i)sync_io_shared_workspace[i]=0xa5a5a5a5u;
    assert(sync_io_run_output_submit_count_core1(g,edges,count));probe_active=false;
    assert(fifo_count==1u&&hw_dma.ch[2].transfer_count==count-1u&&mock_busy==(count>1u));
    assert(snap().schema==7u&&snap().fifo_words_per_edge==1u&&snap().fixed_high_ticks==1000u);
    assert(snap().blocks==1u&&snap().edges==count&&snap().last_ordinal==count-1u);
    assert(sync_io_shared_workspace[count]==0xa5a5a5a5u);
    if(count==1u)assert(last_source==NULL);
    else assert(last_source==sync_io_shared_workspace+1u);
    uint32_t frozen[SYNC_IO_RUN_OUTPUT_MAX_WORDS+1u];memcpy(frozen,sync_io_shared_workspace,sizeof(frozen));
    if(mock_busy) {
        const unsigned before=io_calls;
        assert(!sync_io_run_output_submit_count_core1(g,edges,count));
        assert(!memcmp(frozen,sync_io_shared_workspace,sizeof(frozen))&&io_calls==before);
    }
    drain_counted_words(count);
    make_counted_edges(edges,other,1100000u+count*250000u,count,9u);
    unsigned before=io_calls;
    mock_busy=true;assert(!sync_io_run_output_submit_count_core1(g,edges,other));
    mock_busy=false;hw_dma.ch[2].transfer_count=1u;
    assert(!sync_io_run_output_submit_count_core1(g,edges,other));
    assert(io_calls==before&&!memcmp(frozen,sync_io_shared_workspace,sizeof(frozen)));
    hw_dma.ch[2].transfer_count=0u;
    assert(sync_io_run_output_submit_count_core1(g,edges,other));
    assert(snap().source_retirements==1u&&snap().blocks==2u&&snap().edges==17u);
    assert(snap().last_ordinal==16u&&snap().model_changes==1u);
    assert(hw_dma.ch[2].transfer_count==other&&last_source==sync_io_shared_workspace);
    assert(sync_io_shared_workspace[other]==frozen[other]&&mock_isr==998u);
    drain_counted_words(other);printf("\n");
    complete_stop(g);
    /* A later legacy prepare must reset the mode instead of reusing ISR mode. */
    use_uniform=false;g=prepare();assert(loaded_length==8u);complete_stop(g);
}
static void uniform_invalid(const char *name)
{
    use_uniform=true;
    const bool running=!strcmp(name,"width_running");
    uint32_t g;
    if(running){sync_io_run_output_edge_t initial[4];g=start(initial);dma_drain_to_fifo();}
    else {g=prepare();mock_core=1;}
    sync_io_run_output_edge_t e[16];make_counted_edges(e,16u,running?2100000u:1100000u,running?4u:0u,9u);
    unsigned count=16u;
    if(!strcmp(name,"zero"))count=0;
    if(!strcmp(name,"over_max"))count=17u;
    if(!strncmp(name,"width",5))e[15].falling_tick++;
    if(!strcmp(name,"wrap"))e[15].falling_tick=0u;
    if(!strcmp(name,"ordinal"))e[15].ordinal=e[14].ordinal;
    if(!strcmp(name,"model"))e[15].model_token=0;
    if(!strcmp(name,"too_far")){e[15].rising_tick=UINT64_MAX-1000u;e[15].falling_tick=UINT64_MAX;}
    uint32_t frozen[32],queued[8];memcpy(frozen,sync_io_shared_workspace,sizeof(frozen));
    memcpy(queued,fifo,sizeof(queued));const unsigned queued_count=fifo_count,before=io_calls;
    const sync_io_run_output_snapshot_t previous=snap();
    assert(!sync_io_run_output_submit_count_core1(g,!strcmp(name,"null")?NULL:e,count));
    const sync_io_run_output_snapshot_t after=snap();
    assert(!memcmp(&previous,&after,sizeof(after))&&!memcmp(frozen,sync_io_shared_workspace,sizeof(frozen)));
    assert(!memcmp(queued,fifo,sizeof(queued))&&queued_count==fifo_count&&io_calls==before);
    complete_stop(g);
}
static void uniform_prepare_gates(void)
{
    uint32_t g=0x12345678u;mock_core=0;
    assert(!sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,1000,0u,&g));
    assert(!sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,1000,1u,&g));
    assert(!sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,1000,1000u,NULL));
    mock_core=1;assert(!sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,1000,1000u,&g));
    assert(!io_calls&&!core_token&&!workspace_token&&g==0x12345678u);
    mock_core=0;hw_timer.source=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;
    assert(sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,1000,UINT32_MAX,&g));
    assert(mock_isr==UINT32_MAX-2u&&snap().fixed_high_ticks==UINT32_MAX);complete_stop(g);
}
static void continuous_stream(const char *name)
{
    mock_core=0;hw_timer.source=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;raw(1000000u);
    uint32_t g;
    assert(use_uniform ? sync_io_run_output_prepare_uniform(BOARD_SYS_CLOCK_HZ,0u,1000u,&g) :
        sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,0u,&g));
    mock_core=1;
    sync_io_run_output_edge_t edges[16];
    const unsigned blocks=!strcmp(name,"600") ? 37502u : 2100u;
    for (unsigned b=0;b<blocks;++b) {
        if (b) {
            raw(s_run.last_falling_tick-500000u);sync_io_run_output_service_core1();
            assert(s_run.state==SYNC_IO_RUN_OUTPUT_RUNNING);
        }
        make_counted_edges(edges,16u,1100000ull+(uint64_t)b*4000000u,(uint64_t)b*16u,7u+(b&1u));
        assert(sync_io_run_output_submit_count_core1(g,edges,16u));
        probe_active=false;hw_pio.fdebug=0u;
        assert(s_run.expires_tick==0u && s_run.last_ordinal==(uint64_t)(b+1u)*16u-1u);
        unsigned consumed=0u;
        do {dma_drain_to_fifo();consumed+=fifo_count;fifo_consume(fifo_count);} while(mock_busy);
        assert(consumed==(use_uniform?16u:32u));
    }
    assert(s_run.last_falling_tick-s_run.anchor_before>UINT64_C(32)*BOARD_SYS_CLOCK_HZ);
    if (!strcmp(name,"600")) {
        assert(s_run.last_falling_tick-s_run.anchor_before>UINT64_C(600)*BOARD_SYS_CLOCK_HZ);
        assert(s_run.last_falling_tick>>32u>=34u);
    }
    raw(s_run.last_falling_tick-500000u);sync_io_run_output_service_core1();
    if (!strcmp(name,"saturation")) {
        s_run.blocks=s_run.source_retirements=s_run.model_changes=UINT32_MAX;
        s_run.edges=UINT32_MAX-4u;
        make_counted_edges(edges,16u,s_run.last_rising_tick+250000u,s_run.last_ordinal+1u,99u);
        assert(sync_io_run_output_submit_count_core1(g,edges,16u));
        assert(s_run.blocks==UINT32_MAX && s_run.edges==UINT32_MAX &&
            s_run.source_retirements==UINT32_MAX && s_run.model_changes==UINT32_MAX);
        hw_pio.fdebug=0u;
    } else if (!strcmp(name,"rollback") || !strcmp(name,"submit_rollback")) {
        raw(s_run.service_last_tick-1u);
        if (!strcmp(name,"submit_rollback")) {
            make_counted_edges(edges,16u,s_run.last_rising_tick+250000u,s_run.last_ordinal+1u,99u);
            assert(!sync_io_run_output_submit_count_core1(g,edges,16u));
        } else sync_io_run_output_service_core1();
        assert(s_run.state==SYNC_IO_RUN_OUTPUT_RETIRING && s_run.reason==SYNC_IO_RUN_OUTPUT_CLOCK);
    } else if (!strcmp(name,"dma")) {
        hw_dma.ch[2].ctrl_trig|=DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS;sync_io_run_output_service_core1();
        assert(s_run.reason==SYNC_IO_RUN_OUTPUT_DMA);
    } else if (!strcmp(name,"clock")) {
        hw_timer.pause=1u;sync_io_run_output_service_core1();assert(s_run.reason==SYNC_IO_RUN_OUTPUT_CLOCK);
    }
    complete_stop(g);
    /* New finite preparation must not inherit continuous expiry or counts. */
    hw_timer.pause=0u;hw_dma.ch[2].ctrl_trig=0u;
    const uint32_t next=prepare();assert(next>g && s_run.blocks==0u && s_run.edges==0u);
    mock_core=1;make_counted_edges(edges,4u,1100000u,0u,7u);
    assert(sync_io_run_output_submit_count_core1(next,edges,4u));probe_active=false;hw_pio.fdebug=0u;
    assert(s_run.expires_tick==1000000u+BOARD_SYS_CLOCK_HZ);
    raw(s_run.expires_tick);sync_io_run_output_service_core1();
    assert(s_run.reason==SYNC_IO_RUN_OUTPUT_EXPIRED);complete_stop(next);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strncmp(argv[1],"uniform_base:",13)){use_uniform=true;argv[1]+=13;}
    if(!strcmp(argv[1],"uniform_stream_one")){uniform_stream(1u);return 0;}
    if(!strcmp(argv[1],"uniform_stream_four")){uniform_stream(4u);return 0;}
    if(!strcmp(argv[1],"uniform_stream_ten")){uniform_stream(10u);return 0;}
    if(!strcmp(argv[1],"uniform_stream_max")){uniform_stream(16u);return 0;}
    if(!strcmp(argv[1],"uniform_prepare_gates")){uniform_prepare_gates();return 0;}
    if(!strncmp(argv[1],"continuous_",11)){continuous_stream(argv[1]+11);return 0;}
    if(!strncmp(argv[1],"uniform_invalid:",16)){uniform_invalid(argv[1]+16);return 0;}
    if(!strcmp(argv[1],"gates"))gates();
    else if(!strncmp(argv[1],"prepare_",8))prepare_failure(argv[1]+8);
    else if(!strcmp(argv[1],"retirement"))retirement();
    else if(!strcmp(argv[1],"generation"))stale_generation();
    else if(!strcmp(argv[1],"async_stop"))async_stop();
    else if(!strcmp(argv[1],"diagnostics"))diagnostics();
    else if(!strcmp(argv[1],"diagnostics_invalid_raw"))diagnostics_invalid_raw();
    else if(!strcmp(argv[1],"diagnostics_saturation"))diagnostics_saturation();
    else if(!strncmp(argv[1],"fault_",6))fault(argv[1]+6);
    else if(!strncmp(argv[1],"enable_",7))enable_boundary(argv[1]+7);
    else if(!strcmp(argv[1],"inactive_submit"))inactive_submit();
    else if(!strcmp(argv[1],"inactive_release"))inactive_release();
    else if(!strncmp(argv[1],"invalid_",8))invalid_edges(argv[1]+8);
    else if(!strcmp(argv[1],"count_one"))counted_stream(1u);
    else if(!strcmp(argv[1],"count_max"))counted_stream(SYNC_IO_RUN_OUTPUT_MAX_EDGES);
    else if(!strcmp(argv[1],"count_async_stop"))counted_async_stop();
    else if(!strncmp(argv[1],"count_",6))counted_invalid(argv[1]+6);
    else assert(false);
    return 0;
}
'''
