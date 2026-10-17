"""Host execution of the finite switch PIO transaction.

The harness models PIO TX/RX FIFOs and the GPIO/PIO routing registers. It
executes the production C backend for every output mask and code; no board or
host FIRE path is involved.
"""
from pathlib import Path
import os
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/sync_io/src"

MOCK = r'''
#ifndef SWITCH_MOCK_H
#define SWITCH_MOCK_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "sync_io_persona_manager.h"
#define SYNC_IO_CORE_INTERNAL_H
typedef unsigned uint;
#define BOARD_SYS_CLOCK_HZ 250000000u
#define BOARD_SYNC_PIO_FAST (&pio)
#define BOARD_SYNC_OUTPUT_BASE_PIN 16u
#define BOARD_SYNC_OUTPUT_PIN_COUNT 4u
#define BOARD_TDMA_SMA_PIO_BLOCK_ID 1u
#define GPIO_OUT true
#define PADS_BANK0_GPIO0_OD_BITS 0x80u
#define PIO_SM0_PINCTRL_OUT_BASE_BITS 0x1fu
#define PIO_SM0_PINCTRL_OUT_BASE_LSB 0u
#define PIO_SM0_PINCTRL_OUT_COUNT_BITS (0x3fu<<20)
#define PIO_SM0_PINCTRL_OUT_COUNT_LSB 20u
typedef enum { GPIO_FUNC_PIO0=6, GPIO_FUNC_SIO=5 } gpio_function_t;
typedef struct { uint32_t ctrl,irq,dbg_padout,dbg_padoe; struct {uint32_t pinctrl;} sm[4]; } mock_pio;
typedef mock_pio *PIO;
struct pio_program { const uint16_t *instructions; uint length; int origin; uint pio_version; };
typedef struct {uint div,frac,outbase,outcount,wrap_bottom,wrap_top;} pio_sm_config;
static mock_pio pio; static struct {uint32_t gpio_out;} sio; static struct {struct{uint32_t ctrl;}io[30];} io_bank; static struct{uint32_t io[30];} pads;
#define sio_hw (&sio)
#define io_bank0_hw (&io_bank)
#define pads_bank0_hw (&pads)
static uint claimed,loaded,core,allowed_mask;
static bool gate,deny_load,ring_running;
static void *tdma_runtime_owner_get(void){return NULL;}
static bool tdma_service_run_stopped_maintenance(void *owner,bool (*fn)(void *),void *ctx){(void)owner;return !ring_running&&fn(ctx);}
static uint fail_init;
static bool dir[30];
static uint16_t instruction_memory[32];
static struct {
    uint pc,wrap_bottom,wrap_top,tx_count,rx_count;
    uint32_t osr,isr,x,y,tx[4],rx[4];
    uint writes;
    uint64_t last_write_tick;
} machine[4];
static uint64_t tick_count;
static uint32_t raw_reads,origin_cost=8,late_after;
static struct {uint32_t pause,source,raw_hi_dummy,raw_lo_dummy;} timer;
static void advance(uint cycles);
static uint32_t raw_lo(void){++raw_reads;advance(late_after&&raw_reads==2?40000:origin_cost);return (uint32_t)tick_count;}
#define timer1_hw (&timer)
#define timerawh raw_hi_dummy
#define timerawl raw_lo_dummy + raw_lo()
#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 0u
#define clk_sys 0u
static uint clock_get_hz(uint c){(void)c;return BOARD_SYS_CLOCK_HZ;}
enum {pio_y=2,pio_osr=7};

static uint sync_enables,last_enable_mask;
static void check_sm(PIO p,uint s){assert(p==&pio&&(s==1u||s==3u));}
static void check_owned(PIO p,uint s){check_sm(p,s);assert(claimed&(1u<<s));}
static void check_pin(uint pin){assert(pin>=16&&pin<20&&(allowed_mask&(1u<<(pin-16))));}
static uint32_t pop(uint32_t *q,uint *count){assert(*count);uint32_t v=q[0];--*count;memmove(q,q+1,*count*sizeof(*q));return v;}
/* Interpret the actual words copied by pio_add_program. No execution occurs
 * in FIFO getters or enable: the harness advances each hardware clock tick. */
static void advance(uint cycles){
    while(cycles--){
        ++tick_count;
        for(uint s=0;s<4;s++) if(pio.ctrl&(1u<<s)) {
            assert(machine[s].pc<32u);
            const uint16_t ins=instruction_memory[machine[s].pc];
            uint next=machine[s].pc==machine[s].wrap_top?machine[s].wrap_bottom:machine[s].pc+1u;
            const uint op=ins>>13;
            if(op==4u){
                assert(!(ins&0x40u)); /* no conditional pull/push */
                if(ins&0x80u){
                    assert(ins&0x20u);
                    if(!machine[s].tx_count)continue; /* stalled PULL */
                    machine[s].osr=pop(machine[s].tx,&machine[s].tx_count);
                }else{
                    assert(ins&0x20u);
                    if(machine[s].rx_count==4u)continue;
                    machine[s].rx[machine[s].rx_count++]=machine[s].isr;
                    machine[s].isr=0u;
                }
            }else if(op==5u){
                assert(((ins&7u)==7u||(ins&7u)==2u)&&((ins>>3)&3u)==0u);
                const uint32_t value=(ins&7u)==2u?machine[s].y:machine[s].osr;
                const uint dest=(ins>>5)&7u;
                if(dest==0u){
                    const uint route=pio.sm[s].pinctrl;
                    const uint base=route&PIO_SM0_PINCTRL_OUT_BASE_BITS;
                    const uint n=(route&PIO_SM0_PINCTRL_OUT_COUNT_BITS)>>PIO_SM0_PINCTRL_OUT_COUNT_LSB;
                    assert(n&&n<=4&&base>=16&&base+n<=20);
                    const uint32_t mask=((1u<<n)-1u)<<base;
                    assert(!(mask&~(allowed_mask<<16)));
                    pio.dbg_padout=(pio.dbg_padout&~mask)|((value<<base)&mask);
                    machine[s].last_write_tick=tick_count;++machine[s].writes;
                }else{assert(dest==6u);machine[s].isr=value;}
            }else if(op==3u){
                /* OUT dst, 32: the two-word protocol writes Y (code) and X (delay). */
                const uint dest=(ins>>5)&7u;
                assert(dest==1u||dest==2u);
                if(dest==1u)machine[s].x=machine[s].osr;else machine[s].y=machine[s].osr;}
            else if(op==0u){assert(((ins>>5)&7u)==2u);if(machine[s].x-- != 0u)next=ins&31u;}
            else {assert(!"unexpected assembled instruction");}
            machine[s].pc=next;
        }
    }
}
static unsigned get_core_num(void){return core;}
static bool pio_sm_is_claimed(PIO p,uint s){check_sm(p,s);return (claimed&(1u<<s))!=0;}
static void pio_sm_claim(PIO p,uint s){check_sm(p,s);assert(!(claimed&(1u<<s)));claimed|=1u<<s;}
static void pio_sm_unclaim(PIO p,uint s){check_owned(p,s);assert(!(pio.ctrl&(1u<<s)));claimed&=~(1u<<s);}
static bool pio_can_add_program(PIO p,const struct pio_program *g){assert(p==&pio);return !deny_load&&g->length==8;}
static uint pio_add_program(PIO p,const struct pio_program *g){assert(p==&pio&&!loaded&&g->length==8);memcpy(instruction_memory+9,g->instructions,g->length*sizeof(uint16_t));instruction_memory[13]=(instruction_memory[13]&~31u)|13u; /* JMP delay target, absolute */loaded=1;return 9;}
static void pio_remove_program(PIO p,const struct pio_program *g,uint o){assert(p==&pio&&o==9&&loaded);loaded=0;}
static pio_sm_config pio_get_default_sm_config(void){return (pio_sm_config){0};}
static void sm_config_set_wrap(pio_sm_config*c,uint a,uint b){c->wrap_bottom=a;c->wrap_top=b;}
static void sm_config_set_out_pins(pio_sm_config*c,uint b,uint n){c->outbase=b;c->outcount=n;}
static void sm_config_set_out_shift(pio_sm_config*c,bool r,bool a,uint n){assert(r&&!a&&n==32);}
static void sm_config_set_clkdiv_int_frac(pio_sm_config*c,uint d,uint f){c->div=d;c->frac=f;}
static int pio_sm_init(PIO p,uint s,uint o,const pio_sm_config*c){check_owned(p,s);assert(o==9&&c->div==1&&!c->frac);p->sm[s].pinctrl=c->outbase|(c->outcount<<20);machine[s].pc=o;machine[s].wrap_bottom=c->wrap_bottom;machine[s].wrap_top=c->wrap_top;return fail_init==s?-1:0;}
static void pio_sm_set_enabled(PIO p,uint s,bool e){check_owned(p,s);assert(!e);pio.ctrl&=~(1u<<s);}
static void pio_sm_clear_fifos(PIO p,uint s){check_owned(p,s);machine[s].tx_count=0;machine[s].rx_count=0;}
static void pio_sm_restart(PIO p,uint s){check_owned(p,s);machine[s].isr=0;}
static uint pio_encode_jmp(uint x){return x;}
static uint pio_encode_pull(bool c,bool b) __attribute__((unused));
static uint pio_encode_pull(bool c,bool b){assert(!c&&b);return 0x80a0;}
static uint pio_encode_mov(uint d,uint r){return 0xa000|(d<<5)|r;}
static void pio_sm_exec(PIO p,uint s,uint x){check_owned(p,s);assert(!(pio.ctrl&(1u<<s)));
    if(x==9)machine[s].pc=x;
    else if(x==0x80a0)machine[s].osr=pop(machine[s].tx,&machine[s].tx_count);
    else {assert(x==pio_encode_mov(pio_y,pio_osr));machine[s].y=machine[s].osr;}}

static void pio_sm_set_pins_with_mask(PIO p,uint s,uint v,uint m){check_owned(p,s);assert(!(m&~(allowed_mask<<16))&&!(v&~m));p->dbg_padout=(p->dbg_padout&~m)|v;}
static void pio_sm_set_pindirs_with_mask(PIO p,uint s,uint v,uint m){check_owned(p,s);assert(!(m&~(allowed_mask<<16))&&!(v&~m));p->dbg_padoe=(p->dbg_padoe&~m)|v;}
static void pio_sm_put(PIO p,uint s,uint v){check_owned(p,s);assert(machine[s].tx_count<4);machine[s].tx[machine[s].tx_count++]=v;}
static void pio_enable_sm_mask_in_sync(PIO p,uint m){assert(p==&pio&&m&&!(m&~claimed)&&!(pio.ctrl&m));last_enable_mask=m;++sync_enables;pio.ctrl|=m;}
static bool pio_sm_is_rx_fifo_empty(PIO p,uint s){check_owned(p,s);return machine[s].rx_count==0;}
static uint pio_sm_get(PIO p,uint s){check_owned(p,s);return pop(machine[s].rx,&machine[s].rx_count);}
static uint pio_sm_get_tx_fifo_level(PIO p,uint s){check_owned(p,s);return machine[s].tx_count;}
static uint pio_sm_get_rx_fifo_level(PIO p,uint s){check_owned(p,s);return machine[s].rx_count;}
static bool gpio_get_dir(uint p){check_pin(p);return dir[p];}
static void gpio_put(uint p,bool v){check_pin(p);if(v)sio.gpio_out|=1u<<p;else sio.gpio_out&=~(1u<<p);}
static void gpio_set_dir(uint p,bool v){check_pin(p);dir[p]=v;}
static void gpio_set_function(uint p,gpio_function_t f){check_pin(p);io_bank.io[p].ctrl=f;pads.io[p]=(pads.io[p]&~0x180u)|0x40u;}
static bool gpio_get(uint p){
    assert(p<30);const uint fn=io_bank.io[p].ctrl&31u;
    const bool latch=((fn==GPIO_FUNC_PIO0?pio.dbg_padout:sio.gpio_out)&(1u<<p))!=0;
    const bool output=fn==GPIO_FUNC_PIO0?(pio.dbg_padoe&(1u<<p))!=0:dir[p];
    if(output && !(pads.io[p]&PADS_BANK0_GPIO0_OD_BITS))return latch;
    return (pads.io[p]&8u)!=0; /* disconnected test pad follows pull-up */
}
bool sync_io_core_switch_reserve(const void*p){if(gate)return false;gate=true;return true;}
bool sync_io_core_switch_release(const void*p){assert(gate);gate=false;return true;}
static sync_io_persona_manager_hooks_t hooks;
static sync_io_persona_descriptor_t desc={.id=SYNC_IO_PERSONA_ID_SWITCH};
bool sync_io_pio0_runtime_prepare_io(sync_io_persona_id_t id,uint32_t r,uint32_t w,const sync_io_persona_manager_hooks_t*h,void*c,sync_io_persona_manager_handle_t*o){assert(id==SYNC_IO_PERSONA_ID_SWITCH&&!r&&w==(allowed_mask<<16));desc.gpio_write_mask=w;hooks=*h; if(!h->load(0,&desc,0)||!h->arm(0,&desc,0)){h->cleanup(0,&desc,0);return false;}*o=(sync_io_persona_manager_handle_t){.generation=1};return true;}
bool sync_io_pio0_runtime_release(sync_io_persona_manager_handle_t*h){hooks.cleanup(0,&desc,0);return true;}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#endif
'''

