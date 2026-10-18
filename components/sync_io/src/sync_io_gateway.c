#include "sync_io_gateway.h"
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
#ifndef __not_in_flash_func
#define __not_in_flash_func(name) name
#endif
#include "sync_io_core_internal.h"
#include "sync_io_pio0_runtime.h"
#include "sync_gateway.pio.h"

#define GATEWAY_PIO BOARD_SYNC_PIO_FAST
#define OUTPUT_SM SYNC_IO_GATEWAY_OUTPUT_SM
#define READY_SM SYNC_IO_GATEWAY_READY_SM
#define GATEWAY_SM_MASK ((1u<<OUTPUT_SM)|(1u<<READY_SM))

_Static_assert(sizeof(sync_gateway_output_program_instructions)/sizeof(uint16_t)==
    SYNC_IO_GATEWAY_OUTPUT_WORDS,"gateway output instruction budget");
_Static_assert(sizeof(sync_gateway_ready_program_instructions)/sizeof(uint16_t)==
    SYNC_IO_GATEWAY_READY_WORDS,"gateway READY instruction budget");

static sync_io_gateway_snapshot_t s_gateway;
static sync_io_persona_manager_handle_t s_handle;
static uint32_t s_busy,s_generation,s_claimed,s_loaded,s_output_offset,s_ready_offset;
static bool s_lease,s_pin_saved,s_pin_owned;
static uint32_t s_saved_ctrl,s_saved_pad;
static bool s_saved_direction,s_saved_latch,s_saved_pio_level,s_saved_pio_direction;
static volatile uint32_t s_realtime_pending;
static bool s_ready_active;
/* 1 = a pending READY blocks the next action (bench-accurate, default). 0 = diagnostic mode for a
 * rig without an analyzer: the pending READY is retired and counted, the chain keeps running. */
/* READY is disabled for now (the analyzer is not wired): the pending READY is retired and counted as a
 * collision, so the sequence can run end to end. TRIGger:SEQuence:REAdy 1 re-enables the strict
 * behaviour when the analyzer answers. */
static bool s_ready_required = false;
/* Time-collision evidence: an action started while the previous READY was still outstanding,
 * and the smallest gap ever measured between a received READY and the next armed deadline. */
static uint32_t s_ready_collision_count;
/* Groups whose tick had already passed when they were armed (they still fire). */
static uint32_t s_late_count;
/* Arm refusals: core/clock, prepare_realtime, origin window, delay range. */
static uint32_t s_arm_reject[4];
/* Completion path: waiting for the PIO, receipt mismatch, accepted completions. */
static uint32_t s_service_wait, s_service_bad_receipt, s_service_complete;
/* The PIO echoes the trigger count as its receipt, while the SEQUENCE names the group of the staged
 * table. Keeping them apart is what lets the runtime match a completion to a group (same model as the
 * switch, where the code word carried the group's identity). */
static uint32_t s_pending_receipt, s_pending_sequence, s_sequence_override, s_plan_base_sequence;
/* Why a staged table was refused: NO_LEASE, BAD_COUNT, BAD_OFFSET, BAD_DELTA, BUSY, READY,
 * BAD_PULSE, SCHEDULE. */
#define GATEWAY_STAGE_REJECT_COUNT 8u
static uint32_t s_stage_reject[GATEWAY_STAGE_REJECT_COUNT];
static uint32_t s_ready_margin_min_ticks;
static sync_io_gateway_deadline_status_t s_deadline;
/* Whole action table of the position currently running (SYNC-SCHED-001). Small on purpose: the
 * gateway fires one pulse at a time, so it only needs the groups that are still ahead of it. */
