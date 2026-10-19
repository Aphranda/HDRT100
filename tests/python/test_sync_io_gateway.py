"""PIO0 gateway: real assembly/backend, deadlines and hotload rollback.

The interpreter executes assembled PIO instructions for deadline, pulse and
late READY checks. GPIO lifecycle tests preserve the input and restore the
output function, direction, SIO latch, and both pulls exactly.
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
                 'hardware/gpio.h', 'hardware/timer.h', 'hardware/structs/sio.h', 'hardware/structs/io_bank0.h',
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


@pytest.mark.parametrize('case', ['deadline', 'deadline_ready_high', 'deadline_late', 'deadline_origin', 'deadline_uncertain', 'deadline_cancel', 'deadline_bad_receipt', 'cycle', 'ready_high', 'ready_low', 'restore_high',
                                 'restore_input', 'restore_mux', 'wrong_port', 'pulse',
                                 'core', 'generation', 'gate', 'load_output_fail',
                                 'load_ready_fail', 'init_output_fail', 'init_ready_fail',
                                 'sm_output_busy', 'sm_ready_busy', 'repeated', 'triple', 'stop',
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
enum {pio_x=1,pio_y=2,pio_null=3,pio_isr=6,pio_osr=7};
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
static uint32_t prog_out_len;
static uint32_t deny_load,deny_init;
static bool gate,deny_gate;
static struct {bool dir,level;} pins[30];
static uint input_pin=23u,output_pin=19u;

static struct {uint pc,lo,hi,tx_count,rx_count;uint32_t x,y,osr,isr,tx[4],rx[4];} machine[4];
static uint16_t ins_mem[32];
static uint64_t tick_count,rise_tick,fall_tick;
static uint32_t origin_cost=8,raw_reads,late_after;
static struct {uint32_t pause,source,raw_hi_dummy,raw_lo_dummy;} timer;
#define timer1_hw (&timer)
#define timerawh raw_hi_dummy
#define timerawl raw_lo_dummy + raw_lo()
#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 0u
static uint32_t pop(uint32_t *q,uint *n){assert(*n);uint32_t v=q[0];--*n;memmove(q,q+1,*n*sizeof(*q));return v;}
static void advance(uint cycles){while(cycles--){++tick_count;
    for(uint sm=1;sm<=3;sm+=2)if(peripheral.ctrl&(1u<<sm)){
        const uint16_t ins=ins_mem[machine[sm].pc];uint op=ins>>13;
        uint next=machine[sm].pc==machine[sm].hi?machine[sm].lo:machine[sm].pc+1;
        if(op==0){uint cond=(ins>>5)&7u;uint32_t *v=cond==2?&machine[sm].x:&machine[sm].y;assert(cond==2||cond==4);if((*v)--!=0)next=ins&31u;}
        else if(op==1){uint src=(ins>>5)&3u,pol=(ins>>7)&1u;
            if(src==2){if(!(peripheral.irq&(1u<<(ins&7u))))continue;peripheral.irq&=~(1u<<(ins&7u));}
            else {assert(src==1);if(pins[input_pin].level!=(bool)pol)continue;}}
        else if(op==3){const uint d=(ins>>5)&7u;if(d!=1u&&d!=2u&&d!=6u){printf("OUT dest=%u ins=%04x pc=%u sm=%u\n",d,ins,machine[sm].pc,sm);fflush(stdout);}assert(d==1u||d==2u||d==6u);
            if(d==1u)machine[sm].x=machine[sm].osr;else if(d==2u)machine[sm].y=machine[sm].osr;else machine[sm].isr=machine[sm].osr;}
        else if(op==4){if(ins&0x80u){if(!machine[sm].tx_count)continue;machine[sm].osr=pop(machine[sm].tx,&machine[sm].tx_count);}
            else {if(machine[sm].rx_count==4)continue;machine[sm].rx[machine[sm].rx_count++]=machine[sm].isr;machine[sm].isr=0;}}
        else if(op==5){assert(((ins>>5)&7u)==6);uint32_t v=(ins&7u)==1?machine[sm].x:machine[sm].osr;if(ins&8u)v=~v;machine[sm].isr=v;}
        else if(op==6){assert(!(ins&0x60u));peripheral.irq|=1u<<(ins&7u);}
        else if(op==7){assert(((ins>>5)&7u)==0);if(ins&1u){peripheral.dbg_padout|=1u<<output_pin;rise_tick=tick_count;}else {peripheral.dbg_padout&=~(1u<<output_pin);fall_tick=tick_count;}}
        else assert(!"unexpected PIO opcode");
        machine[sm].pc=next;
    }
}}
static uint32_t raw_lo(void){++raw_reads;advance(late_after&&raw_reads==2?40000:origin_cost);return (uint32_t)tick_count;}
static unsigned get_core_num(void){return core;}
static unsigned clock_get_hz(unsigned clk){assert(clk==0);return hz;}
static void check(PIO p,uint sm){assert(p==BOARD_SYNC_PIO_FAST&&(sm==1||sm==3));}
static bool pio_sm_is_claimed(PIO p,uint sm){check(p,sm);return (claimed&(1u<<sm))!=0;}
static void pio_sm_claim(PIO p,uint sm){check(p,sm);assert(!(claimed&(1u<<sm)));claimed|=1u<<sm;}
static void pio_sm_unclaim(PIO p,uint sm){check(p,sm);assert(claimed&(1u<<sm));claimed&=~(1u<<sm);}
static bool pio_can_add_program(PIO p,const struct pio_program *g){assert(p==BOARD_SYNC_PIO_FAST);return g->length!=deny_load;}
static unsigned pio_add_program(PIO p,const struct pio_program *g){assert(p==BOARD_SYNC_PIO_FAST);add_calls++;loaded_words+=g->length;if(!prog_out_len)prog_out_len=g->length;const uint off=loaded_words==prog_out_len?5u:5u+prog_out_len;
    for(uint i=0;i<g->length;i++){uint16_t ins=g->instructions[i];if((ins>>13)==0)ins=(ins&~31u)|((ins&31u)+off);ins_mem[off+i]=ins;}return off;}
static void pio_remove_program(PIO p,const struct pio_program *g,uint off){(void)off;assert(p==BOARD_SYNC_PIO_FAST);remove_calls++;loaded_words-=g->length;prog_out_len=0;}
static pio_sm_config pio_get_default_sm_config(void){return (pio_sm_config){0};}
static void sm_config_set_wrap(pio_sm_config *c,uint lo,uint hi){c->lo=lo;c->hi=hi;}
static void sm_config_set_set_pins(pio_sm_config *c,uint pin,uint count){assert(pin==output_pin&&count==1);c->set_pin=pin;}
static void sm_config_set_in_pins(pio_sm_config *c,uint pin){assert(pin==input_pin);c->in_pin=pin;}
static void sm_config_set_out_shift(pio_sm_config *c,bool right,bool autopull,uint n){(void)c;assert(right&&!autopull&&n==32);}
static void sm_config_set_clkdiv_int_frac(pio_sm_config *c,uint d,uint f){c->div=d;c->frac=f;}
static int pio_sm_init(PIO p,uint sm,uint off,const pio_sm_config *c){check(p,sm);init_calls++;assert(c->div==1&&!c->frac);if(sm==1)assert(off==5&&c->set_pin==output_pin&&c->lo==5&&c->hi==5+prog_out_len-1u);else assert(off==5+prog_out_len&&c->in_pin==input_pin&&c->lo==5+prog_out_len&&c->hi==c->lo+5u);p->sm[sm].pinctrl=(c->set_pin<<5)|(c->in_pin<<15);machine[sm].pc=off;machine[sm].lo=c->lo;machine[sm].hi=c->hi;return deny_init==sm?-1:0;}
static void pio_sm_set_enabled(PIO p,uint sm,bool enable){check(p,sm);assert(!enable);peripheral.ctrl&=~(1u<<sm);}
static void pio_sm_clear_fifos(PIO p,uint sm){check(p,sm);machine[sm].tx_count=machine[sm].rx_count=0;}
static void pio_sm_restart(PIO p,uint sm){check(p,sm);machine[sm].isr=0;}
static unsigned pio_encode_mov_not(unsigned d,unsigned s){return 0xa000|(d<<5)|8|s;}
static uint pio_encode_jmp(uint d){return d;}
static uint __attribute__((unused)) pio_encode_pull(bool c,bool b){assert(!c&&b);return 0x80a0;}
static uint pio_encode_mov(uint d,uint r){return 0xa000|(d<<5)|r;}
static void pio_sm_exec(PIO p,uint sm,unsigned ins){check(p,sm);assert(!(peripheral.ctrl&(1u<<sm)));
    if(ins<32)machine[sm].pc=ins;
    else if(ins==pio_encode_mov_not(pio_x,pio_null))machine[sm].x=UINT32_MAX;
    else if(ins==0x80a0)machine[sm].osr=pop(machine[sm].tx,&machine[sm].tx_count);
    else if(ins==pio_encode_mov(pio_y,pio_osr))machine[sm].y=machine[sm].osr;
    else {assert(ins==pio_encode_mov(pio_isr,pio_osr));machine[sm].isr=machine[sm].osr;}}
static void pio_sm_put(PIO p,uint sm,uint32_t value){check(p,sm);assert(machine[sm].tx_count<4);machine[sm].tx[machine[sm].tx_count++]=value;}
static void pio_enable_sm_mask_in_sync(PIO p,uint mask){assert(p==BOARD_SYNC_PIO_FAST&&mask==10);peripheral.ctrl|=mask;}
static bool pio_sm_is_rx_fifo_empty(PIO p,uint sm){check(p,sm);return machine[sm].rx_count==0;}
static uint32_t pio_sm_get(PIO p,uint sm){check(p,sm);return pop(machine[sm].rx,&machine[sm].rx_count);}

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
static uint pio_sm_get_tx_fifo_level(PIO p,uint sm){check(p,sm);assert(claimed&(1u<<sm));return machine[sm].tx_count;}
static uint pio_sm_get_rx_fifo_level(PIO p,uint sm){check(p,sm);assert(claimed&(1u<<sm));return machine[sm].rx_count;}
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
#include <stdio.h>
#include "gateway_mock.h"
#include GATEWAY_SOURCE
static void deadline_case(const char *name){
    uint32_t g;assert(sync_io_gateway_load(1,4,1000,&g));core=1;
    uint64_t target=tick_count+30000;
    if(!strcmp(name,"deadline_late"))target=tick_count+10;
    if(!strcmp(name,"deadline_origin"))origin_cost=80;
    if(!strcmp(name,"deadline_uncertain"))late_after=1;
    if(!strcmp(name,"deadline_ready_high"))pins[input_pin].level=true;
    const bool ok=sync_io_gateway_realtime_schedule_at(1000,target);
    sync_io_gateway_deadline_status_t d;assert(sync_io_gateway_realtime_status(&d));
    if(!strcmp(name,"deadline_late")){
        /* Contract (2026-10-19): a tick that is already inside the arming guard still fires, as soon as
         * the PIO can; the lateness is counted instead of failing the group. */
        assert(ok&&d.state==SYNC_IO_GATEWAY_DEADLINE_ARMED&&d.sequence==1);
        advance(40000);assert(rise_tick!=0);   /* the clamp arms at now + guard */
    }else if(!strcmp(name,"deadline_origin")||!strcmp(name,"deadline_uncertain")){
        assert(!ok&&!(peripheral.ctrl&10u));
        assert(d.state==(!strcmp(name,"deadline_uncertain")?SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN:SYNC_IO_GATEWAY_DEADLINE_CANCELLED));
    }else{
        assert(ok&&d.state==SYNC_IO_GATEWAY_DEADLINE_ARMED&&d.sequence==1&&d.pending);
        assert(d.origin_after_tick-d.origin_before_tick<=64);
        assert(!sync_io_gateway_realtime_schedule_at(1000,target+40000));
        if(!strcmp(name,"deadline_cancel")){
            assert(sync_io_gateway_realtime_cancel());advance(40000);
            assert(sync_io_gateway_realtime_status(&d)&&d.state==SYNC_IO_GATEWAY_DEADLINE_CANCELLED&&!d.pending&&!rise_tick);
        }else{
            advance((uint)(target-tick_count-1));assert(!rise_tick);
            advance(4);assert(rise_tick>=target&&rise_tick<=target+64);
            advance(260);assert(fall_tick-rise_tick==250); /* exact configured width */
            if(!strcmp(name,"deadline_bad_receipt"))machine[1].rx[0]^=1;
            sync_io_gateway_realtime_service_core1();assert(sync_io_gateway_realtime_status(&d));
            if(!strcmp(name,"deadline_bad_receipt"))assert(d.state==SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN&&!d.pending);
            else{
                assert(d.state==SYNC_IO_GATEWAY_DEADLINE_COMPLETE&&d.completed_sequence==1&&!d.pending&&d.ready_count==0);
                /* READY arrives after the pulse's output receipt was drained. */
                pins[input_pin].level=false;advance(2);pins[input_pin].level=true;advance(5);
                sync_io_gateway_realtime_service_core1();assert(sync_io_gateway_realtime_status(&d)&&d.ready_count==1);
                target=tick_count+30000;assert(sync_io_gateway_realtime_schedule_at(8,target));
                advance(30020);sync_io_gateway_realtime_service_core1();
                assert(sync_io_gateway_realtime_status(&d)&&d.completed_sequence==2&&!d.pending);
                assert(fall_tick-rise_tick==2); /* minimum pulse stays exact */
            }
        }
    }
    core=0;assert(sync_io_gateway_stop(g));assert(sync_io_gateway_release(g));
}
static sync_io_gateway_snapshot_t snapshot(void){sync_io_gateway_snapshot_t s;assert(sync_io_gateway_snapshot(&s));return s;}
int main(int argc,char **argv){
    assert(argc==2);const char *test=argv[1];uint32_t g=99;
    if(!strncmp(test,"deadline",8)){deadline_case(test);return 0;}
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
    if(!strcmp(test,"load_output_fail"))deny_load=12;
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
    assert(sync_io_gateway_load(inport,outport,1000,&g));assert(g==1&&gate&&claimed==10&&loaded_words==18);
    sync_io_gateway_snapshot_t s=snapshot();
    assert(s.loaded&&s.state==SYNC_IO_GATEWAY_LOADED&&s.input_pin==input_pin&&s.output_pin==output_pin&&s.pulse_ticks==250);
    assert(s.sm_claimed_mask==10&&!s.sm_enabled_mask&&s.instruction_words==18&&s.output_program_offset==5&&s.ready_program_offset==17);
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
    if(!strcmp(test,"triple")){
        uint32_t g;assert(sync_io_gateway_load(1,4,1000,&g));core=1;
        for(uint32_t i=0u;i<3u;++i){
            /* 60 us out: comfortably beyond the arming guard, so the late clamp cannot move it. */
            const uint64_t target=tick_count+60000;
            assert(sync_io_gateway_realtime_schedule_at(1000,target));
            advance(70000);
            sync_io_gateway_realtime_service_core1();
            sync_io_gateway_deadline_status_t d;assert(sync_io_gateway_realtime_status(&d));
            assert(d.state==SYNC_IO_GATEWAY_DEADLINE_COMPLETE);
            assert(d.completed_sequence==i+1u);
        }
        return 0;
    }
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
    assert(g->sm_mask==10&&g->instruction_words==18&&g->gpio_read_mask==(1u<<23)&&g->gpio_write_mask==(1u<<19));
    assert(!g->dma_channel_mask&&!g->workspace_mask&&g->irq_mask==SYNC_IO_PERSONA_IRQ_INTERNAL_6);
    assert(sync_io_persona_compatible(g,r,&p));assert(!sync_io_persona_compatible(g,c,&p)&&(p.conflict_mask&SYNC_IO_PERSONA_CONFLICT_SM));
    const sync_io_persona_manager_hooks_t hooks={.load=load_hook};sync_io_persona_manager_handle_t a,b,conflict;
    for(unsigned reverse=0;reverse<2;reverse++){
        assert(sync_io_pio0_runtime_prepare(reverse?g->id:r->id,&hooks,NULL,&a));
        assert(sync_io_pio0_runtime_prepare(reverse?r->id:g->id,&hooks,NULL,&b));
        assert(!sync_io_pio0_runtime_prepare(c->id,&hooks,NULL,&conflict));
        assert(s_runtime.active_count==2&&s_runtime.instruction_words_total==29);
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
        assert(s_runtime.leases[b.slot].descriptor!=actual&&s_runtime.instruction_words_total==29);
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
static uintptr_t s_turntable_source_token,s_run_output_token,s_reference_token,s_counter_token,s_gateway_token;
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