HARNESS = r'''
#include "switch_mock.h"
#include SWITCH_SOURCE
static sync_io_switch_snapshot_t get(void){sync_io_switch_snapshot_t s;assert(sync_io_switch_snapshot(&s));return s;}
static struct {uint32_t ctrl[30],pad[30],sio,pio_out,pio_oe;bool direction[30];} saved;
static unsigned transactions,mask_count;
static uint32_t pads_output(void){uint32_t r=0;for(uint i=0;i<4;i++)if(gpio_get(16+i))r|=1u<<i;return r;}
static void initialize(uint mask,uint pattern){
    assert(!claimed&&!loaded&&!gate&&!pio.ctrl);allowed_mask=mask;
    sio.gpio_out=pattern?0x35faac69u:0x56a55a96u;
    pio.dbg_padout=pattern?0x2cb59a63u:0x79fa653au;
    pio.dbg_padoe=pattern?0x64f92cb5u:0x55fab326u;
    for(uint i=0;i<30;i++){
        /* Selected and unselected pads alternate old SIO/PIO0, directions,
         * hidden latches, pulls, IE/OD/ISO, and CTRL override sentinels. */
        io_bank.io[i].ctrl=((i+pattern)&1u?GPIO_FUNC_PIO0:GPIO_FUNC_SIO)|(0x30001000u+i*0x100u);
        pads.io[i]=(i*7u+pattern*37u)&0x1ffu;dir[i]=((i+pattern)&2u)!=0;
        saved.ctrl[i]=io_bank.io[i].ctrl;saved.pad[i]=pads.io[i];saved.direction[i]=dir[i];
    }
    saved.sio=sio.gpio_out;saved.pio_out=pio.dbg_padout;saved.pio_oe=pio.dbg_padoe;
}
static void unchanged_unselected(void){
    const uint physical=allowed_mask<<16;
    assert((sio.gpio_out&~physical)==(saved.sio&~physical));
    assert((pio.dbg_padout&~physical)==(saved.pio_out&~physical));
    assert((pio.dbg_padoe&~physical)==(saved.pio_oe&~physical));
    for(uint i=0;i<30;i++) if(!(physical&(1u<<i))){
        assert(io_bank.io[i].ctrl==saved.ctrl[i]);assert(pads.io[i]==saved.pad[i]);assert(dir[i]==saved.direction[i]);
    }
}
static void restored(void){
    assert(!gate&&!claimed&&!loaded&&!pio.ctrl);
    assert(sio.gpio_out==saved.sio&&pio.dbg_padout==saved.pio_out&&pio.dbg_padoe==saved.pio_oe);
    for(uint i=0;i<30;i++){
        assert(io_bank.io[i].ctrl==saved.ctrl[i]&&pads.io[i]==saved.pad[i]&&dir[i]==saved.direction[i]);
    }
}
static void route_check(sync_io_switch_snapshot_t s,uint mask){
    uint covered=0,active=0,expected_groups=0;
    for(uint i=0;i<4;i++)if((mask&(1u<<i))&&(!i||!(mask&(1u<<(i-1)))))++expected_groups;
    assert(s.group_count==expected_groups&&s.group_count<=2);
    for(uint i=0;i<s.group_count;i++){
        const sync_io_switch_group_t g=s.groups[i];
        assert(g.sm==(i?3u:1u)&&g.output_base_pin>=16&&g.output_count&&g.output_base_pin+g.output_count<=20);
        const uint bits=((1u<<g.output_count)-1u)<<(g.output_base_pin-16);
        assert(!(bits&~mask)&&!(covered&bits));covered|=bits;active|=1u<<g.sm;
        assert(!g.rx_level&&!g.tx_level);
    }
    assert(covered==mask&&s.sm_claimed_mask==10&&s.instruction_words==8&&!s.sm_enabled_mask);
    assert(active==(expected_groups==2?10u:2u));
}
static void execute(uint generation,uint mask,uint code){
    const sync_io_switch_snapshot_t previous=get();const uint original_pad=pads_output();
    const uint32_t sio_before=sio.gpio_out,pio_before=pio.dbg_padout;
    assert(sync_io_switch_stage(generation,code));sync_io_switch_snapshot_t staged=get();
    assert(staged.state==SYNC_IO_SWITCH_STAGED&&staged.desired_code==code&&!staged.receipt_mask);
    assert(staged.stage_sequence==previous.stage_sequence+1&&staged.completed_sequence==previous.completed_sequence);
    assert(sio.gpio_out==sio_before&&pio.dbg_padout==pio_before&&pads_output()==original_pad);
    for(uint i=0;i<staged.group_count;i++)assert(staged.groups[i].tx_level==2&&!staged.groups[i].rx_level);
    unchanged_unselected();uint sync_before=sync_enables;
    assert(sync_io_switch_apply(generation));assert(sync_enables==sync_before+1);
    assert(last_enable_mask==(staged.group_count==2?10u:2u));
    assert(get().state==SYNC_IO_SWITCH_PENDING&&pads_output()==original_pad);
    assert(!sync_io_switch_apply(generation));assert(!sync_io_switch_stage(generation,code));
    assert(!sync_io_switch_release(generation));
    advance(1); /* PULL: the code word leaves the FIFO, no GPIO write yet */
    assert(pads_output()==original_pad&&get().state==SYNC_IO_SWITCH_PENDING);
    advance(1); /* OUT Y: the code is latched, still no edge */
    assert(pads_output()==original_pad);
    advance(1); /* PULL: the delay word leaves the FIFO */
    advance(1); /* OUT X */
    advance(2); /* JMP (delay 0 falls through), MOV PINS, independently masked per group */
    assert((pads_output()&mask)==code);
    assert((pads_output()&(~mask&15u))==(original_pad&(~mask&15u)));
    assert(get().state==SYNC_IO_SWITCH_PENDING);
    for(uint i=0;i<staged.group_count;i++)assert(machine[staged.groups[i].sm].last_write_tick==tick_count);
    unchanged_unselected();advance(1); /* MOV ISR must not publish prematurely */
    assert(get().state==SYNC_IO_SWITCH_PENDING);advance(1); /* PUSH */
    for(uint i=0;i<staged.group_count;i++)assert(machine[staged.groups[i].sm].rx_count==1);
    sync_io_switch_snapshot_t done=get();
    assert(done.state==SYNC_IO_SWITCH_COMPLETE&&done.applied_valid&&done.applied_code==code);
    assert((done.actual_output_mask&mask)==code);
    assert((done.actual_output_mask&(~mask&15u))==(original_pad&(~mask&15u)));
    assert(done.completed_sequence==staged.stage_sequence&&done.receipt_mask==((1u<<done.group_count)-1u));
    assert(!done.sm_enabled_mask&&!pio.ctrl);
    for(uint i=0;i<done.group_count;i++)assert(!done.groups[i].tx_level&&!done.groups[i].rx_level);
    uint writes=machine[1].writes+machine[3].writes;advance(100);
    assert(machine[1].writes+machine[3].writes==writes);assert(!sync_io_switch_apply(generation));
    unchanged_unselected();++transactions;
}
static void all_codes(uint mask){
    initialize(mask,mask&1u);uint32_t g;assert(sync_io_switch_load(mask,&g));
    sync_io_switch_snapshot_t s=get();assert(s.state==SYNC_IO_SWITCH_LOADED&&!(s.actual_output_mask&mask));route_check(s,mask);
    uint before=transactions,ones=0;for(uint i=0;i<4;i++)if(mask&(1u<<i))ones++;
    for(uint code=0;code<16;code++){
        if(code&~mask){assert(!sync_io_switch_stage(g,code));continue;}
        execute(g,mask,code);
    }
    assert(transactions-before==(1u<<ones));
    uint completed=get().completed_sequence;assert(sync_io_switch_stop(g));s=get();
    assert(s.state==SYNC_IO_SWITCH_STOPPED&&!s.applied_valid&&!(s.actual_output_mask&mask)&&s.completed_sequence==completed);
    unchanged_unselected();assert(sync_io_switch_release(g));restored();assert(!get().loaded);++mask_count;
}
static void lifecycle(void){
    uint32_t g;initialize(5u,1u);assert(sync_io_switch_load(5u,&g));
    assert(!sync_io_switch_apply(g));execute(g,5u,5u);execute(g,5u,5u); /* same code explicit new sequence */
    uint done=get().completed_sequence;assert(done==2);
    assert(sync_io_switch_stage(g,0));uint old=get().stage_sequence;
    /* Deliberately stale RX word from a previous transaction must be cleared. */
    machine[1].rx[machine[1].rx_count++]=0xdeadbeefu;
    assert(sync_io_switch_stage(g,1));assert(get().cancelled_sequence==old);
    assert(!machine[1].rx_count&&!machine[3].rx_count);
    assert(sync_io_switch_apply(g));assert(!sync_io_switch_stage(g,4));assert(!sync_io_switch_apply(g));
    assert(sync_io_switch_stop(g));uint cancel=get().cancelled_sequence;
    advance(100);assert(get().completed_sequence==done&&get().cancelled_sequence==cancel&&!get().applied_valid);
    assert(!(get().actual_output_mask&5u));
    assert(sync_io_switch_stage(g,4));assert(sync_io_switch_apply(g));advance(6); /* code+PULL/OUT, OUT X, JMP, MOV PINS: no receipt yet */
    assert((get().actual_output_mask&5u)==4u&&get().state==SYNC_IO_SWITCH_PENDING);
    assert(sync_io_switch_stop(g));advance(100);
    assert(get().completed_sequence==done&&!get().applied_valid&&!(get().actual_output_mask&5u));
    execute(g,5u,1u);unchanged_unselected();assert(sync_io_switch_release(g));restored();
}
static void partial_and_bad_receipt(void){
    uint32_t g;initialize(5u,0u);assert(sync_io_switch_load(5u,&g));
    assert(sync_io_switch_stage(g,5));assert(sync_io_switch_apply(g));
    pio.ctrl&=~8u; /* injected stalled second group: first receipt is insufficient */
    advance(8);assert(get().state==SYNC_IO_SWITCH_PENDING&&get().receipt_mask==1u);
    pio.ctrl|=8u;advance(8);assert(get().state==SYNC_IO_SWITCH_COMPLETE&&get().applied_code==5u);
    assert(sync_io_switch_stage(g,0));assert(sync_io_switch_apply(g));advance(8);
    machine[3].rx[0]^=1u;
    assert(get().state==SYNC_IO_SWITCH_FAULT&&get().fault&&!get().applied_valid);
    assert(get().completed_sequence==1u&&!(get().actual_output_mask&5u));
    assert(!sync_io_switch_stage(g,0));assert(sync_io_switch_stop(g));execute(g,5u,4u);
    assert(sync_io_switch_release(g));restored();
}
static void failures(void){
    initialize(5u,1u);uint32_t g=0x55;
    assert(!sync_io_switch_load(0,&g)&&!sync_io_switch_load(16,&g)&&g==0x55);
    deny_load=true;assert(!sync_io_switch_load(5,&g)&&g==0x55);deny_load=false;restored();
    fail_init=3;assert(!sync_io_switch_load(5,&g)&&g==0x55);fail_init=0;restored();
    assert(sync_io_switch_load(5,&g));assert(!sync_io_switch_stage(g+1,1)&&!sync_io_switch_apply(g+1)&&!sync_io_switch_stop(g+1)&&!sync_io_switch_release(g+1));
    core=1;sync_io_switch_snapshot_t before={.schema=0xbeefu},after=before;
    assert(!sync_io_switch_snapshot(&after)&&!memcmp(&before,&after,sizeof(before)));core=0;
    assert(sync_io_switch_release(g));restored();uint32_t newgen;assert(sync_io_switch_load(5,&newgen)&&newgen>g);
    assert(!sync_io_switch_stage(g,0));assert(sync_io_switch_release(newgen));restored();
}
static void deadline_case(const char *name){
    uint32_t g;initialize(5u,0u);assert(sync_io_switch_load(5u,&g));core=1;
    uint64_t target=tick_count+30000;
    if(!strcmp(name,"deadline_late"))target=tick_count+10;
    if(!strcmp(name,"deadline_origin"))origin_cost=80;
    if(!strcmp(name,"deadline_uncertain"))late_after=1;
    const bool ok=sync_io_switch_realtime_schedule_at(5u,target);
    sync_io_switch_deadline_status_t d;assert(sync_io_switch_realtime_status(&d));
    if(!strcmp(name,"deadline_late")||!strcmp(name,"deadline_origin")||!strcmp(name,"deadline_uncertain")){
        assert(!ok&&!(pio.ctrl&10u));
        assert(d.state==SYNC_IO_SWITCH_DEADLINE_CANCELLED||d.state==SYNC_IO_SWITCH_DEADLINE_UNCERTAIN);
        if(!strcmp(name,"deadline_origin"))assert(d.state==SYNC_IO_SWITCH_DEADLINE_CANCELLED);
        if(!strcmp(name,"deadline_uncertain"))assert(d.state==SYNC_IO_SWITCH_DEADLINE_UNCERTAIN);
    }else{
        assert(ok&&d.state==SYNC_IO_SWITCH_DEADLINE_ARMED&&d.sequence==1);
        assert(d.origin_after_tick-d.origin_before_tick<=64);
        /* SYNC-SCHED-001: a SECOND absolute deadline is ENQUEUED, not rejected; the
         * third is refused because the two-deep hardware queue is full. */
        assert(sync_io_switch_realtime_schedule_at(0u,target+40000));
        core=0;assert(get().pending_count==2);core=1;
        assert(!sync_io_switch_realtime_schedule_at(0u,target+80000));
        core=0;assert(get().pending_count==2);core=1;
        if(!strcmp(name,"deadline_cancel")){
            assert(sync_io_switch_realtime_cancel());advance(40000);
            assert(sync_io_switch_realtime_status(&d)&&d.state==SYNC_IO_SWITCH_DEADLINE_CANCELLED);
            assert(!(pio.dbg_padout&(5u<<16))&&!machine[1].writes&&!machine[3].writes);
            core=0;assert(sync_io_switch_stop(g));assert(sync_io_switch_release(g));restored();return;
        }
        advance((uint)(target-tick_count-1));assert(!(pio.dbg_padout&(5u<<16)));
        advance(4);assert((pio.dbg_padout&(5u<<16))==(5u<<16));
        assert(machine[1].last_write_tick==machine[3].last_write_tick);
        assert(machine[1].last_write_tick>=target&&machine[1].last_write_tick<=target+64);
        advance(3);
        /* RUN Core0 query must not consume either hardware receipt. */
        ring_running=true;core=0;
        assert(get().state==SYNC_IO_SWITCH_PENDING&&machine[1].rx_count==1&&machine[3].rx_count==1);
        core=1;sync_io_switch_realtime_service_core1();ring_running=false;
        /* Trigger 1 completed and its evidence is in the snapshot; the status struct has
         * already moved on to the ARMED trigger 2 (that is the point of a queue: the
         * completion of one does not stop the next). */
        assert(sync_io_switch_realtime_status(&d)&&d.state==SYNC_IO_SWITCH_DEADLINE_ARMED&&
            d.sequence==2);
        core=0;
        assert(get().completed_sequence==1&&get().pending_count==1&&
            get().state==SYNC_IO_SWITCH_PENDING);
        core=1;
        /* The second trigger was already in the hardware queue: it completes on its own
         * deadline with no software re-arm and no new schedule call. */
        advance(40004);sync_io_switch_realtime_service_core1();
        assert(sync_io_switch_realtime_status(&d)&&d.state==SYNC_IO_SWITCH_DEADLINE_COMPLETE&&
            d.sequence==2&&d.receipt_mask==3);
        core=0;
        /* Per-trigger evidence lives in the snapshot: the status struct is the "current"
         * transaction, while completed_sequence/receipt_mask name the one just finished. */
        assert(get().completed_sequence==2&&get().receipt_mask==3&&
            get().pending_count==0&&get().state==SYNC_IO_SWITCH_COMPLETE);
        core=1;
        assert(!(pio.dbg_padout&(5u<<16)));
    }
    core=0;assert(sync_io_switch_stop(g));assert(sync_io_switch_release(g));restored();
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strncmp(argv[1],"deadline",8)){deadline_case(argv[1]);}
    else if(!strcmp(argv[1],"all")){
        for(uint mask=1;mask<16;mask++)all_codes(mask);
        assert(mask_count==15&&transactions==80);printf("masks=%u transactions=%u\n",mask_count,transactions);
    }else if(!strcmp(argv[1],"lifecycle"))lifecycle();
    else if(!strcmp(argv[1],"receipts"))partial_and_bad_receipt();
    else if(!strcmp(argv[1],"failures"))failures();
    else if(!strcmp(argv[1],"restore")){
        for(uint pattern=0;pattern<2;pattern++)for(uint mask=1;mask<16;mask++){
            uint32_t g;initialize(mask,pattern);assert(sync_io_switch_load(mask,&g));execute(g,mask,mask);
            assert(sync_io_switch_stop(g));unchanged_unselected();assert(sync_io_switch_release(g));restored();
        }
    }else {uint mask=(uint)strtoul(argv[1],NULL,10);assert(mask&&mask<16);all_codes(mask);}
    return 0;
}
'''