#define GATEWAY_PLAN_MAX 16u
typedef struct {
    uint64_t deadline_tick;
    uint32_t pulse_ns;
} gateway_plan_group_t;
static gateway_plan_group_t s_plan[GATEWAY_PLAN_MAX];
static uint32_t s_plan_count, s_plan_next;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define GATEWAY_HOT(name) __scratch_y("t2_gateway_deadline") name
#else
#define GATEWAY_HOT(name) name
#endif
static inline __attribute__((always_inline)) bool raw_tick(uint64_t *out)
{
    const uint32_t hi=timer1_hw->timerawh,lo=timer1_hw->timerawl;
    if(hi!=timer1_hw->timerawh)return false;
    *out=((uint64_t)hi<<32u)|lo;return true;
}
static __attribute__((noinline)) bool GATEWAY_HOT(start_deadline)(uint64_t deadline)
{
    uint64_t before,after;
    const uint64_t guard=(uint64_t)BOARD_SYS_CLOCK_HZ/1000000u*SYNC_IO_GATEWAY_DEADLINE_GUARD_US;
    if(!raw_tick(&before)) { ++s_arm_reject[3]; return false; }
    /* Clamp BEFORE the range check: a past deadline underflows the subtraction and would be rejected as
     * "not representable" (measured: arm_reject[3]=51 with late=0), which is exactly the case the clamp
     * exists for. A late group fires as soon as the PIO can and the lateness is counted. */
    uint64_t target=deadline;
    if(deadline<before+guard) { target=before+guard; ++s_late_count; }
    const uint64_t delta=target-before;
    if(delta>UINT32_MAX || delta<4u) { ++s_arm_reject[3]; return false; }
    pio_sm_put(GATEWAY_PIO,OUTPUT_SM,(uint32_t)(delta-4u));
    pio_enable_sm_mask_in_sync(GATEWAY_PIO,GATEWAY_SM_MASK);
    const bool read=raw_tick(&after);
    s_deadline.origin_before_tick=before;
    s_deadline.origin_after_tick=read && after!=UINT64_MAX ? after+1u : UINT64_MAX;
    if(read && after>=before && after!=UINT64_MAX &&
        after+1u-before <= (uint64_t)BOARD_SYS_CLOCK_HZ*SYNC_IO_GATEWAY_ORIGIN_MAX_NS/1000000000u)
        return true;
    ++s_arm_reject[2];
    pio_sm_set_enabled(GATEWAY_PIO,OUTPUT_SM,false);
    pio_sm_set_enabled(GATEWAY_PIO,READY_SM,false);
    uint64_t stopped;
    s_deadline.state=raw_tick(&stopped) && stopped<deadline ?
        SYNC_IO_GATEWAY_DEADLINE_CANCELLED:SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
    return false;
}

static bool enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_busy,&idle,1u,
        false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void leave(void){__atomic_store_n(&s_busy,0u,__ATOMIC_RELEASE);}
