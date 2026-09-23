"""Real SWITCH persona/runtime ledger and existing scheduler admission gates."""
import re

from test_sync_io_workspace import SRC, compile_run, function


def test_switch_dynamic_masks_reference_coexistence_and_release(tmp_path):
    runtime = (SRC/'sync_io_pio0_runtime.c').read_text(encoding='utf-8').replace('#include "pico.h"', '')
    code = r'''
#include <assert.h>
#include "sync_io_pio0_runtime.h"
#include "sync_io_switch.h"
#include "resource_arbiter.h"
static unsigned get_core_num(void){return 0;}
void osal_critical_enter(void){}
void osal_critical_exit(void){}
static bool load_hook(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{(void)ctx;assert(dma==d->dma_channel_mask);return true;}
''' + runtime + r'''
int main(void){
    resource_arbiter_init();assert(sync_io_persona_catalog_valid());
    const sync_io_persona_descriptor_t *s=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SWITCH);
    const sync_io_persona_descriptor_t *r=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_REFERENCE_MONITOR);
    const sync_io_persona_descriptor_t *c=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_PULSE_COUNTER);
    const sync_io_persona_descriptor_t *g=sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_GATEWAY);
    assert(s->sm_mask==10 && s->instruction_words==4 && !s->gpio_read_mask);
    assert(!s->dma_channel_mask && !s->workspace_mask && !s->irq_mask);
    const sync_io_persona_manager_hooks_t hooks={.load=load_hook};
    sync_io_persona_manager_handle_t a,b,conflict;
    for(unsigned mask=1;mask<16;mask++)for(unsigned reverse=0;reverse<2;reverse++){
        if(reverse)assert(sync_io_pio0_runtime_prepare(r->id,&hooks,NULL,&b));
        assert(sync_io_pio0_runtime_prepare_io(s->id,0,mask<<16,&hooks,NULL,&a));
        const sync_io_persona_descriptor_t *actual=s_runtime.leases[a.slot].descriptor;
        assert(actual==&s_runtime.leases[a.slot].descriptor_value && actual!=s);
        assert(actual->gpio_write_mask==(mask<<16) && !actual->gpio_read_mask);
        assert(actual->safe_low_gpio_mask==(mask<<16) && actual->restore_gpio_mask==(mask<<16));
        assert(actual->sm_mask==s->sm_mask && actual->instruction_words==s->instruction_words);
        assert(!actual->dma_channel_mask && !actual->workspace_mask && !actual->irq_mask);
        if(!reverse)assert(sync_io_pio0_runtime_prepare(r->id,&hooks,NULL,&b));
        assert(!sync_io_pio0_runtime_prepare(c->id,&hooks,NULL,&conflict));
        assert(!sync_io_pio0_runtime_prepare(g->id,&hooks,NULL,&conflict));
        assert(!sync_io_pio0_runtime_prepare_io(s->id,0,mask<<16,&hooks,NULL,&conflict));
        assert(s_runtime.active_count==2 && s_runtime.instruction_words_total==15);
        assert(s_runtime.used_gpio_write_mask==(mask<<16));
        assert(s_runtime.leases[a.slot].descriptor==actual && actual->gpio_write_mask==(mask<<16));
        assert(s_runtime.leases[b.slot].descriptor!=actual && s->gpio_write_mask==(15u<<16));
        assert(sync_io_pio0_runtime_release(&a));
        assert(s_runtime.active_count==1 && s_runtime.instruction_words_total==11 && !s_runtime.used_gpio_write_mask);
        assert(sync_io_pio0_runtime_release(&b));
        assert(!s_runtime.active_count && !s_runtime.instruction_words_total && !s_runtime.used_sm_mask && !s_runtime.pio_resource_held);
    }
    /* Conflicts are symmetric: an existing counter/gateway cannot be
     * displaced by a later SWITCH, even if its output subset is disjoint. */
    for(unsigned i=0;i<2;i++){
        assert(sync_io_pio0_runtime_prepare(i?g->id:c->id,&hooks,NULL,&a));
        assert(!sync_io_pio0_runtime_prepare_io(s->id,0,1u<<16,&hooks,NULL,&conflict));
        assert(s_runtime.active_count==1);assert(sync_io_pio0_runtime_release(&a));
    }
    assert(!sync_io_pio0_runtime_prepare_io(s->id,1u<<23,1u<<16,&hooks,NULL,&a));
    assert(!sync_io_pio0_runtime_prepare_io(s->id,0,0,&hooks,NULL,&a));
    assert(!sync_io_pio0_runtime_prepare_io(s->id,0,1u<<20,&hooks,NULL,&a));
    assert(!s_runtime.active_count && !s_runtime.pio_resource_held);return 0;
}
'''
    compile_run(tmp_path, code, analyzer=True)


def test_switch_gate_reference_and_other_owner_exclusion(tmp_path):
    model = (SRC/'sync_io_model_sched.c').read_text(encoding='utf-8')
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
static uintptr_t s_run_output_token,s_reference_token,s_counter_token,s_gateway_token,s_switch_token;
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
                 'sync_io_core_gateway_release', 'sync_io_core_switch_reserve',
                 'sync_io_core_switch_release'):
        code += function(model, name)
    code += r'''
int main(void){
    static int s,r,c,g,o,bad;
    for(unsigned reverse=0;reverse<2;reverse++){
        if(reverse)assert(sync_io_core_reference_reserve(&r));
        assert(sync_io_core_switch_reserve(&s));
        if(!reverse)assert(sync_io_core_reference_reserve(&r));
        assert(!sync_io_core_legacy_try_enter() && !sync_io_core_run_output_reserve(&o));
        assert(!sync_io_core_pulse_counter_reserve(&c) && !sync_io_core_gateway_reserve(&g));
        assert(!sync_io_core_switch_reserve(&bad) && !sync_io_core_switch_release(&bad));
        assert(sync_io_core_switch_release(&s));assert(sync_io_core_reference_release(&r));
    }
    assert(sync_io_core_pulse_counter_reserve(&c));assert(!sync_io_core_switch_reserve(&s));assert(sync_io_core_pulse_counter_release(&c));
    assert(sync_io_core_gateway_reserve(&g));assert(!sync_io_core_switch_reserve(&s));assert(sync_io_core_gateway_release(&g));
    assert(sync_io_core_run_output_reserve(&o));assert(!sync_io_core_switch_reserve(&s));assert(sync_io_core_run_output_release(&o));
    assert(sync_io_core_legacy_try_enter());assert(!sync_io_core_switch_reserve(&s));sync_io_core_legacy_leave();
    capture=true;assert(!sync_io_core_switch_reserve(&s));capture=false;
    pwm=true;assert(!sync_io_core_switch_reserve(&s));pwm=false;
    core=1;assert(!sync_io_core_switch_reserve(&s));core=0;
    assert(sync_io_core_switch_reserve(&s));assert(sync_io_core_switch_release(&s));return 0;
}
'''
    compile_run(tmp_path, code)