@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    d=tmp_path_factory.mktemp("switch"); (d/"switch_mock.h").write_text(MOCK,encoding="utf-8")
    for n in ("board_config.h","tdma_runtime_owner.h","hardware/pio.h","hardware/gpio.h","hardware/clocks.h","hardware/timer.h","hardware/structs/sio.h","hardware/structs/io_bank0.h","hardware/structs/pads_bank0.h","hardware/regs/pads_bank0.h","hardware/regs/pio.h","pico/platform.h"):
        p=d/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "switch_mock.h"\n',encoding="utf-8")
    asm=os.environ.get("PIOASM") or shutil.which("pioasm") or str(Path.home()/".pico-sdk/tools/2.2.0/pioasm/pioasm.exe")
    r=subprocess.run([asm,"-o","c-sdk",str(SRC/"sync_switch.pio"),str(d/"sync_switch.pio.h")],capture_output=True,text=True); assert r.returncode==0,r.stderr
    (d/"switch.c").write_text(HARNESS,encoding="utf-8"); exe=d/"switch.exe"
    r=subprocess.run([shutil.which("gcc"),"-std=c11","-O2","-Wall","-Wextra","-Werror","-I"+str(d),"-I"+str(ROOT/"components/sync_io/inc"),"-DSWITCH_SOURCE=\""+str(SRC/"sync_io_switch.c").replace('\\','/')+"\"",str(d/"switch.c"),"-o",str(exe)],capture_output=True,text=True); assert r.returncode==0,r.stderr
    return exe

