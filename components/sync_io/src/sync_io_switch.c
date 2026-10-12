#include "sync_io_switch.h"
#include <string.h>
#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/pio.h"
#include "pico/platform.h"
#ifndef __not_in_flash_func
#define __not_in_flash_func(name) name
#endif
#include "pico/platform.h"
#include "sync_io_core_internal.h"
#include "sync_io_pio0_runtime.h"
#include "tdma_runtime_owner.h"
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
static sync_io_switch_deadline_status_t s_deadline;

/* Only this bounded origin window is resident; resource preparation and
 * division remain outside it. TIMER1 must already use clk_sys. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SWITCH_HOT(name) __scratch_y("t2_switch_deadline") name
#else
#define SWITCH_HOT(name) name
#endif
static inline __attribute__((always_inline)) bool raw_tick(uint64_t *out)
{
    const uint32_t hi=timer1_hw->timerawh,lo=timer1_hw->timerawl;
    if(hi!=timer1_hw->timerawh)return false;
    *out=((uint64_t)hi<<32u)|lo;return true;
}
static __attribute__((noinline)) bool SWITCH_HOT(start_deadline)(uint64_t deadline)
{
    uint64_t before,after;
    const uint64_t guard=(uint64_t)BOARD_SYS_CLOCK_HZ/1000000u*SYNC_IO_SWITCH_DEADLINE_GUARD_US;
    if(!raw_tick(&before) || deadline<before || deadline-before<guard ||
        deadline-before-3u>UINT32_MAX)return false;
    const uint32_t delay=(uint32_t)(deadline-before-3u);
    pio_sm_put(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,delay);
    if(s_active_sms&(1u<<SYNC_IO_SWITCH_SECOND_SM))
        pio_sm_put(SWITCH_PIO,SYNC_IO_SWITCH_SECOND_SM,delay);
    pio_enable_sm_mask_in_sync(SWITCH_PIO,s_active_sms);
    const bool read=raw_tick(&after);
    s_deadline.origin_before_tick=before;
    s_deadline.origin_after_tick=read && after!=UINT64_MAX ? after+1u : UINT64_MAX;
    if(read && after>=before && after!=UINT64_MAX &&
        after+1u-before <= (uint64_t)BOARD_SYS_CLOCK_HZ*SYNC_IO_SWITCH_ORIGIN_MAX_NS/1000000000u)
        return true;
    /* Disable before any slower diagnostics. The fresh post-disable sample
     * distinguishes a cancelled future edge from a possibly executed edge. */
    pio_sm_set_enabled(SWITCH_PIO,SYNC_IO_SWITCH_FIRST_SM,false);
    if(s_active_sms&(1u<<SYNC_IO_SWITCH_SECOND_SM))
        pio_sm_set_enabled(SWITCH_PIO,SYNC_IO_SWITCH_SECOND_SM,false);
    uint64_t stopped;
    s_deadline.state=raw_tick(&stopped) && stopped<deadline ?
        SYNC_IO_SWITCH_DEADLINE_CANCELLED:SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
    return false;
}

static void prepare_group(const sync_io_switch_group_t *g,uint32_t code)
{
    pio_sm_set_enabled(SWITCH_PIO,g->sm,false);
    pio_sm_clear_fifos(SWITCH_PIO,g->sm);
    pio_sm_restart(SWITCH_PIO,g->sm);
    pio_sm_exec(SWITCH_PIO,g->sm,pio_encode_jmp(s_offset));
    pio_sm_put(SWITCH_PIO,g->sm,code);
    pio_sm_exec(SWITCH_PIO,g->sm,pio_encode_pull(false,true));
    pio_sm_exec(SWITCH_PIO,g->sm,pio_encode_mov(pio_y,pio_osr));
}

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
static void __not_in_flash_func(safe_stop)(void)
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
    memset(&s_deadline,0,sizeof(s_deadline));
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
        g->code=(code>>(g->output_base_pin-BOARD_SYNC_OUTPUT_BASE_PIN))&((1u<<g->output_count)-1u);
        prepare_group(g,g->code);
        pio_sm_put(SWITCH_PIO,g->sm,0u);
    }
    ++s_switch.stage_sequence;s_switch.desired_code=code;s_switch.receipt_mask=0u;
    s_deadline.state=SYNC_IO_SWITCH_DEADLINE_IDLE;
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
    if(s_deadline.state==SYNC_IO_SWITCH_DEADLINE_ARMED) {
        uint64_t stopped;
        s_deadline.state=raw_tick(&stopped) && stopped<s_deadline.deadline_tick ?
            SYNC_IO_SWITCH_DEADLINE_CANCELLED:SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
    }
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
/* noinline: a split poll.part.0 clone landed in XIP behind the RAM service. */
static __attribute__((noinline)) void __not_in_flash_func(poll)(void)
{
    if(s_switch.state!=SYNC_IO_SWITCH_PENDING) return;
    for(uint32_t i=0u;i<s_switch.group_count;++i) {
        const sync_io_switch_group_t *g=&s_switch.groups[i];
        if(s_switch.receipt_mask&(1u<<i)) continue;
        if(!pio_sm_is_rx_fifo_empty(SWITCH_PIO,g->sm)) {
            const uint32_t receipt=pio_sm_get(SWITCH_PIO,g->sm);
            if(receipt!=g->code) {
                safe_stop();s_switch.state=SYNC_IO_SWITCH_FAULT;
                if(s_deadline.state==SYNC_IO_SWITCH_DEADLINE_ARMED)
                    s_deadline.state=SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
                s_switch.fault=1u;s_switch.applied_valid=0u;return;
            }
            s_switch.receipt_mask|=1u<<i;
        }
    }
    if(s_switch.receipt_mask==((1u<<s_switch.group_count)-1u)) {
        for(uint32_t i=0u;i<s_switch.group_count;++i) pio_sm_set_enabled(SWITCH_PIO,s_switch.groups[i].sm,false);
        s_switch.applied_code=s_switch.desired_code;s_switch.applied_valid=1u;
        s_switch.completed_sequence=s_switch.stage_sequence;s_switch.state=SYNC_IO_SWITCH_COMPLETE;
        if(s_deadline.state==SYNC_IO_SWITCH_DEADLINE_ARMED) {
            s_deadline.receipt_mask=s_switch.receipt_mask;
            s_deadline.state=SYNC_IO_SWITCH_DEADLINE_COMPLETE;
        }
    }
}
static bool poll_stopped(void *context)
{
    (void)context;poll();return true;
}
bool sync_io_switch_snapshot(sync_io_switch_snapshot_t *out)
{
    if(!out || !enter()) return false;
    /* Core1 alone consumes RUN receipts. The owner gate is indivisible with
     * ARM/START; a read-only SCPI query cannot race its future deadline by
     * draining a FIFO or disabling an SM. Nested STOP callers may skip poll. */
    (void)tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),poll_stopped,NULL);
    sync_io_switch_snapshot_t snapshot=s_switch;snapshot.schema=SYNC_IO_SWITCH_SCHEMA;
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
        sync_io_switch_group_t *g=&s_switch.groups[i];
        const uint32_t mask=(1u<<g->output_count)-1u;
        const uint32_t group_code=(code>>
            (g->output_base_pin-BOARD_SYNC_OUTPUT_BASE_PIN))&mask;
        g->code=group_code;prepare_group(g,group_code);
        pio_sm_put(SWITCH_PIO,g->sm,0u);
    }
    s_switch.desired_code=code;s_switch.receipt_mask=0u;
    ++s_switch.stage_sequence;s_switch.state=SYNC_IO_SWITCH_PENDING;
    s_deadline.state=SYNC_IO_SWITCH_DEADLINE_IDLE;
    pio_enable_sm_mask_in_sync(SWITCH_PIO,s_active_sms);
    return true;
}

