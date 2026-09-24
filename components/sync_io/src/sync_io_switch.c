#include "sync_io_switch.h"
#include <string.h>
#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/pio.h"
#include "pico/platform.h"
#include "sync_io_core_internal.h"
#include "sync_io_pio0_runtime.h"
#include "sync_switch.pio.h"

#define SWITCH_PIO BOARD_SYNC_PIO_FAST
#define SWITCH_SM_MASK ((1u<<SYNC_IO_SWITCH_FIRST_SM)|(1u<<SYNC_IO_SWITCH_SECOND_SM))
#define OUTPUT_MASK ((1u<<BOARD_SYNC_OUTPUT_PIN_COUNT)-1u)
_Static_assert(BOARD_SYNC_OUTPUT_PIN_COUNT==4u,"switch grouping covers four public outputs");
_Static_assert(sizeof(sync_switch_program_instructions)/sizeof(uint16_t)==
    SYNC_IO_SWITCH_INSTRUCTION_WORDS,"switch program accounting");

static sync_io_switch_snapshot_t s_switch;
static sync_io_persona_manager_handle_t s_handle;
static uint32_t s_busy,s_generation,s_claimed,s_offset,s_active_sms,s_physical_mask;
static bool s_loaded,s_lease,s_pins_owned;
static struct {uint32_t ctrl,pad;bool direction,latch;} s_saved[BOARD_SYNC_OUTPUT_PIN_COUNT];
static uint32_t s_saved_pio_out,s_saved_pio_oe;

static bool enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_busy,&idle,1u,
        false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void leave(void){__atomic_store_n(&s_busy,0u,__ATOMIC_RELEASE);}
