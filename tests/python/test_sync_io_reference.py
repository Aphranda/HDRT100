"""Reference arithmetic and assembled PIO counting, without physical I/O."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import pytest

ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'components/sync_io/src'

@pytest.fixture(scope='module')
def backend(tmp_path_factory):
    d=tmp_path_factory.mktemp('reference-backend')
    (d/'reference_mocks.h').write_bytes((ROOT/'tests/unit/test_sync_io_reference_mocks.h').read_bytes())
    for name in ('board_config.h','hardware/clocks.h','hardware/dma.h','hardware/gpio.h',
                 'hardware/pio.h','hardware/timer.h','pico/platform.h','pico/types.h'):
        p=d/name;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text('#include "reference_mocks.h"\n')
    asm=os.environ.get('PIOASM') or shutil.which('pioasm') or str(
        Path.home()/'.pico-sdk/tools/2.2.0/pioasm/pioasm.exe')
    result=subprocess.run([asm,'-o','c-sdk',str(SRC/'sync_reference_cycles.pio'),
                           str(d/'sync_reference_cycles.pio.h')],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    exe=d/'backend.exe'
    result=subprocess.run([shutil.which('gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(d),'-I'+str(ROOT/'components/sync_io/inc'),
        '-DREFERENCE_SOURCE="'+(SRC/'sync_io_reference.c').as_posix()+'"',
        str(ROOT/'tests/unit/test_sync_io_reference_backend.c'),'-o',str(exe)],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    return exe

@pytest.mark.parametrize('case',['admission','cancel_prepared','timeout','clock','dma','three_windows'])
def test_backend_dma_pairing_and_lifetime(backend,case):
    result=subprocess.run([str(backend),case],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr

def test_reference_persona_compatible_with_run(tmp_path):
    from test_sync_io_workspace import compile_run
    compile_run(tmp_path,r'''
#include <assert.h>
#include "sync_io_persona_resources.h"
int main(void) {
    assert(sync_io_persona_catalog_valid());
    const sync_io_persona_descriptor_t *r=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_REFERENCE_MONITOR);
    const sync_io_persona_descriptor_t *o=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER);
    sync_io_persona_compatibility_t c;
    assert(sync_io_persona_descriptor_valid(r));
    assert(r->dma_channel_count==2 && r->dma_channel_mask==((1u<<9)|(1u<<10)) && r->workspace_mask==0);
    assert(sync_io_persona_compatible(r,o,&c));
    assert(c.conflict_mask==0);
    return 0;
}
''')

def test_shared_runtime_real_manager_coexistence_and_rollback(tmp_path):
    from test_sync_io_workspace import compile_run
    p=tmp_path/'pico/platform.h';p.parent.mkdir()
    p.write_text('extern unsigned mock_core;\nstatic inline unsigned get_core_num(void){return mock_core;}\n')
    # compile_run has a fixed include set; the production runtime include
    # resolves platform via the temporary shim placed in its direct include.
    runtime=(SRC/'sync_io_pio0_runtime.c').read_text().replace(
        '#include "pico/platform.h"','#include "'+p.as_posix()+'"').replace(
        '#include "pico.h"','#include "'+p.as_posix()+'"')
    harness=r'''
#include <assert.h>
#include <stdbool.h>
#include "sync_io_pio0_runtime.h"
#include "resource_arbiter.h"
unsigned mock_core;
void osal_critical_enter(void){}
void osal_critical_exit(void){}
static unsigned loads[SYNC_IO_PERSONA_ID_COUNT],cleanups[SYNC_IO_PERSONA_ID_COUNT];
static bool deny;
static bool load_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma){
    assert(ctx==(void *)(uintptr_t)d->id);assert(dma==d->dma_channel_mask);
    loads[d->id]++;return !deny;
}
static bool arm_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma){
    (void)dma;assert(ctx==(void *)(uintptr_t)d->id);return true;
}
static void cleanup_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma){
    (void)dma;assert(ctx==(void *)(uintptr_t)d->id);cleanups[d->id]++;
}
'''+runtime+r'''
int main(void){
    const sync_io_persona_manager_hooks_t h={.load=load_hook,.arm=arm_hook,.cleanup=cleanup_hook};
    const sync_io_persona_id_t r=SYNC_IO_PERSONA_ID_REFERENCE_MONITOR,o=SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER;
    sync_io_persona_manager_handle_t a,b;
    resource_arbiter_init();
    for(unsigned reverse=0;reverse<2;reverse++) {
        const sync_io_persona_id_t first=reverse?r:o,second=reverse?o:r;
        assert(sync_io_pio0_runtime_prepare(first,&h,(void *)(uintptr_t)first,&a));
        assert(!sync_io_pio0_runtime_prepare(first,&h,(void *)(uintptr_t)first,&b));
        deny=true;assert(!sync_io_pio0_runtime_prepare(second,&h,(void *)(uintptr_t)second,&b));deny=false;
        assert(s_runtime.active_count==1 && s_runtime.pio_resource_held);
        assert(sync_io_pio0_runtime_prepare(second,&h,(void *)(uintptr_t)second,&b));
        assert(s_runtime.active_count==2 && s_runtime.used_dma_channel_mask==(SYNC_IO_REFERENCE_DMA_MASK|4u));
        mock_core=1;assert(sync_io_pio0_runtime_start_core1(reverse?&b:&a));
        assert(!sync_io_pio0_runtime_release(&a));mock_core=0;
        assert(sync_io_pio0_runtime_release(&a));assert(s_runtime.pio_resource_held);
        assert(sync_io_pio0_runtime_release(&b));assert(!s_runtime.pio_resource_held);
    }
    assert(loads[r]==3&&loads[o]==3&&cleanups[r]==3&&cleanups[o]==3);
    return 0;
}
'''
    compile_run(tmp_path,harness,analyzer=True)

def test_reference_gate_excludes_legacy_but_allows_run(tmp_path):
    from test_sync_io_workspace import compile_run,function
    model=(SRC/'sync_io_model_sched.c').read_text(encoding='utf-8')
    enum=re.search(r'typedef enum \{\n    SYNC_IO_SCHEDULE_IDLE.*?} sync_io_schedule_phase_t;',model,re.S)[0]
    harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define __not_in_flash_func(n) n
static unsigned core;
static bool initialized=true,capture,sequence,encoder,pwm;
static struct {bool running;} s_model_pulse;
static bool s_wave_output_manager_active,s_wave_output_sm_claimed;
static bool s_wave_output_dma_claimed,s_wave_output_program_loaded;
static uint32_t s_schedule_phase;
static uintptr_t s_run_output_token,s_reference_token;
#define get_core_num() core
#define sync_io_core_initialized() initialized
#define sync_io_core_capture_is_running() capture
#define sync_io_seq_step_is_running() sequence
#define sync_io_enc_count_is_running() encoder
#define sync_io_core_sma_frequency_output_active() pwm
'''+enum+'\n'
    for name in ('sync_io_schedule_reserve','sync_io_schedule_publish_phase',
                 'sync_io_core_legacy_try_enter','sync_io_core_legacy_leave',
                 'sync_io_core_run_output_reserve','sync_io_core_run_output_held',
                 'sync_io_core_run_output_release','sync_io_core_reference_reserve',
                 'sync_io_core_reference_release'):
        harness+=function(model,name)
    harness+=r'''
int main(void){
    static int r,o,wrong;
    assert(sync_io_core_reference_reserve(&r));
    assert(!sync_io_core_legacy_try_enter());
    assert(!sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,SYNC_IO_SCHEDULE_FIXED_PREPARING));
    assert(sync_io_core_run_output_reserve(&o));
    assert(!sync_io_core_reference_release(&wrong));
    core=1;assert(!sync_io_core_reference_release(&r));core=0;
    assert(sync_io_core_reference_release(&r));
    assert(!sync_io_core_legacy_try_enter());
    assert(sync_io_core_run_output_release(&o));
    assert(sync_io_core_legacy_try_enter());
    assert(!sync_io_core_reference_reserve(&r));sync_io_core_legacy_leave();
    assert(sync_io_core_run_output_reserve(&o));
    assert(sync_io_core_reference_reserve(&r));
    assert(sync_io_core_run_output_release(&o));
    assert(!sync_io_core_legacy_try_enter());
    assert(sync_io_core_reference_release(&r));
    assert(sync_io_core_legacy_try_enter());sync_io_core_legacy_leave();
    return 0;
}
'''
    compile_run(tmp_path,harness)

def test_frequency_arithmetic(tmp_path):
    exe=tmp_path/'math.exe'
    command=[shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror',
             '-I'+str(ROOT/'components/sync_io/inc'),
             str(ROOT/'tests/unit/test_sync_io_reference_math.c'),'-o',str(exe)]
    result=subprocess.run(command,capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    assert result.returncode==0,result.stderr

@pytest.fixture(scope='module')
def program(tmp_path_factory):
    output=tmp_path_factory.mktemp('reference-pio')/'sync_reference_cycles.pio.h'
    asm=os.environ.get('PIOASM') or shutil.which('pioasm') or str(
        Path.home()/'.pico-sdk/tools/2.2.0/pioasm/pioasm.exe')
    result=subprocess.run([asm,'-o','c-sdk',str(SRC/'sync_reference_cycles.pio'),
                           str(output)],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    body=output.read_text().split('sync_reference_cycles_program_instructions[] = {')[1].split('};')[0]
    return [int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{4}),',body)]

@pytest.mark.parametrize('periods',[1,2,17,100])
@pytest.mark.parametrize('falling',[False,True])
@pytest.mark.parametrize('period',[13,25,250])
def test_actual_pio_counts_exact_periods_and_then_stalls(program,periods,falling,period):
    code=program[:]
    if falling:
        for i in (2,3,6,7):code[i]^=0x80
    pc=x=isr=0
    tx=[periods-1]
    tokens=[]
    edges=[]
    for tick in range((periods+4)*period):
        pin=int(tick%period>=period//2)
        if tick and pin!=int((tick-1)%period>=period//2) and pin==int(not falling):
            edges.append(tick)
        ins=code[pc];op=ins>>13;next_pc=(pc+1)%len(code)
        if op==4: # pull or push
            if ins&0x80:
                if not tx:continue
                osr=tx.pop(0)
            else:tokens.append((tick,isr))
        elif op==3: # out x,32
            assert (ins>>5)&7==1 and ins&31==0
            x=osr
        elif op==1: # wait pin
            assert (ins>>5)&3==1
            if pin!=((ins>>7)&1):continue
        elif op==5: # mov isr,x
            assert (ins>>5)&7==6 and ins&7==1
            isr=x
        elif op==0: # jmp x-- always decrements
            assert (ins>>5)&7==2
            take=x!=0;x=(x-1)&0xffffffff
            if take:next_pc=ins&31
        else:raise AssertionError(hex(ins))
        pc=next_pc
    assert len(tokens)==2 and not tx and pc==0
    assert tokens[0][1]==periods-1 and tokens[1][1]==0xffffffff
    assert tokens[1][0]-tokens[0][0]==periods*period+1
    assert tokens[0][0]==edges[0]+2
    assert tokens[1][0]==edges[periods]+3
