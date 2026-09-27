"""Actual PIO counter/backend, bounded FIFO freshness, and hotload ownership.

No board access. The backend's hardware calls drive an interpreter of the
assembled production program; GPIO configuration APIs deliberately do not exist.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'components/sync_io/src'


@pytest.fixture(scope='module')
def backend(tmp_path_factory):
    d = tmp_path_factory.mktemp('pulse-counter')
    (d / 'counter_mock.h').write_text(MOCK, encoding='utf-8')
    for name in ('board_config.h', 'hardware/clocks.h', 'hardware/pio.h',
                 'hardware/timer.h', 'pico/platform.h', 'tdma_runtime_owner.h'):
        p = d / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "counter_mock.h"\n', encoding='utf-8')
    asm = os.environ.get('PIOASM') or shutil.which('pioasm') or str(
        Path.home() / '.pico-sdk/tools/2.2.0/pioasm/pioasm.exe')
    r = subprocess.run([asm, '-o', 'c-sdk', str(SRC / 'sync_pulse_counter.pio'),
                        str(d / 'sync_pulse_counter.pio.h')], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    (d / 'counter.c').write_text(HARNESS, encoding='utf-8')
    exe = d / 'counter.exe'
    r = subprocess.run([shutil.which('gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(d), '-I' + str(ROOT / 'components/sync_io/inc'),
                        '-DCOUNTER_SOURCE="' + (SRC / 'sync_io_pulse_counter.c').as_posix() + '"',
                        str(d / 'counter.c'), '-o', str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return exe


@pytest.mark.parametrize('case', ['cycle', 'idle', 'pin', 'core', 'generation', 'clear',
                                 'stale', 'overflow', 'fifo_full', 'fifo_missing', 'clock',
                                 'load_fail', 'arm_fail', 'gate_fail', 'stop_busy', 'reload',
                                 'route1','route2','route3','route4','route_readback',
                                 'run_readonly','run_fifo_empty','stop_refresh',
                                 'read_core1','read_core1_missing','read_core1_clock'])
def test_hardware_count_and_lifecycle(backend, case):
    r = subprocess.run([str(backend), case], capture_output=True, text=True, timeout=10)
    assert r.returncode == 0, r.stdout + r.stderr


MOCK = r'''
#ifndef COUNTER_MOCK_H
#define COUNTER_MOCK_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sync_io_persona_manager.h"
#define SYNC_IO_CORE_INTERNAL_H
typedef unsigned uint;
#define PICO_PIO_VERSION 0
#define BOARD_SYS_CLOCK_HZ 250000000u
#define BOARD_SYNC_TRIG_IN_PIN 23u
#define BOARD_SYNC_INPUT_BASE_PIN 20u
#define BOARD_SYNC_INPUT_PIN_COUNT 4u
#define BOARD_SYNC_INPUT_BITS_REVERSED 1u
#define PIO_SM0_EXECCTRL_JMP_PIN_BITS 0x1f000000u
#define PIO_SM0_EXECCTRL_JMP_PIN_LSB 24u
#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 1u
#define clk_sys 0u
enum {pio_x=1,pio_null=3,pio_isr=6};
typedef struct {struct {uint32_t execctrl;} sm[4];} mock_pio;
typedef mock_pio *PIO;
struct pio_program {const uint16_t *instructions;uint length;int origin;uint pio_version;};
typedef struct {uint32_t pause,source,timerawh,timerawl;} mock_timer;
typedef struct {uint lo,hi,pin,div,frac;} pio_sm_config;
static mock_pio peripheral;
static mock_timer timer={.source=1};
#define BOARD_SYNC_PIO_FAST (&peripheral)
#define timer1_hw (&timer)
static unsigned core,hz=BOARD_SYS_CLOCK_HZ,pc,words,depth;
static unsigned expected_pin=23u;
static uint32_t x,isr,fifo[4];
static uint16_t code[8];
static uint64_t sampled_at[4],last_popped_at,mov_at;
static bool enabled,claimed,loaded,pin,gate,deny_load,deny_arm,deny_gate,deny_stop,freeze;
static bool tdma_stopped=true,maintenance_owned;
static unsigned fifo_pops;
typedef void tdma_service_service_t;
static tdma_service_service_t *tdma_runtime_owner_get(void){return (void *)1;}
static bool tdma_service_run_stopped_maintenance(tdma_service_service_t *s,bool (*f)(void *),void *p){
    assert(s&&core==0&&!maintenance_owned);
    if(!tdma_stopped)return false;
    maintenance_owned=true;bool ok=f(p);maintenance_owned=false;return ok;
}
static uint64_t ticks;
static void tick(void) {
    ++ticks;timer.timerawl=(uint32_t)ticks;timer.timerawh=(uint32_t)(ticks>>32);
    if(!enabled || freeze)return;
    assert(pc<8);uint16_t ins=code[pc];unsigned next=(pc+1)%8;
    if((ins>>13)==5) {
        assert(((ins>>5)&7)==6 && (ins&7)==1 && ((ins>>3)&3)==1);
        isr=~x;mov_at=ticks;
    } else if((ins>>13)==4) {
        assert(!(ins&0x80) && !(ins&0x20));
        if(depth<4){fifo[depth]=isr;sampled_at[depth++]=mov_at;}
        isr=0;
    } else {
        assert((ins>>13)==0);unsigned cond=(ins>>5)&7;bool take;
        if(cond==0)take=true;
        else if(cond==6)take=pin;
        else {assert(cond==2);take=x!=0;x--;}
        if(take)next=ins&31;
    }
    pc=next;
}
static void advance(unsigned n){for(unsigned i=0;i<n;i++)tick();}
static void pulse(void){pin=false;advance(50);pin=true;advance(50);}
static void access(PIO p,uint sm){assert(p==BOARD_SYNC_PIO_FAST&&sm==3);advance(8);}
static unsigned get_core_num(void){return core;}
static unsigned clock_get_hz(unsigned clk){assert(clk==0);return hz;}
static bool pio_sm_is_claimed(PIO p,uint sm){access(p,sm);return claimed;}
static void pio_sm_claim(PIO p,uint sm){access(p,sm);assert(!claimed);claimed=true;}
static void pio_sm_unclaim(PIO p,uint sm){access(p,sm);assert(claimed);claimed=false;}
static bool pio_can_add_program(PIO p,const struct pio_program *g){assert(p==BOARD_SYNC_PIO_FAST&&g->length==8);return !deny_load;}
static unsigned pio_add_program(PIO p,const struct pio_program *g){assert(p==BOARD_SYNC_PIO_FAST&&!loaded);loaded=true;words=g->length;memcpy(code,g->instructions,sizeof(code));return 0;}
static void pio_remove_program(PIO p,const struct pio_program *g,uint off){assert(p==BOARD_SYNC_PIO_FAST&&loaded&&!enabled&&g->length==8&&!off);loaded=false;words=0;}
static pio_sm_config pio_get_default_sm_config(void){return (pio_sm_config){0};}
static void sm_config_set_wrap(pio_sm_config *c,uint lo,uint hi){c->lo=lo;c->hi=hi;}
static void sm_config_set_jmp_pin(pio_sm_config *c,uint n){c->pin=n;}
static void sm_config_set_clkdiv_int_frac(pio_sm_config *c,uint div,uint frac){c->div=div;c->frac=frac;}
static void sm_config_set_in_shift(pio_sm_config *c,bool right,bool autopush,uint bits){(void)c;assert(!right&&!autopush&&bits==32);}
static int pio_sm_init(PIO p,uint sm,uint off,const pio_sm_config *c){access(p,sm);assert(!off&&c->pin==expected_pin&&c->lo==0&&c->hi==7&&c->div==1&&!c->frac);p->sm[sm].execctrl=c->pin<<24;enabled=false;pc=0;return deny_arm?-1:0;}
static void pio_sm_set_enabled(PIO p,uint sm,bool active){access(p,sm);enabled=active;}
static void pio_sm_clear_fifos(PIO p,uint sm){access(p,sm);depth=0;}
static void pio_sm_restart(PIO p,uint sm){access(p,sm);isr=0;}
static void pio_sm_clkdiv_restart(PIO p,uint sm){access(p,sm);}
static unsigned pio_encode_mov_not(unsigned dest,unsigned src){return 0xa000u|(dest<<5)|8u|src;}
static unsigned pio_encode_push(bool iffull,bool block){return 0x8000u|((uint)iffull<<6)|((uint)block<<5);}
static unsigned pio_encode_jmp(uint target){return target;}
static void pio_sm_exec(PIO p,uint sm,unsigned ins){
    access(p,sm);assert(!enabled);
    if(ins==pio_encode_mov_not(pio_x,pio_null))x=UINT32_MAX;
    else if(ins==pio_encode_mov_not(pio_isr,pio_x)){isr=~x;mov_at=ticks;}
    else if(ins==pio_encode_push(false,false)){assert(depth<4);fifo[depth]=isr;sampled_at[depth++]=mov_at;}
    else {assert(ins==0);pc=0;}
}
static bool pio_sm_is_rx_fifo_empty(PIO p,uint sm){access(p,sm);return depth==0;}
static unsigned pio_sm_get(PIO p,uint sm){access(p,sm);assert(depth);++fifo_pops;uint32_t v=fifo[0];last_popped_at=sampled_at[0];--depth;memmove(fifo,fifo+1,depth*sizeof(fifo[0]));memmove(sampled_at,sampled_at+1,depth*sizeof(sampled_at[0]));return v;}
bool sync_io_core_pulse_counter_reserve(const void *p){assert(p);if(gate||deny_gate)return false;gate=true;return true;}
bool sync_io_core_pulse_counter_release(const void *p){assert(p&&gate);gate=false;return true;}
static sync_io_persona_manager_hooks_t hooks;
static sync_io_persona_descriptor_t desc={.id=SYNC_IO_PERSONA_ID_PULSE_COUNTER};
static unsigned lease_state;
bool sync_io_pio0_runtime_prepare_io(sync_io_persona_id_t id,uint32_t read_mask,uint32_t write_mask,const sync_io_persona_manager_hooks_t *h,void *ctx,sync_io_persona_manager_handle_t *out){
    assert(read_mask==(1u<<expected_pin)&&!write_mask);desc.gpio_read_mask=read_mask;
    assert(id==desc.id&&!ctx);hooks=*h;
    bool ok=h->load(NULL,&desc,0)&&h->arm(NULL,&desc,0);
    if(!ok)h->cleanup(NULL,&desc,0);else{out->generation=1;lease_state=3;}return ok;
}
bool sync_io_pio0_runtime_release(sync_io_persona_manager_handle_t *h){assert(h->generation==1);hooks.cleanup(NULL,&desc,0);lease_state=0;return true;}
bool sync_io_pio0_runtime_counter_start_core0(sync_io_persona_manager_handle_t *h){assert(h->generation==1);if(lease_state==2){assert(hooks.arm(NULL,&desc,0));lease_state=3;}if(lease_state!=3)return false;lease_state=4;return true;}
bool sync_io_pio0_runtime_counter_stop_core0(sync_io_persona_manager_handle_t *h){assert(h->generation==1);if(deny_stop)return false;assert(lease_state==4);lease_state=2;return true;}
#endif
'''

HARNESS = r'''
#include "counter_mock.h"
#include COUNTER_SOURCE
static sync_io_pulse_counter_snapshot_t snapshot(void){sync_io_pulse_counter_snapshot_t s;assert(sync_io_pulse_counter_snapshot(&s));return s;}
int main(int argc,char **argv){
    assert(argc==2);const char *c=argv[1];uint32_t g=99;
    uint32_t port=!strncmp(c,"route",5)&&c[5]>='1'&&c[5]<='4'?(uint32_t)(c[5]-'0'):1u;expected_pin=24u-port;
    if(!strcmp(c,"idle")){assert(snapshot().schema==1&&!snapshot().valid);return 0;}
    if(!strcmp(c,"pin")){assert(!sync_io_pulse_counter_load(0,&g));assert(!sync_io_pulse_counter_load(5,&g));assert(g==99&&!gate&&!claimed);return 0;}
    if(!strcmp(c,"core")){core=1;assert(!sync_io_pulse_counter_load(1,&g));assert(!sync_io_pulse_counter_snapshot(&(sync_io_pulse_counter_snapshot_t){0}));core=0;}
    if(!strcmp(c,"load_fail")){deny_load=true;assert(!sync_io_pulse_counter_load(1,&g));assert(!gate&&!claimed&&!loaded&&g==99);deny_load=false;}
    if(!strcmp(c,"arm_fail")){deny_arm=true;assert(!sync_io_pulse_counter_load(1,&g));assert(!gate&&!claimed&&!loaded&&g==99);deny_arm=false;}
    if(!strcmp(c,"gate_fail")){deny_gate=true;assert(!sync_io_pulse_counter_load(1,&g));assert(!gate&&!claimed&&!loaded&&g==99);deny_gate=false;}
    assert(sync_io_pulse_counter_load(port,&g));assert(g==1&&gate&&claimed&&words==8&&!enabled);
    assert(snapshot().input_pin==expected_pin&&snapshot().input_port==port&&snapshot().count32==0);
    if(!strcmp(c,"route_readback")){peripheral.sm[3].execctrl=20u<<24;assert(snapshot().input_pin==20);peripheral.sm[3].execctrl=expected_pin<<24;}
    if(!strcmp(c,"generation")){assert(!sync_io_pulse_counter_start(g+1));assert(!sync_io_pulse_counter_clear(g+1));assert(!sync_io_pulse_counter_release(g+1));}
    pin=true;assert(sync_io_pulse_counter_start(g));advance(100);assert(snapshot().count32==0);
    assert(!sync_io_pulse_counter_start(g)&&!sync_io_pulse_counter_clear(g)&&!sync_io_pulse_counter_release(g));
    for(unsigned i=0;i<71;i++)pulse();
    assert(snapshot().count32==71);
    if(!strncmp(c,"read_core1",10)){
        uint32_t count=0xa5a5u;
        assert(!sync_io_pulse_counter_read_core1(&count)&&count==0xa5a5u);
        core=1;assert(!sync_io_pulse_counter_read_core1(NULL));
        for(unsigned i=0;i<9;i++)pulse();
        const uint32_t before_guard=s_snapshot_guard;
        if(!strcmp(c,"read_core1_missing")){
            freeze=true;depth=4;
            assert(!sync_io_pulse_counter_read_core1(&count)&&count==0xa5a5u&&s_snapshot_guard==before_guard);
            freeze=false;
        }
        if(!strcmp(c,"read_core1_clock"))timer.pause=1;
        assert(sync_io_pulse_counter_read_core1(&count)&&count==80&&s_counter.valid);
        assert(s_counter.count32==count&&s_counter.generation==g&&s_counter.clear_generation==0);
        assert(s_counter.timestamp_valid==(!timer.pause));
        if(s_counter.timestamp_valid)assert(s_counter.sample_before_tick<=last_popped_at&&last_popped_at<=s_counter.sample_after_tick);
        core=0;tdma_stopped=false;const unsigned pops=fifo_pops;
        assert(snapshot().count32==count&&fifo_pops==pops);
        return 0;
    }
    if(!strcmp(c,"run_readonly")||!strcmp(c,"run_fifo_empty")||!strcmp(c,"stop_refresh")){
        tdma_stopped=false;core=1;
        sync_io_pulse_counter_snapshot_t last;
        assert(sync_io_pulse_counter_snapshot_core1(&last)&&last.count32==71);
        core=0;for(unsigned i=0;i<9;i++)pulse();
        if(!strcmp(c,"run_fifo_empty")){freeze=true;depth=0;}
        const unsigned before_pops=fifo_pops;
        const uint32_t before_guard=s_snapshot_guard;
        const uint64_t before_ticks=ticks;
        sync_io_pulse_counter_snapshot_t observed=snapshot();
        assert(!memcmp(&last,&observed,sizeof(last))&&fifo_pops==before_pops&&s_snapshot_guard==before_guard&&ticks==before_ticks);
        /* Transition to real STOP grants Core0 the FIFO and preserves the
         * previous fresh-drain behavior, including failure with no words. */
        tdma_stopped=true;
        if(freeze){assert(!sync_io_pulse_counter_snapshot(&observed));freeze=false;}
        observed=snapshot();assert(observed.count32==80&&fifo_pops>before_pops);
        assert(observed.generation==last.generation&&observed.clear_generation==last.clear_generation);
    }
    if(!strcmp(c,"stale") || !strcmp(c,"fifo_full")){
        advance(20);assert(depth==4);for(unsigned i=0;i<113;i++)pulse();assert(depth==4);
        sync_io_pulse_counter_snapshot_t s=snapshot();assert(s.count32==184);
        assert(s.timestamp_valid&&s.sample_before_tick<=last_popped_at&&last_popped_at<=s.sample_after_tick);
    }
    if(!strcmp(c,"fifo_missing")){
        freeze=true;depth=0;sync_io_pulse_counter_snapshot_t a;memset(&a,0xa5,sizeof(a));sync_io_pulse_counter_snapshot_t b=a;
        assert(!sync_io_pulse_counter_snapshot(&a)&&!memcmp(&a,&b,sizeof(a)));freeze=false;
    }
    if(!strcmp(c,"overflow")){x=1;for(unsigned i=0;i<3;i++)pulse();assert(snapshot().count32==1);}
    if(!strcmp(c,"clock")){timer.pause=1;assert(snapshot().valid&&!snapshot().timestamp_valid);timer.pause=0;assert(snapshot().timestamp_valid);}
    if(!strcmp(c,"stop_busy")){deny_stop=true;assert(!sync_io_pulse_counter_stop(g)&&enabled);deny_stop=false;}
    assert(sync_io_pulse_counter_stop(g)&&!enabled&&lease_state==2);uint32_t stopped=snapshot().count32;
    for(unsigned i=0;i<20;i++){pulse();}
    assert(snapshot().count32==stopped);
    assert(sync_io_pulse_counter_start(g));advance(100);assert(snapshot().count32==stopped);
    pulse();assert(snapshot().count32==stopped+1);assert(sync_io_pulse_counter_stop(g));
    assert(sync_io_pulse_counter_clear(g));assert(snapshot().count32==0&&snapshot().clear_generation==1);
    assert(sync_io_pulse_counter_release(g));assert(!gate&&!claimed&&!loaded&&!enabled&&snapshot().state==0);
    if(!strcmp(c,"reload")){uint32_t next;assert(sync_io_pulse_counter_load(1,&next)&&next==2);assert(!sync_io_pulse_counter_start(g));assert(sync_io_pulse_counter_release(next));}
    return 0;
}
'''


def test_persona_catalog_and_real_runtime_reload(tmp_path):
    from test_sync_io_workspace import compile_run
    runtime = (SRC / 'sync_io_pio0_runtime.c').read_text(encoding='utf-8').replace(
        '#include "pico.h"', '')
    code = r'''
#include <assert.h>
#include "sync_io_pio0_runtime.h"
#include "sync_io_pulse_counter.h"
#include "resource_arbiter.h"
static unsigned core,loads,arms,cleanups;
static unsigned get_core_num(void){return core;}
void osal_critical_enter(void){}
void osal_critical_exit(void){}
static bool load_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma){(void)ctx;assert(d->id==SYNC_IO_PERSONA_ID_PULSE_COUNTER&&!dma);loads++;return true;}
static bool arm_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma){(void)ctx;(void)d;assert(!dma);arms++;return true;}
static void cleanup_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma){(void)ctx;(void)d;assert(!dma);cleanups++;}
''' + runtime + r'''
int main(void){
    assert(sync_io_persona_catalog_valid());resource_arbiter_init();
    const sync_io_persona_descriptor_t *c=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_PULSE_COUNTER);
    assert(c->sm_mask==8&&c->instruction_words==8&&c->gpio_read_mask==(1u<<23));
    assert(c->flags==SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD&&!c->gpio_write_mask&&!c->workspace_mask&&!c->dma_channel_mask&&!c->irq_mask);
    sync_io_persona_compatibility_t compatible;
    assert(sync_io_persona_compatible(c,sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_REFERENCE_MONITOR),&compatible));
    const sync_io_persona_manager_hooks_t h={.load=load_hook,.arm=arm_hook,.cleanup=cleanup_hook};
    sync_io_persona_manager_handle_t lease;
    for(unsigned n=0;n<2;n++){
        assert(sync_io_pio0_runtime_prepare(c->id,&h,NULL,&lease));
        assert(sync_io_pio0_runtime_counter_start_core0(&lease));
        assert(sync_io_pio0_runtime_counter_stop_core0(&lease));
        assert(sync_io_pio0_runtime_counter_start_core0(&lease));
        assert(sync_io_pio0_runtime_counter_stop_core0(&lease));
        assert(sync_io_pio0_runtime_release(&lease));
        assert(!s_runtime.active_count&&!s_runtime.pio_resource_held);
    }
    assert(loads==2&&arms==4&&cleanups==2);return 0;
}
'''
    compile_run(tmp_path, code, analyzer=True)


def test_counter_admission_allows_reference_excludes_legacy_and_run(tmp_path):
    from test_sync_io_workspace import compile_run, function
    model = (SRC / 'sync_io_model_sched.c').read_text(encoding='utf-8')
    enum = re.search(r'typedef enum \{\n    SYNC_IO_SCHEDULE_IDLE.*?} sync_io_schedule_phase_t;', model, re.S)[0]
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define __not_in_flash_func(n) n
static unsigned core;
static bool initialized=true,capture,sequence,encoder,pwm;
static struct {bool running;} s_model_pulse;
static bool s_wave_output_manager_active,s_wave_output_sm_claimed,s_wave_output_dma_claimed,s_wave_output_program_loaded;
static uint32_t s_schedule_phase;
static uintptr_t s_turntable_source_token,s_run_output_token,s_reference_token,s_counter_token;
#define get_core_num() core
#define sync_io_core_initialized() initialized
#define sync_io_core_capture_is_running() capture
#define sync_io_seq_step_is_running() sequence
#define sync_io_enc_count_is_running() encoder
#define sync_io_core_sma_frequency_output_active() pwm
''' + enum + '\n'
    for name in ('sync_io_schedule_reserve', 'sync_io_schedule_publish_phase',
                 'sync_io_core_legacy_try_enter', 'sync_io_core_legacy_leave',
                 'sync_io_core_run_output_reserve', 'sync_io_core_run_output_held',
                 'sync_io_core_run_output_release', 'sync_io_core_reference_reserve',
                 'sync_io_core_reference_release', 'sync_io_core_pulse_counter_reserve',
                 'sync_io_core_pulse_counter_release'):
        code += function(model, name)
    code += r'''
int main(void){
    static int c,r,o,bad;
    for(unsigned reverse=0;reverse<2;reverse++){
        if(reverse)assert(sync_io_core_reference_reserve(&r));
        assert(sync_io_core_pulse_counter_reserve(&c));
        if(!reverse)assert(sync_io_core_reference_reserve(&r));
        assert(!sync_io_core_legacy_try_enter()&&!sync_io_core_run_output_reserve(&o));
        assert(!sync_io_core_pulse_counter_release(&bad));
        assert(sync_io_core_pulse_counter_release(&c));
        assert(sync_io_core_reference_release(&r));
    }
    capture=true;assert(!sync_io_core_pulse_counter_reserve(&c));capture=false;
    assert(sync_io_core_run_output_reserve(&o));assert(!sync_io_core_pulse_counter_reserve(&c));assert(sync_io_core_run_output_release(&o));
    assert(sync_io_core_legacy_try_enter());assert(!sync_io_core_pulse_counter_reserve(&c));sync_io_core_legacy_leave();
    assert(sync_io_core_pulse_counter_reserve(&c));assert(sync_io_core_pulse_counter_release(&c));return 0;
}
'''
    compile_run(tmp_path, code)
