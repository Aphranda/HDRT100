#include "sync_io_action.h"
#include <string.h>
#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/regs/pads_bank0.h"
#include "pico/platform.h"
#include "sync_io_core_internal.h"
#include "sync_io_pio0_runtime.h"
#include "sync_action.pio.h"

#define ACTION_PIO BOARD_SYNC_PIO_FAST
#define ACTION_SMS ((1u<<1u)|(1u<<3u))
_Static_assert(BOARD_SYNC_OUTPUT_PIN_COUNT==4u,"finite groups assume four outputs");
_Static_assert(sizeof(sync_action_program_instructions)/sizeof(uint16_t)==
    SYNC_IO_ACTION_INSTRUCTION_WORDS,"action program budget");
static sync_io_action_snapshot_t s_action;
static sync_io_persona_manager_handle_t s_handle;
static uint32_t s_busy,s_generation,s_claimed,s_offset,s_mask,s_active;
static uint32_t s_codes[2],s_counts[2];
static bool s_loaded,s_lease,s_pins;
static struct {uint32_t ctrl,pad;bool direction,latch;} s_saved[4];
static uint32_t s_pio_out,s_pio_oe;
static bool enter(void){uint32_t idle=0u;return get_core_num()==0u &&
    __atomic_compare_exchange_n(&s_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
static void leave(void){__atomic_store_n(&s_busy,0u,__ATOMIC_RELEASE);}
static bool raw(uint64_t *value)
{
    if(clock_get_hz(clk_sys)!=BOARD_SYS_CLOCK_HZ || timer1_hw->pause ||
        timer1_hw->source!=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS)return false;
    const uint32_t hi=timer1_hw->timerawh,lo=timer1_hw->timerawl;
    if(hi!=timer1_hw->timerawh)return false;
    *value=((uint64_t)hi<<32u)|lo;return true;
}
static uint sm(uint group){return group?3u:1u;}
static bool matching(uint32_t generation){return s_lease && generation && generation==s_action.generation;}
static bool load(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;
    if(!d || d->id!=SYNC_IO_PERSONA_ID_ACTION || dma || d->gpio_write_mask!=s_mask ||
        pio_sm_is_claimed(ACTION_PIO,1u) || pio_sm_is_claimed(ACTION_PIO,3u) ||
        !pio_can_add_program(ACTION_PIO,&sync_action_program))return false;
    pio_sm_claim(ACTION_PIO,1u);s_claimed|=1u<<1u;
    pio_sm_claim(ACTION_PIO,3u);s_claimed|=1u<<3u;
    s_offset=pio_add_program(ACTION_PIO,&sync_action_program);s_loaded=true;
    s_pio_out=ACTION_PIO->dbg_padout&s_mask;s_pio_oe=ACTION_PIO->dbg_padoe&s_mask;
    for(uint i=0u;i<4u;++i)if(s_action.config.output_mask&(1u<<i)){
        const uint pin=BOARD_SYNC_OUTPUT_BASE_PIN+i;
        s_saved[i].ctrl=io_bank0_hw->io[pin].ctrl;s_saved[i].pad=pads_bank0_hw->io[pin];
        s_saved[i].direction=gpio_get_dir(pin);s_saved[i].latch=(sio_hw->gpio_out&(1u<<pin))!=0u;
    }
    return true;
}
static bool prepare(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;(void)d;(void)dma;
    for(uint i=0u;i<2u;++i)pio_sm_set_enabled(ACTION_PIO,sm(i),false);
    for(uint i=0u;i<s_action.group_count;++i){
        pio_sm_config c=sync_action_program_get_default_config(s_offset);
        sm_config_set_out_pins(&c,s_action.group_base[i],s_action.group_count_pins[i]);
        sm_config_set_out_shift(&c,true,false,32u);sm_config_set_clkdiv_int_frac(&c,1u,0u);
        if(pio_sm_init(ACTION_PIO,sm(i),s_offset,&c)!=0)return false;
        pio_sm_clear_fifos(ACTION_PIO,sm(i));pio_sm_restart(ACTION_PIO,sm(i));
    }
    pio_sm_set_pins_with_mask(ACTION_PIO,1u,0u,s_mask);
    pio_sm_set_pindirs_with_mask(ACTION_PIO,1u,s_mask,s_mask);
    for(uint i=0u;i<4u;++i)if(s_action.config.output_mask&(1u<<i)){
        const uint pin=BOARD_SYNC_OUTPUT_BASE_PIN+i;
        gpio_put(pin,false);gpio_set_dir(pin,GPIO_OUT);gpio_set_function(pin,GPIO_FUNC_PIO0);
    }
    s_pins=true;return true;
}
static void safe_stop(void)
{
    for(uint i=0u;i<2u;++i)if(s_claimed&(1u<<sm(i))){
        pio_sm_set_enabled(ACTION_PIO,sm(i),false);pio_sm_clear_fifos(ACTION_PIO,sm(i));
        pio_sm_restart(ACTION_PIO,sm(i));
    }
    if(s_pins)pio_sm_set_pins_with_mask(ACTION_PIO,1u,0u,s_mask);
}
static void cleanup(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;(void)d;(void)dma;safe_stop();
    if(s_pins){
        for(uint i=0u;i<4u;++i)if(s_action.config.output_mask&(1u<<i))
            pads_bank0_hw->io[BOARD_SYNC_OUTPUT_BASE_PIN+i]|=PADS_BANK0_GPIO0_OD_BITS;
        pio_sm_set_pins_with_mask(ACTION_PIO,1u,s_pio_out,s_mask);
        pio_sm_set_pindirs_with_mask(ACTION_PIO,1u,s_pio_oe,s_mask);
        for(uint i=0u;i<4u;++i)if(s_action.config.output_mask&(1u<<i)){
            const uint pin=BOARD_SYNC_OUTPUT_BASE_PIN+i;
            gpio_put(pin,s_saved[i].latch);gpio_set_dir(pin,s_saved[i].direction);
            io_bank0_hw->io[pin].ctrl=s_saved[i].ctrl;pads_bank0_hw->io[pin]=s_saved[i].pad;
        }
    }
    s_pins=false;
    if(s_loaded){pio_remove_program(ACTION_PIO,&sync_action_program,s_offset);s_loaded=false;}
    for(uint i=0u;i<2u;++i)if(s_claimed&(1u<<sm(i)))pio_sm_unclaim(ACTION_PIO,sm(i));
    s_claimed=0u;
}
bool sync_io_action_load(const sync_io_action_config_t *config,uint32_t *generation)
{
    if(!config || !generation || !config->output_mask || (config->output_mask&~15u))return false;
    if(config->kind==SYNC_IO_ACTION_SWITCH){
        if((config->code&~config->output_mask) || config->input_port || config->pulse_ns)return false;
    }else if(config->kind==SYNC_IO_ACTION_GATEWAY){
        const uint64_t ticks=(uint64_t)config->pulse_ns*BOARD_SYS_CLOCK_HZ;
        if((config->output_mask&(config->output_mask-1u)) || config->code!=config->output_mask ||
            !config->input_port || config->input_port>BOARD_SYNC_INPUT_PIN_COUNT ||
            ticks%UINT64_C(1000000000) || ticks/UINT64_C(1000000000)<8u)return false;
    }else return false;
    if(!enter())return false;
    /* Same admission lane as local SWITCH: action/counter/gateway/run are
     * mutually exclusive; reference remains admitted by the same manager. */
    if(s_lease || s_generation==UINT32_MAX || !sync_io_core_switch_reserve(&s_action)){leave();return false;}
    const sync_io_action_snapshot_t previous=s_action;
    memset(&s_action,0,sizeof(s_action));s_action.schema=SYNC_IO_ACTION_SCHEMA;s_action.config=*config;
    s_action.tick_hz=BOARD_SYS_CLOCK_HZ;s_active=0u;s_mask=config->output_mask<<BOARD_SYNC_OUTPUT_BASE_PIN;
    if(config->input_port)s_action.input_pin=BOARD_SYNC_INPUT_BASE_PIN+
        (BOARD_SYNC_INPUT_BITS_REVERSED?BOARD_SYNC_INPUT_PIN_COUNT-config->input_port:config->input_port-1u);
    for(uint pin=0u;pin<4u;){
        if(!(config->output_mask&(1u<<pin))){++pin;continue;}
        const uint group=s_action.group_count++;
        s_action.group_base[group]=BOARD_SYNC_OUTPUT_BASE_PIN+pin;
        do{++s_action.group_count_pins[group];++pin;}while(pin<4u&&(config->output_mask&(1u<<pin)));
        s_active|=1u<<sm(group);
        s_codes[group]=(config->code>>(s_action.group_base[group]-BOARD_SYNC_OUTPUT_BASE_PIN))&
            ((1u<<s_action.group_count_pins[group])-1u);
    }
    const sync_io_persona_manager_hooks_t hooks={.load=load,.arm=prepare,.cleanup=cleanup};
    if(!sync_io_pio0_runtime_prepare_io(SYNC_IO_PERSONA_ID_ACTION,
            config->input_port?1u<<s_action.input_pin:0u,s_mask,&hooks,NULL,&s_handle)){
        (void)sync_io_core_switch_release(&s_action);s_action=previous;leave();return false;
    }
    s_lease=true;s_action.generation=++s_generation;s_action.state=SYNC_IO_ACTION_LOADED;
    s_action.expected_receipts=s_action.group_count*(config->kind==SYNC_IO_ACTION_GATEWAY?2u:1u);
    *generation=s_action.generation;leave();return true;
}
bool sync_io_action_arm_checked(uint32_t generation,uint64_t deadline_tick,
    bool (*validate)(void *context),void *context)
{
    if(!enter())return false;
    uint64_t before=0u;
    const uint64_t lead=(uint64_t)BOARD_SYS_CLOCK_HZ*SYNC_IO_ACTION_MIN_LEAD_US/1000000u;
    const uint64_t high=(uint64_t)s_action.config.pulse_ns*BOARD_SYS_CLOCK_HZ/1000000000u;
    if(!matching(generation) || s_action.state!=SYNC_IO_ACTION_LOADED || !raw(&before) ||
        deadline_tick<before || deadline_tick-before<lead || deadline_tick-before-5u>UINT32_MAX ||
        deadline_tick>UINT64_MAX-high){leave();return false;}
    /* Reset all private FIFOs/PCs BEFORE the final origin sample. No write
     * made after this point changes the selected output value. */
    for(uint i=0u;i<s_action.group_count;++i){
        pio_sm_clear_fifos(ACTION_PIO,sm(i));pio_sm_restart(ACTION_PIO,sm(i));
        pio_sm_exec(ACTION_PIO,sm(i),pio_encode_jmp(s_offset));
    }
    if(!raw(&before) || deadline_tick<before || deadline_tick-before<lead ||
        deadline_tick-before-5u>UINT32_MAX){leave();return false;}
    const uint32_t delay=(uint32_t)(deadline_tick-before-5u);
    for(uint i=0u;i<s_action.group_count;++i){
        pio_sm_put(ACTION_PIO,sm(i),delay);pio_sm_put(ACTION_PIO,sm(i),s_codes[i]);
        if(s_action.config.kind==SYNC_IO_ACTION_GATEWAY){
            pio_sm_put(ACTION_PIO,sm(i),(uint32_t)(high-8u));pio_sm_put(ACTION_PIO,sm(i),0u);
        }
        s_counts[i]=0u;
    }
    uint64_t ready=0u;
    if((validate && !validate(context)) || !raw(&ready) || ready<before ||
        deadline_tick<ready || deadline_tick-ready<lead){
        for(uint i=0u;i<s_action.group_count;++i)pio_sm_clear_fifos(ACTION_PIO,sm(i));
        leave();return false;
    }
    pio_enable_sm_mask_in_sync(ACTION_PIO,s_active);
    uint64_t after=0u;
    s_action.deadline_tick=deadline_tick;s_action.falling_tick=high?deadline_tick+high:0u;
    s_action.enable_before_tick=before;s_action.state=SYNC_IO_ACTION_ARMED;
    if(!raw(&after) || after<before || after==UINT64_MAX || after>=deadline_tick){
        safe_stop();s_action.state=SYNC_IO_ACTION_FAULT;s_action.fault=1u;leave();return false;
    }
    s_action.enable_after_tick=after+1u;leave();return true;
}
bool sync_io_action_arm(uint32_t generation,uint64_t deadline_tick)
{return sync_io_action_arm_checked(generation,deadline_tick,NULL,NULL);}
bool sync_io_action_stop(uint32_t generation)
{
    if(!enter())return false;
    if(!matching(generation)){leave();return false;}
    safe_stop();if(s_action.state!=SYNC_IO_ACTION_COMPLETE)s_action.state=SYNC_IO_ACTION_CANCELLED;
    leave();return true;
}
bool sync_io_action_release(uint32_t generation)
{
    if(!enter())return false;
    if(!matching(generation) || s_action.state==SYNC_IO_ACTION_ARMED ||
        !sync_io_pio0_runtime_release(&s_handle)){leave();return false;}
    s_lease=false;const bool ok=sync_io_core_switch_release(&s_action);
    s_action.state=SYNC_IO_ACTION_IDLE;leave();return ok;
}
static void poll(void)
{
    if(s_action.state!=SYNC_IO_ACTION_ARMED)return;
    for(uint i=0u;i<s_action.group_count;++i){
        const uint wanted=s_action.config.kind==SYNC_IO_ACTION_GATEWAY?2u:1u;
        for(uint k=0u;k<2u && s_counts[i]<wanted;++k){
            if(pio_sm_is_rx_fifo_empty(ACTION_PIO,sm(i)))break;
            const uint32_t value=pio_sm_get(ACTION_PIO,sm(i));
            if(value!=(s_counts[i]?0u:s_codes[i])){
                safe_stop();s_action.state=SYNC_IO_ACTION_FAULT;s_action.fault=2u;return;
            }
            ++s_counts[i];++s_action.receipts;
        }
    }
    if(s_action.receipts==s_action.expected_receipts){
        for(uint i=0u;i<s_action.group_count;++i)pio_sm_set_enabled(ACTION_PIO,sm(i),false);
        s_action.state=SYNC_IO_ACTION_COMPLETE;
        uint64_t observed=0u;if(raw(&observed))s_action.completed_observed_tick=observed;
    }
}
bool sync_io_action_snapshot(sync_io_action_snapshot_t *out)
{
    if(!out || !enter())return false;
    poll();sync_io_action_snapshot_t snapshot=s_action;
    snapshot.schema=SYNC_IO_ACTION_SCHEMA;snapshot.loaded=s_lease;snapshot.sm_claimed_mask=s_claimed;
    snapshot.sm_enabled_mask=s_lease?ACTION_PIO->ctrl&s_claimed:0u;
    snapshot.instruction_words=s_loaded?SYNC_IO_ACTION_INSTRUCTION_WORDS:0u;snapshot.program_offset=s_loaded?s_offset:0u;
    snapshot.actual_output_mask=0u;for(uint i=0u;i<4u;++i)
        if(gpio_get(BOARD_SYNC_OUTPUT_BASE_PIN+i))snapshot.actual_output_mask|=1u<<i;
    snapshot.ready_level=snapshot.config.input_port?gpio_get(snapshot.input_pin):0u;
    *out=snapshot;leave();return true;
}