@pytest.mark.parametrize("case", ["all", "lifecycle", "receipts", "failures", "restore", "deadline", "deadline_late", "deadline_origin", "deadline_uncertain", "deadline_cancel"] + [str(i) for i in range(1,16)])
def test_switch_masks_codes_and_lifecycle(executable,case):
    r=subprocess.run([str(executable),case],capture_output=True,text=True,timeout=10)
    assert r.returncode==0,r.stdout+r.stderr
    if case=="all":
        assert r.stdout.strip()=="masks=15 transactions=80"


def test_switch_reference_resource_budget(tmp_path):
    from test_sync_io_workspace import compile_run
    code = r"""
#include <assert.h>
#include "sync_io_persona_resources.h"
#include "sync_io_switch.h"
int main(void){
    assert(sync_io_persona_catalog_valid());
    const sync_io_persona_descriptor_t *s=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SWITCH);
    const sync_io_persona_descriptor_t *r=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_REFERENCE_MONITOR);
    sync_io_persona_compatibility_t c;
    assert(s->instruction_words==8 && s->sm_mask==10);
    assert(!s->dma_channel_mask && !s->irq_mask && !s->workspace_mask);
    assert(sync_io_persona_compatible(s,r,&c));
    assert(c.instruction_words_total==19);
    return 0;
}
"""
    compile_run(tmp_path, code)