static bool matching(uint32_t generation){return s_lease && generation && generation==s_switch.generation;}
static void split_groups(void)
{
    s_active_sms=0u;
    for(uint32_t i=0u;i<BOARD_SYNC_OUTPUT_PIN_COUNT;) {
        if(!(s_switch.output_mask&(1u<<i))) {++i;continue;}
        sync_io_switch_group_t *g=&s_switch.groups[s_switch.group_count];
        g->sm=s_switch.group_count ? SYNC_IO_SWITCH_SECOND_SM : SYNC_IO_SWITCH_FIRST_SM;
        g->output_base_pin=BOARD_SYNC_OUTPUT_BASE_PIN+i;
        do {++g->output_count;++i;} while(i<BOARD_SYNC_OUTPUT_PIN_COUNT && (s_switch.output_mask&(1u<<i)));
        s_active_sms|=1u<<g->sm;++s_switch.group_count;
    }
}
static bool load(void *context,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)context;
    if(!d || d->id!=SYNC_IO_PERSONA_ID_SWITCH || dma || d->gpio_read_mask ||
        d->gpio_write_mask!=s_physical_mask ||
        pio_sm_is_claimed(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM) ||
        pio_sm_is_claimed(SWITCH_PIO,SYNC_IO_SWITCH_SECOND_SM) ||
        !pio_can_add_program(SWITCH_PIO,&sync_switch_program)) return false;
    pio_sm_claim(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM);s_claimed|=1u<<SYNC_IO_SWITCH_FIRST_SM;
    pio_sm_claim(SWITCH_PIO,SYNC_IO_SWITCH_SECOND_SM);s_claimed|=1u<<SYNC_IO_SWITCH_SECOND_SM;
    s_offset=pio_add_program(SWITCH_PIO,&sync_switch_program);s_loaded=true;
    s_saved_pio_out=SWITCH_PIO->dbg_padout&s_physical_mask;
    s_saved_pio_oe=SWITCH_PIO->dbg_padoe&s_physical_mask;
    for(uint32_t i=0u;i<BOARD_SYNC_OUTPUT_PIN_COUNT;++i) if(s_switch.output_mask&(1u<<i)) {
        const uint pin=BOARD_SYNC_OUTPUT_BASE_PIN+i;
        s_saved[i].ctrl=io_bank0_hw->io[pin].ctrl;s_saved[i].pad=pads_bank0_hw->io[pin];
        s_saved[i].direction=gpio_get_dir(pin);s_saved[i].latch=(sio_hw->gpio_out&(1u<<pin))!=0u;
    }
    return true;
}
static bool arm(void *context,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)context;(void)d;(void)dma;
    if(!s_loaded || s_claimed!=SWITCH_SM_MASK) return false;
    pio_sm_set_enabled(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,false);
    pio_sm_set_enabled(SWITCH_PIO,SYNC_IO_SWITCH_SECOND_SM,false);
    for(uint32_t i=0u;i<s_switch.group_count;++i) {
        const sync_io_switch_group_t *g=&s_switch.groups[i];
        pio_sm_config config=sync_switch_program_get_default_config(s_offset);
        sm_config_set_out_pins(&config,g->output_base_pin,g->output_count);
        sm_config_set_out_shift(&config,true,false,32u);
        sm_config_set_clkdiv_int_frac(&config,1u,0u);
        if(pio_sm_init(SWITCH_PIO,g->sm,s_offset,&config)!=0) return false;
    }
    for(uint sm=0u;sm<4u;++sm) if(s_claimed&(1u<<sm)) {
        pio_sm_clear_fifos(SWITCH_PIO,sm);pio_sm_restart(SWITCH_PIO,sm);
    }
    pio_sm_set_pins_with_mask(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,0u,s_physical_mask);
    pio_sm_set_pindirs_with_mask(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,s_physical_mask,s_physical_mask);
    for(uint32_t i=0u;i<BOARD_SYNC_OUTPUT_PIN_COUNT;++i) if(s_switch.output_mask&(1u<<i)) {
        const uint pin=BOARD_SYNC_OUTPUT_BASE_PIN+i;
        gpio_put(pin,false);gpio_set_dir(pin,GPIO_OUT);gpio_set_function(pin,GPIO_FUNC_PIO0);
    }
    s_pins_owned=true;return true;
}
static void safe_stop(void)
{
    for(uint sm=0u;sm<4u;++sm) if(s_claimed&(1u<<sm)) {
        pio_sm_set_enabled(SWITCH_PIO,sm,false);
        pio_sm_clear_fifos(SWITCH_PIO,sm);pio_sm_restart(SWITCH_PIO,sm);
    }
    if(s_pins_owned) {
        pio_sm_set_pins_with_mask(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,0u,s_physical_mask);
        for(uint32_t i=0u;i<BOARD_SYNC_OUTPUT_PIN_COUNT;++i)
            if(s_switch.output_mask&(1u<<i)) gpio_put(BOARD_SYNC_OUTPUT_BASE_PIN+i,false);
    }
}
static void cleanup(void *context,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)context;(void)d;(void)dma;safe_stop();
    if(s_pins_owned) {
        for(uint32_t i=0u;i<BOARD_SYNC_OUTPUT_PIN_COUNT;++i)
            if(s_switch.output_mask&(1u<<i)) pads_bank0_hw->io[BOARD_SYNC_OUTPUT_BASE_PIN+i]|=PADS_BANK0_GPIO0_OD_BITS;
        pio_sm_set_pins_with_mask(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,s_saved_pio_out,s_physical_mask);
        pio_sm_set_pindirs_with_mask(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,s_saved_pio_oe,s_physical_mask);
        for(uint32_t i=0u;i<BOARD_SYNC_OUTPUT_PIN_COUNT;++i) if(s_switch.output_mask&(1u<<i)) {
            const uint pin=BOARD_SYNC_OUTPUT_BASE_PIN+i;
            gpio_put(pin,s_saved[i].latch);gpio_set_dir(pin,s_saved[i].direction);
            io_bank0_hw->io[pin].ctrl=s_saved[i].ctrl;pads_bank0_hw->io[pin]=s_saved[i].pad;
        }
    }
    s_pins_owned=false;
    if(s_loaded) {pio_remove_program(SWITCH_PIO,&sync_switch_program,s_offset);s_loaded=false;}
    for(uint sm=0u;sm<4u;++sm) if(s_claimed&(1u<<sm)) pio_sm_unclaim(SWITCH_PIO,sm);
    s_claimed=0u;
}
bool sync_io_switch_load(uint32_t output_mask,uint32_t *generation)
{
    if(!generation || !output_mask || (output_mask&~OUTPUT_MASK) || !enter()) return false;
    if(s_lease || s_generation==UINT32_MAX || !sync_io_core_switch_reserve(&s_switch)) {leave();return false;}
    const sync_io_switch_snapshot_t previous=s_switch;
    memset(&s_switch,0,sizeof(s_switch));s_switch.schema=SYNC_IO_SWITCH_SCHEMA;
    s_switch.output_mask=output_mask;s_physical_mask=output_mask<<BOARD_SYNC_OUTPUT_BASE_PIN;split_groups();
    const sync_io_persona_manager_hooks_t hooks={.load=load,.arm=arm,.cleanup=cleanup};
    if(!sync_io_pio0_runtime_prepare_io(SYNC_IO_PERSONA_ID_SWITCH,0u,s_physical_mask,&hooks,NULL,&s_handle)) {
        (void)sync_io_core_switch_release(&s_switch);s_switch=previous;leave();return false;
    }
    s_lease=true;s_switch.generation=++s_generation;s_switch.state=SYNC_IO_SWITCH_LOADED;
    *generation=s_switch.generation;leave();return true;
}
bool sync_io_switch_stage(uint32_t generation,uint32_t code)
{
    if(!enter()) return false;
    if(!matching(generation) || (code&~s_switch.output_mask) ||
        s_switch.state==SYNC_IO_SWITCH_PENDING || s_switch.state==SYNC_IO_SWITCH_FAULT ||
        s_switch.stage_sequence==UINT32_MAX) {leave();return false;}
    if(s_switch.state==SYNC_IO_SWITCH_STAGED) s_switch.cancelled_sequence=s_switch.stage_sequence;
    for(uint32_t i=0u;i<s_switch.group_count;++i) {
        sync_io_switch_group_t *g=&s_switch.groups[i];
        pio_sm_set_enabled(SWITCH_PIO,g->sm,false);pio_sm_clear_fifos(SWITCH_PIO,g->sm);
        pio_sm_restart(SWITCH_PIO,g->sm);pio_sm_exec(SWITCH_PIO,g->sm,pio_encode_jmp(s_offset));
        g->code=(code>>(g->output_base_pin-BOARD_SYNC_OUTPUT_BASE_PIN))&((1u<<g->output_count)-1u);
        pio_sm_put(SWITCH_PIO,g->sm,g->code);
    }
    ++s_switch.stage_sequence;s_switch.desired_code=code;s_switch.receipt_mask=0u;
    s_switch.state=SYNC_IO_SWITCH_STAGED;leave();return true;
}
bool sync_io_switch_apply(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation) || s_switch.state!=SYNC_IO_SWITCH_STAGED) {leave();return false;}
    /* Simultaneous divider reset + enable: groups run the same four words
     * with one already queued input each. No host/CPU wait for completion. */
    pio_enable_sm_mask_in_sync(SWITCH_PIO,s_active_sms);
    s_switch.state=SYNC_IO_SWITCH_PENDING;leave();return true;
}
bool sync_io_switch_stop(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation)) {leave();return false;}
    if(s_switch.state==SYNC_IO_SWITCH_PENDING || s_switch.state==SYNC_IO_SWITCH_STAGED)
        s_switch.cancelled_sequence=s_switch.stage_sequence;
    safe_stop();s_switch.state=SYNC_IO_SWITCH_STOPPED;s_switch.applied_valid=0u;
    s_switch.receipt_mask=0u;s_switch.fault=0u;leave();return true;
}
bool sync_io_switch_release(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation) || s_switch.state==SYNC_IO_SWITCH_PENDING ||
        !sync_io_pio0_runtime_release(&s_handle)) {leave();return false;}
    s_lease=false;const bool ok=sync_io_core_switch_release(&s_switch);
    s_switch.state=SYNC_IO_SWITCH_IDLE;s_switch.applied_valid=0u;leave();return ok;
}
static void poll(void)
{
    if(s_switch.state!=SYNC_IO_SWITCH_PENDING) return;
    for(uint32_t i=0u;i<s_switch.group_count;++i) {
        const sync_io_switch_group_t *g=&s_switch.groups[i];
        if(s_switch.receipt_mask&(1u<<i)) continue;
        if(!pio_sm_is_rx_fifo_empty(SWITCH_PIO,g->sm)) {
            const uint32_t receipt=pio_sm_get(SWITCH_PIO,g->sm);
            if(receipt!=g->code) {
                safe_stop();s_switch.state=SYNC_IO_SWITCH_FAULT;
                s_switch.fault=1u;s_switch.applied_valid=0u;return;
            }
            s_switch.receipt_mask|=1u<<i;
        }
    }
    if(s_switch.receipt_mask==((1u<<s_switch.group_count)-1u)) {
        for(uint32_t i=0u;i<s_switch.group_count;++i) pio_sm_set_enabled(SWITCH_PIO,s_switch.groups[i].sm,false);
        s_switch.applied_code=s_switch.desired_code;s_switch.applied_valid=1u;
        s_switch.completed_sequence=s_switch.stage_sequence;s_switch.state=SYNC_IO_SWITCH_COMPLETE;
    }
}
bool sync_io_switch_snapshot(sync_io_switch_snapshot_t *out)
{
    if(!out || !enter()) return false;
    poll();sync_io_switch_snapshot_t snapshot=s_switch;snapshot.schema=SYNC_IO_SWITCH_SCHEMA;
    snapshot.loaded=s_lease;snapshot.sm_claimed_mask=0u;
    for(uint sm=0u;sm<4u;++sm)
        if((s_claimed&(1u<<sm)) && pio_sm_is_claimed(SWITCH_PIO,sm)) snapshot.sm_claimed_mask|=1u<<sm;
    snapshot.sm_enabled_mask=s_lease ? SWITCH_PIO->ctrl&s_claimed : 0u;
    snapshot.instruction_words=s_loaded ? SYNC_IO_SWITCH_INSTRUCTION_WORDS : 0u;
    snapshot.program_offset=s_loaded ? s_offset : 0u;
    snapshot.actual_output_mask=0u;
    for(uint32_t i=0u;i<BOARD_SYNC_OUTPUT_PIN_COUNT;++i)
        if(gpio_get(BOARD_SYNC_OUTPUT_BASE_PIN+i)) snapshot.actual_output_mask|=1u<<i;
    for(uint32_t i=0u;i<snapshot.group_count;++i) {
        sync_io_switch_group_t *g=&snapshot.groups[i];
        if(s_lease) {
            const uint32_t route=SWITCH_PIO->sm[g->sm].pinctrl;
            g->output_base_pin=(route&PIO_SM0_PINCTRL_OUT_BASE_BITS)>>PIO_SM0_PINCTRL_OUT_BASE_LSB;
            g->output_count=(route&PIO_SM0_PINCTRL_OUT_COUNT_BITS)>>PIO_SM0_PINCTRL_OUT_COUNT_LSB;
            g->tx_level=pio_sm_get_tx_fifo_level(SWITCH_PIO,g->sm);
            g->rx_level=pio_sm_get_rx_fifo_level(SWITCH_PIO,g->sm);
        } else {g->tx_level=0u;g->rx_level=0u;}
    }
    *out=snapshot;leave();return true;
}

