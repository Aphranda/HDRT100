"""Actual countdown PIO + production finite action backend, no hardware.

Reuse the independently tested GPIO/FIFO model; replace only instruction
semantics for the assembled timed program. No timer expiry fabricates receipts.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import pytest
from test_sync_io_switch import MOCK as SWITCH_MOCK

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'components/sync_io/src'


def replace_function(source,name,body):
    match=re.search(r'(?m)^(?:static )?[^\n]*\b'+name+r'\([^\n]*\)\{',source)
    assert match,name
    start=source.index('{',match.start());depth=1;end=start+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[:match.start()]+body+source[end:]


MOCK=SWITCH_MOCK.replace('uint32_t osr,isr,tx[4],rx[4];','uint32_t osr,isr,x,y,tx[4],rx[4];')
MOCK=replace_function(MOCK,'pio_sm_get_rx_fifo_level','')
MOCK=replace_function(MOCK,'pio_sm_get_tx_fifo_level','')
MOCK=MOCK.replace('g->length==4','g->length==8').replace('SYNC_IO_PERSONA_ID_SWITCH','SYNC_IO_PERSONA_ID_ACTION')
MOCK=MOCK.replace('&&!r&&w==(allowed_mask<<16)','&&w==(allowed_mask<<16)')
MOCK=MOCK.replace('#define BOARD_SYS_CLOCK_HZ 250000000u',r'''
#define BOARD_SYS_CLOCK_HZ 250000000u
#define BOARD_SYNC_INPUT_BASE_PIN 20u
#define BOARD_SYNC_INPUT_PIN_COUNT 4u
#define BOARD_SYNC_INPUT_BITS_REVERSED 1u
#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 1u
#define clk_sys 0u
static unsigned mock_hz=BOARD_SYS_CLOCK_HZ;
static struct {uint32_t timerawh,timerawl,pause,source;} timer={.source=1};
#define timer1_hw (&timer)
static unsigned clock_get_hz(unsigned clk){(void)clk;return mock_hz;}
''')
MOCK=replace_function(MOCK,'advance',r'''
static void advance(uint cycles){
 while(cycles){
    if(!pio.ctrl){tick_count+=cycles;timer.timerawl=(uint32_t)tick_count;timer.timerawh=(uint32_t)(tick_count>>32);return;}
    /* Fast-forward only when every enabled SM is at its actual JMP X--.
     * Exactly min(X) taken iterations are equivalent; X==0 fallthrough is
     * still interpreted one instruction at a time. */
    uint skip=cycles;bool all=pio.ctrl!=0;
    for(uint s=0;s<4;s++)if(pio.ctrl&(1u<<s)){
        if((instruction_memory[machine[s].pc]>>13)!=0 || !machine[s].x){all=false;break;}
        if(machine[s].x<skip)skip=machine[s].x;
    }
    if(all&&skip){
        for(uint s=0;s<4;s++)if(pio.ctrl&(1u<<s))machine[s].x-=skip;
        tick_count+=skip;cycles-=skip;
        timer.timerawl=(uint32_t)tick_count;timer.timerawh=(uint32_t)(tick_count>>32);
        continue;
    }
    ++tick_count;--cycles;timer.timerawl=(uint32_t)tick_count;timer.timerawh=(uint32_t)(tick_count>>32);
    for(uint s=0;s<4;s++)if(pio.ctrl&(1u<<s)){
        uint ins=instruction_memory[machine[s].pc],op=ins>>13;
        uint next=machine[s].pc==machine[s].wrap_top?machine[s].wrap_bottom:machine[s].pc+1;
        if(op==4){
            if(ins&0x80){if(!machine[s].tx_count)continue;machine[s].osr=pop(machine[s].tx,&machine[s].tx_count);}
            else {if(machine[s].rx_count==4)continue;machine[s].rx[machine[s].rx_count++]=machine[s].isr;machine[s].isr=0;}
        }else if(op==3){assert(((ins>>5)&7)==1&&!(ins&31));machine[s].x=machine[s].osr;machine[s].osr=0;}
        else if(op==0){assert(((ins>>5)&7)==2);bool take=machine[s].x!=0;--machine[s].x;if(take)next=ins&31;}
        else if(op==5){
            uint src=ins&7,dest=(ins>>5)&7;assert(!((ins>>3)&3));
            uint value=src==7?machine[s].osr:machine[s].y;assert(src==7||src==2);
            if(dest==2)machine[s].y=value;
            else if(dest==6)machine[s].isr=value;
            else {
                assert(dest==0);uint route=pio.sm[s].pinctrl,base=route&31,n=(route>>20)&63;
                uint mask=((1u<<n)-1u)<<base;assert(!(mask&~(allowed_mask<<16)));
                pio.dbg_padout=(pio.dbg_padout&~mask)|((value<<base)&mask);
                machine[s].last_write_tick=tick_count;machine[s].writes++;
            }
        }else assert(!"unexpected opcode");
        machine[s].pc=next;
    }
 }
}
''')
# PIOASM emits a relative JMP, SDK relocates it when loading at offset nine.
MOCK=replace_function(MOCK,'pio_add_program',r'''
static uint pio_add_program(PIO p,const struct pio_program*g){
 assert(p==&pio&&!loaded&&g->length==8);
 for(uint i=0;i<g->length;i++){
    uint ins=g->instructions[i];if((ins>>13)==0)ins=(ins&~31u)|((ins&31u)+9u);
    instruction_memory[9+i]=(uint16_t)ins;
 }
 loaded=1;return 9;
}
''')
MOCK=replace_function(MOCK,'pio_sm_put',r'''
static void pio_sm_put(PIO p,uint s,uint v){
 check_owned(p,s);assert(!(pio.ctrl&(1u<<s))&&machine[s].tx_count<4);
 advance(7);machine[s].tx[machine[s].tx_count++]=v;
}
''')
MOCK=replace_function(MOCK,'pio_enable_sm_mask_in_sync',r'''
static void pio_enable_sm_mask_in_sync(PIO p,uint m){
 assert(p==&pio&&m&&!(m&~claimed)&&!(pio.ctrl&m));advance(3);
 last_enable_mask=m;++sync_enables;pio.ctrl|=m;
}
''')


@pytest.fixture(scope='module')
def backend(tmp_path_factory):
    d=tmp_path_factory.mktemp('action');(d/'action_mock.h').write_text(MOCK,encoding='utf-8')
    for n in ('board_config.h','hardware/clocks.h','hardware/timer.h','hardware/pio.h','hardware/gpio.h',
              'hardware/structs/sio.h','hardware/structs/io_bank0.h','hardware/structs/pads_bank0.h',
              'hardware/regs/pads_bank0.h','pico/platform.h'):
        p=d/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "action_mock.h"\n',encoding='utf-8')
    asm=os.environ.get('PIOASM') or shutil.which('pioasm') or str(Path.home()/'.pico-sdk/tools/2.2.0/pioasm/pioasm.exe')
    r=subprocess.run([asm,'-o','c-sdk',str(SRC/'sync_action.pio'),str(d/'sync_action.pio.h')],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
    (d/'action.c').write_text(HARNESS,encoding='utf-8');exe=d/'action.exe'
    r=subprocess.run([shutil.which('gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(d),
        '-I'+str(ROOT/'components/sync_io/inc'),'-DACTION_SOURCE="'+(SRC/'sync_io_action.c').as_posix()+'"',
        str(d/'action.c'),'-o',str(exe)],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
    return exe


@pytest.mark.parametrize('case',['switch','gateway','cancel','late','too_far','callback',
                                'bad_receipt','clock','restore','invalid'])
def test_finite_action_actual_pio(backend,case):
    r=subprocess.run([str(backend),case],capture_output=True,text=True,timeout=15)
    assert r.returncode==0,r.stdout+r.stderr


HARNESS=r'''
#include "action_mock.h"
#include ACTION_SOURCE
static sync_io_action_snapshot_t snap(void){sync_io_action_snapshot_t s;assert(sync_io_action_snapshot(&s));return s;}
static bool validate(void*ctx){return *(bool*)ctx;}
static void run(uint mask,uint code,bool gateway){
 allowed_mask=mask;uint32_t g;
 const sync_io_action_config_t c={.kind=gateway?2:1,.output_mask=mask,.code=code,.input_port=gateway?1:0,.pulse_ns=gateway?1000:0};
 pio.dbg_padout=0x56a95a5a;pio.dbg_padoe=0xdeadbeef;sio.gpio_out=0xabcd1357;
 uint oldout=pio.dbg_padout,oldoe=pio.dbg_padoe,oldsio=sio.gpio_out;
 uint ctrl[4],pad[4];bool olddir[4];
 for(uint i=0;i<4;i++){io_bank.io[16+i].ctrl=(i&1?6u:5u)|0x30010000u;pads.io[16+i]=(i*13u)|0x180u;dir[16+i]=i&1;ctrl[i]=io_bank.io[16+i].ctrl;pad[i]=pads.io[16+i];olddir[i]=dir[16+i];}
 assert(sync_io_action_load(&c,&g));assert(snap().state==SYNC_IO_ACTION_LOADED&&!pio.ctrl);
 uint64_t origin=tick_count,deadline=origin+UINT64_C(1250000000); /* 5 seconds */
 bool valid=true;assert(sync_io_action_arm_checked(g,deadline,validate,&valid));
 sync_io_action_snapshot_t s=snap();assert(s.state==SYNC_IO_ACTION_ARMED&&!s.receipts&&!s.ready_qualified);
 assert(!sync_io_action_arm(g,deadline)&&!sync_io_action_release(g));
 assert(s.enable_before_tick==origin&&s.enable_after_tick>origin);
 const uint64_t enable=tick_count;
 const uint64_t expected=deadline+enable-origin+1u; /* next tick starts PULL */
 assert(expected>=deadline&&expected<=deadline+s.enable_after_tick-s.enable_before_tick);
 advance((uint)(expected-tick_count-1u));assert(snap().state==SYNC_IO_ACTION_ARMED&&!snap().receipts);
 assert(!(pio.dbg_padout&(mask<<16)));advance(1);
 assert(((pio.dbg_padout>>16)&mask)==code);
 for(uint i=0;i<s.group_count;i++)assert(machine[i?3:1].last_write_tick==expected);
 assert(snap().state==SYNC_IO_ACTION_ARMED);advance(2);
 if(gateway){
    assert(snap().receipts==1&&snap().state==SYNC_IO_ACTION_ARMED);
    advance(247);assert(pio.dbg_padout&(mask<<16));advance(1);
    assert(!(pio.dbg_padout&(mask<<16)));assert(machine[1].last_write_tick==expected+250);
    advance(2);
 }
 s=snap();assert(s.state==SYNC_IO_ACTION_COMPLETE&&s.receipts==s.expected_receipts&&!pio.ctrl);
 assert(s.completed_observed_tick>=expected&&s.ready_qualified==0);
 assert((pio.dbg_padout&~(mask<<16))==(oldout&~(mask<<16)));
 uint writes=machine[1].writes+machine[3].writes;advance(1000);assert(writes==machine[1].writes+machine[3].writes);
 assert(sync_io_action_release(g));assert(!gate&&!loaded&&!claimed);
 assert(pio.dbg_padout==oldout&&pio.dbg_padoe==oldoe&&sio.gpio_out==oldsio);
 for(uint i=0;i<4;i++)assert(ctrl[i]==io_bank.io[16+i].ctrl&&pad[i]==pads.io[16+i]&&olddir[i]==dir[16+i]);
}
int main(int argc,char**argv){
 assert(argc==2);const char*c=argv[1];
 if(!strcmp(c,"switch")||!strcmp(c,"restore")){
    uint n=0;for(uint m=1;m<16;m++)for(uint v=0;v<16;v++)if(!(v&~m)){run(m,v,false);++n;}assert(n==80);return 0;
 }
 if(!strcmp(c,"gateway")){for(uint i=0;i<4;i++)run(1u<<i,1u<<i,true);return 0;}
 allowed_mask=5;sync_io_action_config_t cfg={.kind=1,.output_mask=5,.code=5};uint32_t g=0;
 if(!strcmp(c,"invalid")){
    cfg.output_mask=0;assert(!sync_io_action_load(&cfg,&g));cfg.output_mask=5;cfg.code=2;
    assert(!sync_io_action_load(&cfg,&g));cfg.code=5;cfg.kind=2;
    assert(!sync_io_action_load(&cfg,&g));assert(!claimed&&!gate);return 0;
 }
 assert(sync_io_action_load(&cfg,&g));uint64_t deadline=tick_count+1250000000;
 if(!strcmp(c,"late")){assert(!sync_io_action_arm(g,tick_count+1));assert(!pio.ctrl&&snap().state==SYNC_IO_ACTION_LOADED);}
 else if(!strcmp(c,"too_far")){assert(!sync_io_action_arm(g,tick_count+UINT64_C(0x100000100)));assert(!pio.ctrl);}
 else if(!strcmp(c,"clock")){timer.pause=1;assert(!sync_io_action_arm(g,deadline)&&!pio.ctrl);timer.pause=0;}
 else if(!strcmp(c,"callback")){bool valid=false;assert(!sync_io_action_arm_checked(g,deadline,validate,&valid));assert(!pio.ctrl&&!machine[1].tx_count&&!machine[3].tx_count);}
 else if(!strcmp(c,"bad_receipt")){
    assert(sync_io_action_arm(g,deadline));advance(1250000100);machine[1].rx[0]^=1;
    assert(snap().state==SYNC_IO_ACTION_FAULT&&!pio.ctrl);
 }else {
    assert(sync_io_action_arm(g,deadline));advance(250000000);assert(sync_io_action_stop(g));
    uint writes=machine[1].writes+machine[3].writes;advance(1250000000);
    assert(snap().state==SYNC_IO_ACTION_CANCELLED&&!snap().receipts&&writes==machine[1].writes+machine[3].writes);
 }
 assert(sync_io_action_stop(g));assert(sync_io_action_release(g));return 0;
}
'''