static bool load(void *context,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)context;
    if(!d || d->id!=SYNC_IO_PERSONA_ID_GATEWAY || dma ||
        d->gpio_read_mask!=(1u<<s_gateway.input_pin) || d->gpio_write_mask!=(1u<<s_gateway.output_pin) ||
        pio_sm_is_claimed(GATEWAY_PIO,OUTPUT_SM) || pio_sm_is_claimed(GATEWAY_PIO,READY_SM)) return false;
    pio_sm_claim(GATEWAY_PIO,OUTPUT_SM);s_claimed|=1u<<OUTPUT_SM;
    pio_sm_claim(GATEWAY_PIO,READY_SM);s_claimed|=1u<<READY_SM;
    if(!pio_can_add_program(GATEWAY_PIO,&sync_gateway_output_program)) return false;
    s_output_offset=pio_add_program(GATEWAY_PIO,&sync_gateway_output_program);s_loaded|=1u;
    /* Test again after the first real allocation: fragmentation and other
     * resident personas, not a static sum, determine whether this fits. */
    if(!pio_can_add_program(GATEWAY_PIO,&sync_gateway_ready_program)) return false;
    s_ready_offset=pio_add_program(GATEWAY_PIO,&sync_gateway_ready_program);s_loaded|=2u;
    const uint pin=s_gateway.output_pin;
    s_saved_ctrl=io_bank0_hw->io[pin].ctrl;s_saved_pad=pads_bank0_hw->io[pin];
    s_saved_direction=gpio_get_dir(pin);
    s_saved_latch=(sio_hw->gpio_out&(1u<<pin))!=0u;
    s_saved_pio_level=(GATEWAY_PIO->dbg_padout&(1u<<pin))!=0u;
    s_saved_pio_direction=(GATEWAY_PIO->dbg_padoe&(1u<<pin))!=0u;
    s_pin_saved=true;return true;
}
static bool arm(void *context,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)context;(void)d;(void)dma;
    if(s_loaded!=3u || s_claimed!=GATEWAY_SM_MASK || !s_pin_saved) return false;
    pio_sm_set_enabled(GATEWAY_PIO,OUTPUT_SM,false);
    pio_sm_set_enabled(GATEWAY_PIO,READY_SM,false);
    pio_sm_config output=sync_gateway_output_program_get_default_config(s_output_offset);
    sm_config_set_set_pins(&output,s_gateway.output_pin,1u);
    sm_config_set_out_shift(&output,true,false,32u);
    sm_config_set_clkdiv_int_frac(&output,1u,0u);
    if(pio_sm_init(GATEWAY_PIO,OUTPUT_SM,s_output_offset,&output)!=0) return false;
    pio_sm_config ready=sync_gateway_ready_program_get_default_config(s_ready_offset);
    sm_config_set_in_pins(&ready,s_gateway.input_pin);
    sm_config_set_clkdiv_int_frac(&ready,1u,0u);
    if(pio_sm_init(GATEWAY_PIO,READY_SM,s_ready_offset,&ready)!=0) return false;
    pio_sm_clear_fifos(GATEWAY_PIO,OUTPUT_SM);pio_sm_clear_fifos(GATEWAY_PIO,READY_SM);
    pio_sm_restart(GATEWAY_PIO,OUTPUT_SM);pio_sm_restart(GATEWAY_PIO,READY_SM);
    pio_sm_exec(GATEWAY_PIO,READY_SM,pio_encode_mov_not(pio_x,pio_null));
    pio_interrupt_clear(GATEWAY_PIO,SYNC_IO_GATEWAY_GRANT_IRQ);
    /* Prepare a low output before routing the pad to PIO0. Never queue a
     * pulse or enable either SM in this lifecycle-only implementation. */
    pio_sm_set_pins_with_mask(GATEWAY_PIO,OUTPUT_SM,0u,1u<<s_gateway.output_pin);
    pio_sm_set_consecutive_pindirs(GATEWAY_PIO,OUTPUT_SM,s_gateway.output_pin,1u,true);
    gpio_put(s_gateway.output_pin,false);
    gpio_set_dir(s_gateway.output_pin,GPIO_OUT);
    gpio_set_function(s_gateway.output_pin,GPIO_FUNC_PIO0);
    s_pin_owned=true;
    return true;
}
static void __not_in_flash_func(safe_stop)(void)
{
    __atomic_store_n(&s_realtime_pending,0u,__ATOMIC_RELEASE);
    s_ready_active=false;
    if(s_claimed&(1u<<OUTPUT_SM)) {
        pio_sm_set_enabled(GATEWAY_PIO,OUTPUT_SM,false);
        if(s_pin_owned) pio_sm_set_pins_with_mask(GATEWAY_PIO,OUTPUT_SM,0u,1u<<s_gateway.output_pin);
        pio_sm_clear_fifos(GATEWAY_PIO,OUTPUT_SM);pio_sm_restart(GATEWAY_PIO,OUTPUT_SM);
    }
    if(s_claimed&(1u<<READY_SM)) {
        pio_sm_set_enabled(GATEWAY_PIO,READY_SM,false);
        pio_sm_clear_fifos(GATEWAY_PIO,READY_SM);pio_sm_restart(GATEWAY_PIO,READY_SM);
    }
    if(s_loaded) pio_interrupt_clear(GATEWAY_PIO,SYNC_IO_GATEWAY_GRANT_IRQ);
    if(s_pin_owned) gpio_put(s_gateway.output_pin,false);
}
static void cleanup(void *context,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)context;(void)d;(void)dma;safe_stop();
    if(s_pin_saved && s_pin_owned) {
        const uint pin=s_gateway.output_pin;
        /* gpio_set_function also overwrites CTRL overrides and PAD IE/OD/ISO.
         * Restore complete registers, not just mux/pulls. Disable the pad
         * across the mux transition so restored overrides cannot glitch an
         * originally disabled output before its original PAD is restored. */
        pads_bank0_hw->io[pin]|=PADS_BANK0_GPIO0_OD_BITS;
        /* PIO output/OE latches are separate from SIO. Restore the selected
         * bit while our disabled SM is still claimed, including when the
         * prior GPIO mux already selected PIO0. Leave every other pad alone. */
        pio_sm_set_pins_with_mask(GATEWAY_PIO,OUTPUT_SM,
            s_saved_pio_level ? 1u<<pin : 0u,1u<<pin);
        pio_sm_set_pindirs_with_mask(GATEWAY_PIO,OUTPUT_SM,
            s_saved_pio_direction ? 1u<<pin : 0u,1u<<pin);
        gpio_put(pin,s_saved_latch);gpio_set_dir(pin,s_saved_direction);
        io_bank0_hw->io[pin].ctrl=s_saved_ctrl;
        pads_bank0_hw->io[pin]=s_saved_pad;
    }
    if(s_loaded&2u) pio_remove_program(GATEWAY_PIO,&sync_gateway_ready_program,s_ready_offset);
    if(s_loaded&1u) pio_remove_program(GATEWAY_PIO,&sync_gateway_output_program,s_output_offset);
    s_loaded=0u;
    if(s_claimed&(1u<<READY_SM)) pio_sm_unclaim(GATEWAY_PIO,READY_SM);
    if(s_claimed&(1u<<OUTPUT_SM)) pio_sm_unclaim(GATEWAY_PIO,OUTPUT_SM);
    s_claimed=0u;
    s_pin_saved=false;s_pin_owned=false;
}
static bool matching(uint32_t generation)
{return s_lease && generation && generation==s_gateway.generation;}
bool sync_io_gateway_load(uint32_t input_port,uint32_t output_port,uint32_t pulse_ns,uint32_t *generation)
{
    const uint64_t product=(uint64_t)pulse_ns*BOARD_SYS_CLOCK_HZ;
    if(!generation || input_port<1u || input_port>BOARD_SYNC_INPUT_PIN_COUNT ||
        output_port<1u || output_port>BOARD_SYNC_OUTPUT_PIN_COUNT || !pulse_ns ||
        product%UINT64_C(1000000000) || product/UINT64_C(1000000000)<2u ||
        product/UINT64_C(1000000000)>UINT32_MAX || !enter()) return false;
    if(s_lease || s_generation==UINT32_MAX || clock_get_hz(clk_sys)!=BOARD_SYS_CLOCK_HZ ||
        !sync_io_core_gateway_reserve(&s_gateway)) {leave();return false;}
    const sync_io_gateway_snapshot_t previous=s_gateway;
    memset(&s_gateway,0,sizeof(s_gateway));s_gateway.schema=SYNC_IO_GATEWAY_SCHEMA;
    s_gateway.input_port=input_port;s_gateway.output_port=output_port;
    s_gateway.input_pin=BOARD_SYNC_INPUT_BASE_PIN+
        (BOARD_SYNC_INPUT_BITS_REVERSED ? BOARD_SYNC_INPUT_PIN_COUNT-input_port : input_port-1u);
    s_gateway.output_pin=BOARD_SYNC_OUTPUT_BASE_PIN+output_port-1u;
    s_gateway.pulse_ns=pulse_ns;s_gateway.pulse_ticks=(uint32_t)(product/UINT64_C(1000000000));
    s_gateway.tick_hz=BOARD_SYS_CLOCK_HZ;
    const sync_io_persona_manager_hooks_t hooks={.load=load,.arm=arm,.cleanup=cleanup};
    if(!sync_io_pio0_runtime_prepare_io(SYNC_IO_PERSONA_ID_GATEWAY,1u<<s_gateway.input_pin,
            1u<<s_gateway.output_pin,&hooks,NULL,&s_handle)) {
        (void)sync_io_core_gateway_release(&s_gateway);s_gateway=previous;leave();return false;
    }
    s_lease=true;s_gateway.generation=++s_generation;s_gateway.state=SYNC_IO_GATEWAY_LOADED;
    memset(&s_deadline,0,sizeof(s_deadline));
    __atomic_store_n(&s_realtime_pending,0u,__ATOMIC_RELEASE);s_ready_active=false;
    *generation=s_gateway.generation;leave();return true;
}
bool sync_io_gateway_stop(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation)) {leave();return false;}
    safe_stop();s_gateway.state=SYNC_IO_GATEWAY_STOPPED;
    if(s_deadline.state==SYNC_IO_GATEWAY_DEADLINE_ARMED) {
        uint64_t stopped;
        s_deadline.state=raw_tick(&stopped) && stopped<s_deadline.deadline_tick ?
            SYNC_IO_GATEWAY_DEADLINE_CANCELLED:SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
    }
    leave();return true;
}
bool sync_io_gateway_release(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation) || !sync_io_pio0_runtime_release(&s_handle)) {leave();return false;}
    s_lease=false;const bool ok=sync_io_core_gateway_release(&s_gateway);
    s_gateway.state=SYNC_IO_GATEWAY_IDLE;leave();return ok;
}
bool sync_io_gateway_snapshot(sync_io_gateway_snapshot_t *out)
{
    if(!out || !enter()) return false;
    sync_io_gateway_snapshot_t snapshot=s_gateway;
    snapshot.schema=SYNC_IO_GATEWAY_SCHEMA;snapshot.loaded=s_lease;
    if(s_lease) {
        snapshot.input_pin=(GATEWAY_PIO->sm[READY_SM].pinctrl&PIO_SM0_PINCTRL_IN_BASE_BITS)>>
            PIO_SM0_PINCTRL_IN_BASE_LSB;
        snapshot.output_pin=(GATEWAY_PIO->sm[OUTPUT_SM].pinctrl&PIO_SM0_PINCTRL_SET_BASE_BITS)>>
            PIO_SM0_PINCTRL_SET_BASE_LSB;
    }
    snapshot.sm_claimed_mask=0u;
    for(uint sm=0u;sm<4u;++sm)
        if((s_claimed&(1u<<sm)) && pio_sm_is_claimed(GATEWAY_PIO,sm)) snapshot.sm_claimed_mask|=1u<<sm;
    snapshot.sm_enabled_mask=GATEWAY_PIO->ctrl&s_claimed;
    snapshot.instruction_words=((s_loaded&1u)?SYNC_IO_GATEWAY_OUTPUT_WORDS:0u)+
        ((s_loaded&2u)?SYNC_IO_GATEWAY_READY_WORDS:0u);
    snapshot.output_program_offset=(s_loaded&1u)?s_output_offset:0u;
    snapshot.ready_program_offset=(s_loaded&2u)?s_ready_offset:0u;
    snapshot.grant_pending=s_lease && pio_interrupt_get(GATEWAY_PIO,SYNC_IO_GATEWAY_GRANT_IRQ);
    snapshot.ready_level=snapshot.input_port ? gpio_get(snapshot.input_pin) : 0u;
    snapshot.output_level=snapshot.output_port ? gpio_get(snapshot.output_pin) : 0u;
    snapshot.output_tx_level=s_lease ? pio_sm_get_tx_fifo_level(GATEWAY_PIO,OUTPUT_SM) : 0u;
    snapshot.output_rx_level=s_lease ? pio_sm_get_rx_fifo_level(GATEWAY_PIO,OUTPUT_SM) : 0u;
    snapshot.ready_collision_count=s_ready_collision_count;
    snapshot.ready_margin_min_ticks=s_ready_margin_min_ticks;
    snapshot.ready_tx_level=s_lease ? pio_sm_get_tx_fifo_level(GATEWAY_PIO,READY_SM) : 0u;
    snapshot.ready_rx_level=s_lease ? pio_sm_get_rx_fifo_level(GATEWAY_PIO,READY_SM) : 0u;
    *out=snapshot;leave();return true;
}