/* Polled every T2 slice while a deadline is armed: keep it out of XIP. */
void __not_in_flash_func(sync_io_switch_realtime_service_core1)(void)
{
    if(get_core_num()==1u) poll();
}

/* Runs on the T2 arming slice, the busiest one: keep it out of XIP. */
bool __not_in_flash_func(sync_io_switch_realtime_schedule_at)(uint32_t code,uint64_t deadline_tick)
{
    if(get_core_num()!=1u || !s_lease || !s_loaded || !s_pins_owned ||
       (code&~s_switch.output_mask) || s_switch.state==SYNC_IO_SWITCH_PENDING ||
       s_switch.state==SYNC_IO_SWITCH_FAULT || s_switch.stage_sequence==UINT32_MAX ||
       clock_get_hz(clk_sys)!=BOARD_SYS_CLOCK_HZ || timer1_hw->pause ||
       timer1_hw->source!=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS)return false;
    for(uint32_t i=0u;i<s_switch.group_count;++i) {
        sync_io_switch_group_t *g=&s_switch.groups[i];
        g->code=(code>>(g->output_base_pin-BOARD_SYNC_OUTPUT_BASE_PIN))&((1u<<g->output_count)-1u);
        prepare_group(g,g->code);
    }
    s_deadline=(sync_io_switch_deadline_status_t){
        .state=SYNC_IO_SWITCH_DEADLINE_CANCELLED,.deadline_tick=deadline_tick,
        .sequence=s_switch.stage_sequence+1u};
    if(!start_deadline(deadline_tick)) {
        for(uint32_t i=0u;i<s_switch.group_count;++i)
            pio_sm_clear_fifos(SWITCH_PIO,s_switch.groups[i].sm);
        s_switch.state=s_deadline.state==SYNC_IO_SWITCH_DEADLINE_UNCERTAIN ?
            SYNC_IO_SWITCH_FAULT:SYNC_IO_SWITCH_STOPPED;
        if(s_deadline.state==SYNC_IO_SWITCH_DEADLINE_UNCERTAIN) {
            safe_stop();s_switch.fault=2u;
        }
        s_switch.applied_valid=0u;
        return false;
    }
    s_switch.desired_code=code;s_switch.receipt_mask=0u;
    ++s_switch.stage_sequence;s_switch.state=SYNC_IO_SWITCH_PENDING;
    s_deadline.state=SYNC_IO_SWITCH_DEADLINE_ARMED;
    return true;
}

bool __not_in_flash_func(sync_io_switch_realtime_status)(sync_io_switch_deadline_status_t *out)
{
    if(get_core_num()!=1u || !out)return false;
    *out=s_deadline;
    if(s_deadline.sequence==s_switch.stage_sequence)out->receipt_mask=s_switch.receipt_mask;
    return true;
}

bool __not_in_flash_func(sync_io_switch_realtime_cancel)(void)
{
    if(get_core_num()!=1u || !s_lease)return false;
    safe_stop();
    if(s_deadline.state==SYNC_IO_SWITCH_DEADLINE_ARMED) {
        uint64_t stopped;
        s_deadline.state=raw_tick(&stopped) && stopped<s_deadline.deadline_tick ?
            SYNC_IO_SWITCH_DEADLINE_CANCELLED:SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
    }
    s_switch.cancelled_sequence=s_switch.stage_sequence;
    s_switch.state=SYNC_IO_SWITCH_STOPPED;s_switch.applied_valid=0u;
    return true;
}
