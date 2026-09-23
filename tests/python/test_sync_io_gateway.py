"""Prepared-only PIO0 gateway: real assembly/backend and hotload rollback.

Hardware shims intentionally have no DMA, IRQ-enable, or pulse-queue API.
Every SM-enable call asserts disabled. GPIO tests preserve the input and
restore the output function, direction, SIO latch, and both pulls exactly.
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
    d = tmp_path_factory.mktemp('gateway')
    (d / 'gateway_mock.h').write_text(MOCK, encoding='utf-8')
    for name in ('board_config.h', 'hardware/clocks.h', 'hardware/pio.h',
                 'hardware/gpio.h', 'hardware/structs/sio.h', 'hardware/structs/io_bank0.h',
                 'hardware/structs/pads_bank0.h', 'hardware/regs/pads_bank0.h', 'pico/platform.h'):
        p = d / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "gateway_mock.h"\n', encoding='utf-8')
    asm = os.environ.get('PIOASM') or shutil.which('pioasm') or str(
        Path.home() / '.pico-sdk/tools/2.2.0/pioasm/pioasm.exe')
    r = subprocess.run([asm, '-o', 'c-sdk', str(SRC / 'sync_gateway.pio'),
                        str(d / 'sync_gateway.pio.h')], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    (d / 'gateway.c').write_text(HARNESS, encoding='utf-8')
    exe = d / 'gateway.exe'
    r = subprocess.run([shutil.which('gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(d), '-I' + str(ROOT / 'components/sync_io/inc'),
                        '-DGATEWAY_SOURCE="' + (SRC / 'sync_io_gateway.c').as_posix() + '"',
                        str(d / 'gateway.c'), '-o', str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return exe


@pytest.mark.parametrize('case', ['cycle', 'ready_high', 'ready_low', 'restore_high',
                                 'restore_input', 'restore_mux', 'wrong_port', 'pulse',
                                 'core', 'generation', 'gate', 'load_output_fail',
                                 'load_ready_fail', 'init_output_fail', 'init_ready_fail',
                                 'sm_output_busy', 'sm_ready_busy', 'repeated', 'stop',
                                 'clock', 'snapshot_unchanged', 'restore_registers', 'route_readback',
                                 'restore_pio0_high', 'restore_pio0_input'] +
                         [f'route{i}{o}' for i in range(1,5) for o in range(1,5)])
def test_gateway_never_fires_and_restores_exactly(backend, case):
    r = subprocess.run([str(backend), case], capture_output=True, text=True, timeout=10)
    assert r.returncode == 0, r.stdout + r.stderr


MOCK = r'''
#ifndef GATEWAY_MOCK_H
#define GATEWAY_MOCK_H
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
#define BOARD_SYNC_OUTPUT_BASE_PIN 16u
#define BOARD_SYNC_OUTPUT_PIN_COUNT 4u
#define PADS_BANK0_GPIO0_OD_BITS 0x80u
#define PIO_SM0_PINCTRL_IN_BASE_BITS 0x000f8000u
#define PIO_SM0_PINCTRL_IN_BASE_LSB 15u
#define PIO_SM0_PINCTRL_SET_BASE_BITS 0x000003e0u
#define PIO_SM0_PINCTRL_SET_BASE_LSB 5u
#define clk_sys 0u
#define GPIO_OUT true
typedef enum {GPIO_FUNC_PIO0=6,GPIO_FUNC_PIO1=7,GPIO_FUNC_SIO=5,GPIO_FUNC_NULL=31} gpio_function_t;
enum {pio_x=1,pio_null=3};
typedef struct {uint32_t ctrl,irq,dbg_padout,dbg_padoe;struct {uint32_t pinctrl;} sm[4];} mock_pio;
typedef mock_pio *PIO;
struct pio_program {const uint16_t *instructions;uint length;int origin;uint pio_version;};
typedef struct {uint lo,hi,set_pin,in_pin,div,frac;} pio_sm_config;
static mock_pio peripheral;
static struct {uint32_t gpio_out;} sio;
static struct {struct {uint32_t ctrl;} io[30];} io_bank;
static struct {uint32_t io[30];} pads;
#define BOARD_SYNC_PIO_FAST (&peripheral)
#define sio_hw (&sio)
#define io_bank0_hw (&io_bank)
#define pads_bank0_hw (&pads)
static uint32_t core,hz=BOARD_SYS_CLOCK_HZ,claimed,loaded_words,add_calls,remove_calls,init_calls,io_writes;
static uint32_t deny_load,deny_init;
static bool gate,deny_gate;
static struct {bool dir,level;} pins[30];
static uint input_pin=23u,output_pin=19u;
static unsigned get_core_num(void){return core;}
static unsigned clock_get_hz(unsigned clk){assert(clk==0);return hz;}
static void check(PIO p,uint sm){assert(p==BOARD_SYNC_PIO_FAST&&(sm==1||sm==3));}
static bool pio_sm_is_claimed(PIO p,uint sm){check(p,sm);return (claimed&(1u<<sm))!=0;}
static void pio_sm_claim(PIO p,uint sm){check(p,sm);assert(!(claimed&(1u<<sm)));claimed|=1u<<sm;}
static void pio_sm_unclaim(PIO p,uint sm){check(p,sm);assert(claimed&(1u<<sm));claimed&=~(1u<<sm);}
static bool pio_can_add_program(PIO p,const struct pio_program *g){assert(p==BOARD_SYNC_PIO_FAST);return g->length!=deny_load;}
static unsigned pio_add_program(PIO p,const struct pio_program *g){assert(p==BOARD_SYNC_PIO_FAST);add_calls++;loaded_words+=g->length;return g->length==7?5:12;}
static void pio_remove_program(PIO p,const struct pio_program *g,uint off){assert(p==BOARD_SYNC_PIO_FAST&&off==(g->length==7?5u:12u));remove_calls++;loaded_words-=g->length;}
static pio_sm_config pio_get_default_sm_config(void){return (pio_sm_config){0};}
static void sm_config_set_wrap(pio_sm_config *c,uint lo,uint hi){c->lo=lo;c->hi=hi;}
static void sm_config_set_set_pins(pio_sm_config *c,uint pin,uint count){assert(pin==output_pin&&count==1);c->set_pin=pin;}
static void sm_config_set_in_pins(pio_sm_config *c,uint pin){assert(pin==input_pin);c->in_pin=pin;}
static void sm_config_set_out_shift(pio_sm_config *c,bool right,bool autopull,uint n){(void)c;assert(right&&!autopull&&n==32);}
static void sm_config_set_clkdiv_int_frac(pio_sm_config *c,uint d,uint f){c->div=d;c->frac=f;}
static int pio_sm_init(PIO p,uint sm,uint off,const pio_sm_config *c){check(p,sm);init_calls++;assert(c->div==1&&!c->frac);if(sm==1)assert(off==5&&c->set_pin==output_pin&&c->lo==5&&c->hi==11);else assert(off==12&&c->in_pin==input_pin&&c->lo==12&&c->hi==17);p->sm[sm].pinctrl=(c->set_pin<<5)|(c->in_pin<<15);return deny_init==sm?-1:0;}
static void pio_sm_set_enabled(PIO p,uint sm,bool enable){check(p,sm);assert(!enable);peripheral.ctrl&=~(1u<<sm);}
static void pio_sm_clear_fifos(PIO p,uint sm){check(p,sm);}
static void pio_sm_restart(PIO p,uint sm){check(p,sm);}
static unsigned pio_encode_mov_not(unsigned d,unsigned s){return 0xa000|(d<<5)|8|s;}
static void pio_sm_exec(PIO p,uint sm,unsigned ins){check(p,sm);assert(sm==3&&!(peripheral.ctrl&(1u<<sm))&&ins==pio_encode_mov_not(pio_x,pio_null));}
static void pio_sm_set_pins_with_mask(PIO p,uint sm,uint value,uint mask){check(p,sm);assert(sm==1&&(claimed&(1u<<sm))&&mask==(1u<<output_pin)&&!(value&~mask));if(value)assert(pads.io[output_pin]&PADS_BANK0_GPIO0_OD_BITS);p->dbg_padout=(p->dbg_padout&~mask)|value;}
static void pio_sm_set_consecutive_pindirs(PIO p,uint sm,uint pin,uint count,bool out){check(p,sm);assert(sm==1&&pin==output_pin&&count==1&&out);p->dbg_padoe|=1u<<pin;}
static void pio_sm_set_pindirs_with_mask(PIO p,uint sm,uint value,uint mask){check(p,sm);assert(sm==1&&(claimed&(1u<<sm))&&mask==(1u<<output_pin)&&!(value&~mask));assert(pads.io[output_pin]&PADS_BANK0_GPIO0_OD_BITS);p->dbg_padoe=(p->dbg_padoe&~mask)|value;}
static void pio_interrupt_clear(PIO p,uint irq){assert(p==BOARD_SYNC_PIO_FAST&&irq==6);peripheral.irq&=~(1u<<irq);}
static bool pio_interrupt_get(PIO p,uint irq){assert(p==BOARD_SYNC_PIO_FAST&&irq==6);return (peripheral.irq&(1u<<irq))!=0;}
static bool gpio_get_dir(uint pin){assert(pin==output_pin);return pins[pin].dir;}
static void gpio_set_function(uint pin,gpio_function_t fn){assert(pin==output_pin);io_writes++;io_bank.io[pin].ctrl=fn;pads.io[pin]=(pads.io[pin]&~0x180u)|0x40u;}
static void gpio_set_dir(uint pin,bool out){assert(pin==output_pin);io_writes++;pins[pin].dir=out;}
static void gpio_put(uint pin,bool value){assert(pin==output_pin);io_writes++;if(value)sio.gpio_out|=1u<<pin;else sio.gpio_out&=~(1u<<pin);}
static bool gpio_get(uint pin){assert(pin>=16&&pin<24);return pin>=20?pins[pin].level:(io_bank.io[pin].ctrl&31)==GPIO_FUNC_PIO0?(peripheral.dbg_padout&(1u<<pin))!=0:(sio.gpio_out&(1u<<pin))!=0;}
static uint pio_sm_get_tx_fifo_level(PIO p,uint sm){check(p,sm);assert(claimed&(1u<<sm));return 0;}
static uint pio_sm_get_rx_fifo_level(PIO p,uint sm){check(p,sm);assert(claimed&(1u<<sm));return 0;}
bool sync_io_core_gateway_reserve(const void *p){assert(p);if(gate||deny_gate)return false;gate=true;return true;}
bool sync_io_core_gateway_release(const void *p){assert(p&&gate);gate=false;return true;}
static sync_io_persona_manager_hooks_t hooks;
static sync_io_persona_descriptor_t descriptor={.id=SYNC_IO_PERSONA_ID_GATEWAY};
bool sync_io_pio0_runtime_prepare_io(sync_io_persona_id_t id,uint32_t read_mask,uint32_t write_mask,const sync_io_persona_manager_hooks_t *h,void *ctx,sync_io_persona_manager_handle_t *out){
    assert(read_mask==(1u<<input_pin)&&write_mask==(1u<<output_pin));descriptor.gpio_read_mask=read_mask;descriptor.gpio_write_mask=write_mask;
    assert(id==descriptor.id&&!ctx);hooks=*h;
    bool ok=h->load(NULL,&descriptor,0)&&h->arm(NULL,&descriptor,0);
    if(!ok)h->cleanup(NULL,&descriptor,0);else out->generation=1;return ok;
}
bool sync_io_pio0_runtime_release(sync_io_persona_manager_handle_t *h){assert(h->generation==1);hooks.cleanup(NULL,&descriptor,0);return true;}
#endif
'''

HARNESS = r'''
#include "gateway_mock.h"
#include GATEWAY_SOURCE
static sync_io_gateway_snapshot_t snapshot(void){sync_io_gateway_snapshot_t s;assert(sync_io_gateway_snapshot(&s));return s;}
int main(int argc,char **argv){
    assert(argc==2);const char *test=argv[1];uint32_t g=99;
    uint32_t inport=1,outport=4;
    if(!strncmp(test,"route",5)&&test[5]>='1'&&test[5]<='4'){inport=(uint32_t)(test[5]-'0');outport=(uint32_t)(test[6]-'0');}
    input_pin=24-inport;output_pin=15+outport;
    io_bank.io[output_pin].ctrl=GPIO_FUNC_SIO;pins[output_pin].dir=true;pads.io[output_pin]=4;
    io_bank.io[input_pin].ctrl=GPIO_FUNC_PIO0;pads.io[input_pin]=8;
    if(!strcmp(test,"ready_high"))pins[input_pin].level=true;
    if(!strcmp(test,"restore_high")){sio.gpio_out|=1u<<output_pin;pads.io[output_pin]=8;}
    if(!strcmp(test,"restore_input")){pins[output_pin].dir=false;sio.gpio_out|=1u<<output_pin;}
    if(!strcmp(test,"restore_mux")){io_bank.io[output_pin].ctrl=GPIO_FUNC_NULL;pins[output_pin].dir=false;pads.io[output_pin]=8;}
    if(!strcmp(test,"restore_registers")){io_bank.io[output_pin].ctrl=0x30031005u;pads.io[output_pin]=0x1bcu;sio.gpio_out|=1u<<output_pin;}
    peripheral.dbg_padout=0x35000003u;peripheral.dbg_padoe=0x6a00000cu;
    if(!strcmp(test,"restore_pio0_high")){io_bank.io[output_pin].ctrl=GPIO_FUNC_PIO0;peripheral.dbg_padout|=1u<<output_pin;peripheral.dbg_padoe|=1u<<output_pin;}
    if(!strcmp(test,"restore_pio0_input")){io_bank.io[output_pin].ctrl=GPIO_FUNC_PIO0;peripheral.dbg_padout|=1u<<output_pin;peripheral.dbg_padoe&=~(1u<<output_pin);}
    const uint32_t original_pio_out=peripheral.dbg_padout,original_pio_oe=peripheral.dbg_padoe;
    unsigned char original[sizeof(pins)];memcpy(original,pins,sizeof(pins));uint32_t latch=sio.gpio_out;
    uint32_t original_ctrl=io_bank.io[output_pin].ctrl,original_pad=pads.io[output_pin];
    const uint32_t input_ctrl=io_bank.io[input_pin].ctrl,input_pad=pads.io[input_pin];
    if(!strcmp(test,"wrong_port")){assert(!sync_io_gateway_load(5,4,1000,&g));assert(!sync_io_gateway_load(1,0,1000,&g));assert(g==99&&!io_writes);}
    if(!strcmp(test,"pulse")){assert(!sync_io_gateway_load(1,4,0,&g));assert(!sync_io_gateway_load(1,4,4,&g));assert(!sync_io_gateway_load(1,4,9,&g));assert(g==99&&!io_writes);}
    if(!strcmp(test,"core")){core=1;assert(!sync_io_gateway_load(1,4,1000,&g));assert(!sync_io_gateway_stop(1));core=0;}
    if(!strcmp(test,"clock")){hz=125000000;assert(!sync_io_gateway_load(1,4,1000,&g));hz=BOARD_SYS_CLOCK_HZ;}
    if(!strcmp(test,"gate"))deny_gate=true;
    if(!strcmp(test,"load_output_fail"))deny_load=7;
    if(!strcmp(test,"load_ready_fail"))deny_load=6;
    if(!strcmp(test,"init_output_fail"))deny_init=1;
    if(!strcmp(test,"init_ready_fail"))deny_init=3;
    if(!strcmp(test,"sm_output_busy"))claimed=1u<<1;
    if(!strcmp(test,"sm_ready_busy"))claimed=1u<<3;
    if(deny_gate||deny_load||deny_init||claimed){
        uint32_t prior_claimed=claimed;
        assert(!sync_io_gateway_load(1,4,1000,&g));assert(g==99&&!gate&&!loaded_words&&claimed==prior_claimed&&!io_writes);
        assert(!memcmp(original,pins,sizeof(pins))&&sio.gpio_out==latch);
        deny_gate=false;deny_load=deny_init=claimed=0;
    }
    peripheral.irq=(1u<<6)|(1u<<4);
    assert(sync_io_gateway_load(inport,outport,1000,&g));assert(g==1&&gate&&claimed==10&&loaded_words==13);
    sync_io_gateway_snapshot_t s=snapshot();
    assert(s.loaded&&s.state==SYNC_IO_GATEWAY_LOADED&&s.input_pin==input_pin&&s.output_pin==output_pin&&s.pulse_ticks==250);
    assert(s.sm_claimed_mask==10&&!s.sm_enabled_mask&&s.instruction_words==13&&s.output_program_offset==5&&s.ready_program_offset==12);
    assert(!s.output_level&&!s.grant_pending&&!s.trigger_count&&!s.ready_count&&s.ready_level==pins[input_pin].level);
    assert(!s.output_tx_level&&!s.output_rx_level&&!s.ready_tx_level&&!s.ready_rx_level);
    assert(peripheral.irq==(1u<<4));assert(!memcmp(original+input_pin*sizeof(pins[0]),&pins[input_pin],sizeof(pins[input_pin])));
    assert(input_ctrl==io_bank.io[input_pin].ctrl&&input_pad==pads.io[input_pin]);
    assert(!sync_io_gateway_load(1,4,1000,&g));assert(g==1);
    if(!strcmp(test,"generation")){assert(!sync_io_gateway_stop(g+1));assert(!sync_io_gateway_release(g+1));assert(snapshot().loaded);}
    if(!strcmp(test,"snapshot_unchanged")){sync_io_gateway_snapshot_t a=s;core=1;assert(!sync_io_gateway_snapshot(&a)&&!memcmp(&a,&s,sizeof(a)));core=0;}
    if(!strcmp(test,"route_readback")){
        uint32_t a=peripheral.sm[3].pinctrl,b=peripheral.sm[1].pinctrl;
        peripheral.sm[3].pinctrl=20u<<15;peripheral.sm[1].pinctrl=16u<<5;
        assert(snapshot().input_pin==20&&snapshot().output_pin==16);
        peripheral.sm[3].pinctrl=a;peripheral.sm[1].pinctrl=b;
    }
    assert(sync_io_gateway_stop(g));assert(snapshot().state==SYNC_IO_GATEWAY_STOPPED&&snapshot().loaded&&!snapshot().output_level);
    assert(sync_io_gateway_stop(g));assert(sync_io_gateway_release(g));
    assert(!gate&&!claimed&&!loaded_words&&sio.gpio_out==latch&&!memcmp(original,pins,sizeof(pins)));
    assert(original_ctrl==io_bank.io[output_pin].ctrl&&original_pad==pads.io[output_pin]);
    assert(peripheral.dbg_padout==original_pio_out&&peripheral.dbg_padoe==original_pio_oe);
    assert(!snapshot().loaded&&!snapshot().instruction_words&&!snapshot().sm_claimed_mask&&!snapshot().sm_enabled_mask);
    if(!strcmp(test,"repeated")){uint32_t next;assert(sync_io_gateway_load(1,4,8,&next)&&next==2);assert(snapshot().pulse_ticks==2);assert(!sync_io_gateway_stop(g));assert(sync_io_gateway_release(next));}
    assert(add_calls==remove_calls);return 0;
}
'''


def test_real_persona_runtime_gateway_reference_and_counter_conflict(tmp_path):
    from test_sync_io_workspace import compile_run
    runtime = (SRC / 'sync_io_pio0_runtime.c').read_text(encoding='utf-8').replace('#include "pico.h"', '')
    code = r'''
#include <assert.h>
#include "sync_io_pio0_runtime.h"
#include "sync_io_gateway.h"
#include "resource_arbiter.h"
static unsigned get_core_num(void){return 0;}
void osal_critical_enter(void){}
void osal_critical_exit(void){}
static bool load_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma){(void)ctx;assert(dma==d->dma_channel_mask);return true;}
''' + runtime + r'''
int main(void){
    resource_arbiter_init();assert(sync_io_persona_catalog_valid());
    const sync_io_persona_descriptor_t *g=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_GATEWAY);
    const sync_io_persona_descriptor_t *r=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_REFERENCE_MONITOR);
    const sync_io_persona_descriptor_t *c=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_PULSE_COUNTER);
    sync_io_persona_compatibility_t p;
    assert(g->sm_mask==10&&g->instruction_words==13&&g->gpio_read_mask==(1u<<23)&&g->gpio_write_mask==(1u<<19));
    assert(!g->dma_channel_mask&&!g->workspace_mask&&g->irq_mask==SYNC_IO_PERSONA_IRQ_INTERNAL_6);
    assert(sync_io_persona_compatible(g,r,&p));assert(!sync_io_persona_compatible(g,c,&p)&&(p.conflict_mask&SYNC_IO_PERSONA_CONFLICT_SM));
    const sync_io_persona_manager_hooks_t hooks={.load=load_hook};sync_io_persona_manager_handle_t a,b,conflict;
    for(unsigned reverse=0;reverse<2;reverse++){
        assert(sync_io_pio0_runtime_prepare(reverse?g->id:r->id,&hooks,NULL,&a));
        assert(sync_io_pio0_runtime_prepare(reverse?r->id:g->id,&hooks,NULL,&b));
        assert(!sync_io_pio0_runtime_prepare(c->id,&hooks,NULL,&conflict));
        assert(s_runtime.active_count==2&&s_runtime.instruction_words_total==24);
        assert(sync_io_pio0_runtime_release(&a));assert(sync_io_pio0_runtime_release(&b));
    }
    for(unsigned in=20;in<24;in++)for(unsigned out=16;out<20;out++){
        assert(sync_io_pio0_runtime_prepare_io(g->id,1u<<in,1u<<out,&hooks,NULL,&a));
        const sync_io_persona_descriptor_t *actual=s_runtime.leases[a.slot].descriptor;
        assert(actual==&s_runtime.leases[a.slot].descriptor_value&&actual!=g);
        assert(actual->gpio_read_mask==(1u<<in)&&actual->gpio_write_mask==(1u<<out));
        assert(actual->safe_low_gpio_mask==(1u<<out)&&actual->restore_gpio_mask==(1u<<out));
        assert(actual->sm_mask==g->sm_mask&&actual->instruction_words==g->instruction_words&&actual->irq_mask==g->irq_mask);
        assert(sync_io_pio0_runtime_prepare(r->id,&hooks,NULL,&b));
        assert(actual==s_runtime.leases[a.slot].descriptor&&actual->gpio_write_mask==(1u<<out));
        assert(s_runtime.leases[b.slot].descriptor!=actual&&s_runtime.instruction_words_total==24);
        assert(g->gpio_write_mask==(1u<<19)&&r->gpio_read_mask==(15u<<20));
        assert(sync_io_pio0_runtime_release(&a));
        assert(s_runtime.active_count==1&&s_runtime.instruction_words_total==11&&s_runtime.used_gpio_write_mask==0);
        assert(sync_io_pio0_runtime_release(&b));
    }
    for(unsigned in=20;in<24;in++){
        assert(sync_io_pio0_runtime_prepare_io(c->id,1u<<in,0,&hooks,NULL,&a));
        assert(s_runtime.leases[a.slot].descriptor->gpio_read_mask==(1u<<in));
        assert(sync_io_pio0_runtime_release(&a));
    }
    assert(!sync_io_pio0_runtime_prepare_io(c->id,1u<<23,1u<<19,&hooks,NULL,&a));
    assert(!sync_io_pio0_runtime_prepare_io(g->id,3u<<20,1u<<19,&hooks,NULL,&a));
    assert(!sync_io_pio0_runtime_prepare_io(g->id,1u<<23,0,&hooks,NULL,&a));
    assert(!sync_io_pio0_runtime_prepare_io(g->id,1u<<19,1u<<23,&hooks,NULL,&a));
    assert(!sync_io_pio0_runtime_prepare_io(r->id,1u<<23,0,&hooks,NULL,&a));
    assert(!s_runtime.active_count&&!s_runtime.pio_resource_held);return 0;
}
'''
    compile_run(tmp_path, code, analyzer=True)


def test_gateway_gate_reference_coexistence_and_exclusive_switch(tmp_path):
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
static uintptr_t s_run_output_token,s_reference_token,s_counter_token,s_gateway_token;
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
                 'sync_io_core_pulse_counter_release', 'sync_io_core_gateway_reserve',
                 'sync_io_core_gateway_release'):
        code += function(model, name)
    code += r'''
int main(void){
    static int g,c,r,o,bad;
    for(unsigned reverse=0;reverse<2;reverse++){
        if(reverse)assert(sync_io_core_reference_reserve(&r));
        assert(sync_io_core_gateway_reserve(&g));
        if(!reverse)assert(sync_io_core_reference_reserve(&r));
        assert(!sync_io_core_legacy_try_enter()&&!sync_io_core_run_output_reserve(&o));
        assert(!sync_io_core_pulse_counter_reserve(&c));assert(!sync_io_core_gateway_release(&bad));
        assert(sync_io_core_gateway_release(&g));assert(sync_io_core_reference_release(&r));
        assert(sync_io_core_pulse_counter_reserve(&c));assert(!sync_io_core_gateway_reserve(&g));assert(sync_io_core_pulse_counter_release(&c));
    }
    capture=true;assert(!sync_io_core_gateway_reserve(&g));capture=false;
    assert(sync_io_core_run_output_reserve(&o));assert(!sync_io_core_gateway_reserve(&g));assert(sync_io_core_run_output_release(&o));
    assert(sync_io_core_legacy_try_enter());assert(!sync_io_core_gateway_reserve(&g));sync_io_core_legacy_leave();
    return 0;
}
'''
    compile_run(tmp_path, code)