static bool prepare_realtime(uint32_t pulse_ns)
{
    const uint64_t product=(uint64_t)pulse_ns*BOARD_SYS_CLOCK_HZ;
    const uint64_t pulse_ticks=product/UINT64_C(1000000000);
    if(get_core_num()!=1u || !s_lease || s_loaded!=3u || !s_pin_owned ||
       !pulse_ns || product%UINT64_C(1000000000) || pulse_ticks<2u ||
       pulse_ticks>UINT32_MAX || s_gateway.trigger_count==UINT32_MAX ||
       __atomic_load_n(&s_realtime_pending,__ATOMIC_ACQUIRE)) return false;
    /* Drain an already captured late READY before beginning the next local diagnostic
     * window. An absent READY never blocks a new action, but an action that starts while the
     * previous READY is still missing IS a time collision and is counted: the schedule owes
     * exactly one READY per TRIG, and the next action may not overtake it. */
    if (s_ready_active) {
        ++s_ready_collision_count;
        if (!s_ready_required) s_ready_active = false;
    }
    sync_io_gateway_realtime_service_core1();
    pio_sm_set_enabled(GATEWAY_PIO,OUTPUT_SM,false);
    pio_sm_set_enabled(GATEWAY_PIO,READY_SM,false);
    pio_sm_clear_fifos(GATEWAY_PIO,OUTPUT_SM);
    pio_sm_clear_fifos(GATEWAY_PIO,READY_SM);
    pio_sm_restart(GATEWAY_PIO,OUTPUT_SM);
    pio_sm_restart(GATEWAY_PIO,READY_SM);
    pio_sm_exec(GATEWAY_PIO,OUTPUT_SM,pio_encode_jmp(s_output_offset));
    pio_sm_exec(GATEWAY_PIO,READY_SM,pio_encode_jmp(s_ready_offset));
    pio_sm_exec(GATEWAY_PIO,READY_SM,pio_encode_mov_not(pio_x,pio_null));
    pio_interrupt_clear(GATEWAY_PIO,SYNC_IO_GATEWAY_GRANT_IRQ);
    pio_sm_put(GATEWAY_PIO,OUTPUT_SM,(uint32_t)pulse_ticks-2u);
    pio_sm_exec(GATEWAY_PIO,OUTPUT_SM,pio_encode_pull(false,true));
    pio_sm_exec(GATEWAY_PIO,OUTPUT_SM,pio_encode_mov(pio_y,pio_osr));
    pio_sm_put(GATEWAY_PIO,OUTPUT_SM,s_gateway.trigger_count+1u);
    pio_sm_exec(GATEWAY_PIO,OUTPUT_SM,pio_encode_pull(false,true));
    pio_sm_exec(GATEWAY_PIO,OUTPUT_SM,pio_encode_mov(pio_isr,pio_osr));
    s_ready_active=false;
    return true;
}