bool sync_io_switch_realtime_fire(uint32_t code)
{
    if(get_core_num()!=1u || !s_lease || !s_loaded || !s_pins_owned ||
       (code&~s_switch.output_mask) || s_switch.state==SYNC_IO_SWITCH_PENDING ||
       s_switch.state==SYNC_IO_SWITCH_FAULT || s_switch.stage_sequence==UINT32_MAX)
        return false;
    for(uint32_t i=0u;i<s_switch.group_count;++i) {
        const sync_io_switch_group_t *g=&s_switch.groups[i];
        pio_sm_set_enabled(SWITCH_PIO,g->sm,false);
        pio_sm_clear_fifos(SWITCH_PIO,g->sm);
        pio_sm_restart(SWITCH_PIO,g->sm);
        pio_sm_exec(SWITCH_PIO,g->sm,pio_encode_jmp(s_offset));
        const uint32_t mask=(1u<<g->output_count)-1u;
        const uint32_t group_code=(code>>
            (g->output_base_pin-BOARD_SYNC_OUTPUT_BASE_PIN))&mask;
        pio_sm_put(SWITCH_PIO,g->sm,group_code);
    }
    s_switch.desired_code=code;s_switch.receipt_mask=0u;
    ++s_switch.stage_sequence;s_switch.state=SYNC_IO_SWITCH_PENDING;
    pio_enable_sm_mask_in_sync(SWITCH_PIO,s_active_sms);
    return true;
}

void sync_io_switch_realtime_service_core1(void)
{
    if(get_core_num()==1u) poll();
}