bool sync_io_gateway_realtime_fire(uint32_t pulse_ns)
{
    s_plan_count=0u;s_plan_next=0u;
    if(!prepare_realtime(pulse_ns))return false;
    pio_sm_put(GATEWAY_PIO,OUTPUT_SM,0u);
    pio_enable_sm_mask_in_sync(GATEWAY_PIO,GATEWAY_SM_MASK);
    __atomic_store_n(&s_realtime_pending,1u,__ATOMIC_RELEASE);
    ++s_gateway.trigger_count;s_ready_active=true;
    s_deadline.state=SYNC_IO_GATEWAY_DEADLINE_IDLE;
    return true;
}

/* Polled every T2 slice while a deadline is armed: keep it out of XIP. */
void __not_in_flash_func(sync_io_gateway_realtime_service_core1)(void)
{
    if(get_core_num()!=1u || !s_lease)return;
    if(__atomic_load_n(&s_realtime_pending,__ATOMIC_ACQUIRE) &&
       !pio_sm_is_rx_fifo_empty(GATEWAY_PIO,OUTPUT_SM)) {
        const uint32_t receipt=pio_sm_get(GATEWAY_PIO,OUTPUT_SM);
        __atomic_store_n(&s_realtime_pending,0u,__ATOMIC_RELEASE);
        pio_sm_set_enabled(GATEWAY_PIO,OUTPUT_SM,false);
        if(receipt!=s_pending_receipt) {
            ++s_service_bad_receipt;
            safe_stop();s_deadline.state=SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
            return;
        }
        ++s_service_complete;
        s_deadline.completed_sequence=s_pending_sequence;
        if(s_deadline.state==SYNC_IO_GATEWAY_DEADLINE_ARMED)
            s_deadline.state=SYNC_IO_GATEWAY_DEADLINE_COMPLETE;
        /* Next group of the staged table: armed now, at its own absolute tick, so the spacing
         * between pulses is decided by the table rather than by this service pass. */
        if(s_plan_next<s_plan_count) {
            const gateway_plan_group_t group=s_plan[s_plan_next++];
            s_sequence_override=s_plan_base_sequence+s_plan_next-1u;
            const bool armed=sync_io_gateway_realtime_schedule_at(group.pulse_ns,group.deadline_tick);
            s_sequence_override=0u;
            if(!armed) {
                s_plan_count=0u;s_plan_next=0u;
                s_deadline.state=SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
            }
        } else s_plan_count=0u;
    } else if(__atomic_load_n(&s_realtime_pending,__ATOMIC_ACQUIRE)) {
        ++s_service_wait;
    }
    /* One READY per transaction; continue after the pulse completion. */
    if(s_ready_active && !pio_sm_is_rx_fifo_empty(GATEWAY_PIO,READY_SM)) {
        (void)pio_sm_get(GATEWAY_PIO,READY_SM);
        if(s_gateway.ready_count!=UINT32_MAX)++s_gateway.ready_count;
        /* Margin to the action armed NEXT (if any): a tiny or negative margin is exactly the
         * "READY vs next action" collision the sequence layout has to avoid. */
        uint64_t ready_tick;
        if(raw_tick(&ready_tick)) {
            /* The next action is the deadline currently armed in this backend (a queued
             * group, once the sequence path exists, appears here through the same field). */
            uint64_t next_deadline=0u;
            if(s_deadline.state==SYNC_IO_GATEWAY_DEADLINE_ARMED)
                next_deadline=s_deadline.deadline_tick;
            if(next_deadline>ready_tick) {
                const uint64_t margin=next_deadline-ready_tick;
                const uint32_t margin_ticks = margin>UINT32_MAX ? UINT32_MAX : (uint32_t)margin;
                if(!s_ready_margin_min_ticks || margin_ticks<s_ready_margin_min_ticks)
                    s_ready_margin_min_ticks=margin_ticks;
            }else if(next_deadline) ++s_ready_collision_count;
        }
        s_ready_active=false;pio_sm_set_enabled(GATEWAY_PIO,READY_SM,false);
    }
}

/* Runs on the T2 arming slice, the busiest one: keep it out of XIP. */
bool __not_in_flash_func(sync_io_gateway_realtime_schedule_at)(uint32_t pulse_ns,uint64_t deadline_tick)
{
    if(get_core_num()!=1u || clock_get_hz(clk_sys)!=BOARD_SYS_CLOCK_HZ ||
       timer1_hw->pause || timer1_hw->source!=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS) {
        ++s_arm_reject[0];
        return false;
    }
    if(!prepare_realtime(pulse_ns)) { ++s_arm_reject[1]; return false; }
    s_deadline.state=SYNC_IO_GATEWAY_DEADLINE_CANCELLED;
    s_pending_receipt=s_gateway.trigger_count+1u;
    s_pending_sequence=s_sequence_override?s_sequence_override:s_pending_receipt;
    s_deadline.deadline_tick=deadline_tick;s_deadline.sequence=s_pending_sequence;
    s_deadline.origin_before_tick=s_deadline.origin_after_tick=0u;
    if(!start_deadline(deadline_tick)) {
        if(s_deadline.state==SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN)safe_stop();
        pio_sm_clear_fifos(GATEWAY_PIO,OUTPUT_SM);
        pio_sm_clear_fifos(GATEWAY_PIO,READY_SM);
        return false;
    }
    ++s_gateway.trigger_count;s_ready_active=true;
    __atomic_store_n(&s_realtime_pending,1u,__ATOMIC_RELEASE);
    s_deadline.state=SYNC_IO_GATEWAY_DEADLINE_ARMED;
    return true;
}

/* Accept the whole action table of one position. The groups' absolute ticks were solved on the
 * shared timeline, so this call does not need to know the action period. */
void sync_io_gateway_realtime_set_ready_required(bool required)
{
    s_ready_required = required;
}

bool sync_io_gateway_realtime_ready_required(void)
{
    return s_ready_required;
}

bool __not_in_flash_func(sync_io_gateway_realtime_stage_sequence)(uint64_t base_deadline_tick,
    const uint32_t *pulse_ns,const uint64_t *delta_ticks,uint32_t count)
{
    if(get_core_num()!=1u || !s_lease) { ++s_stage_reject[0]; return false; }
    if(count==0u || count>GATEWAY_PLAN_MAX || pulse_ns==NULL || delta_ticks==NULL)
        { ++s_stage_reject[1]; return false; }
    if(delta_ticks[0]!=0u) { ++s_stage_reject[2]; return false; }
    for(uint32_t i=1u;i<count;++i)
        if(delta_ticks[i]<=delta_ticks[i-1u]) { ++s_stage_reject[3]; return false; }
    /* Refuse before touching the backend: a rejected table must not leave a half-armed pulse. */
    if(s_plan_count) { ++s_stage_reject[4]; return false; }
    if(s_ready_active && s_ready_required) { ++s_stage_reject[5]; return false; }
    if(s_ready_active) { ++s_ready_collision_count; s_ready_active=false; }
    for(uint32_t i=0u;i<count;++i) {
        if(pulse_ns[i]==0u) { ++s_stage_reject[6]; return false; }
        if(base_deadline_tick>UINT64_MAX-delta_ticks[i]) { ++s_stage_reject[6]; return false; }
        s_plan[i].pulse_ns=pulse_ns[i];
        s_plan[i].deadline_tick=base_deadline_tick+delta_ticks[i];
    }
    s_plan_count=count;s_plan_next=0u;
    /* The staged table owns the group numbering: group k reports sequence base + k. */
    s_plan_base_sequence=s_gateway.trigger_count+1u;
    const gateway_plan_group_t first=s_plan[s_plan_next++];
    s_sequence_override=s_plan_base_sequence;
    const bool armed=sync_io_gateway_realtime_schedule_at(first.pulse_ns,first.deadline_tick);
    s_sequence_override=0u;
    if(!armed) {
        ++s_stage_reject[7];
        s_plan_count=0u;s_plan_next=0u;
        return false;
    }
    return true;
}

void sync_io_gateway_realtime_diag(uint32_t *out)
{
    if(!out)return;
    out[0]=s_plan_count;
    out[1]=s_ready_active?1u:0u;
    out[2]=s_ready_required?1u:0u;
    out[3]=s_gateway.pulse_ns;
    out[4]=s_ready_collision_count;
    out[5]=s_ready_margin_min_ticks;
    out[14]=s_late_count;
    for(uint32_t i=0u;i<4u;++i) out[15u+i]=s_arm_reject[i];
    out[19]=s_service_wait;
    out[20]=s_service_bad_receipt;
    out[21]=s_service_complete;
    out[22]=s_plan_next;
    for(uint32_t i=0u;i<GATEWAY_STAGE_REJECT_COUNT;++i) out[6u+i]=s_stage_reject[i];
}

void sync_io_gateway_realtime_stage_reject(uint32_t *out)
{
    if(!out)return;
    for(uint32_t i=0u;i<GATEWAY_STAGE_REJECT_COUNT;++i) out[i]=s_stage_reject[i];
}

bool __not_in_flash_func(sync_io_gateway_realtime_status)(sync_io_gateway_deadline_status_t *out)
{
    if(get_core_num()!=1u || !out)return false;
    *out=s_deadline;out->ready_count=s_gateway.ready_count;
    out->pending=__atomic_load_n(&s_realtime_pending,__ATOMIC_ACQUIRE);
    return true;
}

bool __not_in_flash_func(sync_io_gateway_realtime_cancel)(void)
{
    if(get_core_num()!=1u || !s_lease)return false;
    safe_stop();
    if(s_deadline.state==SYNC_IO_GATEWAY_DEADLINE_ARMED) {
        uint64_t stopped;
        s_deadline.state=raw_tick(&stopped) && stopped<s_deadline.deadline_tick ?
            SYNC_IO_GATEWAY_DEADLINE_CANCELLED:SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
    }
    s_gateway.state=SYNC_IO_GATEWAY_STOPPED;
    return true;
}
