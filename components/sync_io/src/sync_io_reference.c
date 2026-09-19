#include "sync_io_reference.h"
#include "sync_io_reference_math.h"
#include "sync_io_pio0_runtime.h"
#include "sync_io_core_internal.h"
#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "sync_reference_cycles.pio.h"
#include <string.h>

#define REF_PIO BOARD_SYNC_PIO_FAST
#define REF_SM 2u
#define REF_POP0_DMA SYNC_IO_REFERENCE_POP_DMA_CH
#define REF_STAMP0_DMA SYNC_IO_REFERENCE_STAMP_DMA_CH
#define REF_DMA_MASK SYNC_IO_REFERENCE_DMA_MASK
static sync_io_reference_snapshot_t s_ref;
static uint32_t s_words[sizeof(s_ref)/4u],s_guard,s_state,s_cancel,s_generation;
static sync_io_persona_manager_handle_t s_handle;
static volatile uint32_t s_stamps[2],s_tokens[2];
static uint16_t s_instructions[11];
static struct pio_program s_program;
static uint32_t s_offset;
static uint64_t s_started;
static bool s_sm,s_stamp_dma,s_pop_dma,s_loaded,s_lease;
static bool s_aborting,s_restart;
_Static_assert(sizeof(s_ref)%4u==0u,"atomic word snapshot");
_Static_assert(sizeof(sync_reference_cycles_program_instructions)==sizeof(s_instructions),
    "reference persona instruction count");

bool sync_io_reference_config_valid(const sync_io_reference_config_t *c)
{ uint32_t n;return sync_io_reference_validate(c,&n); }
static void publish(void)
{
    __atomic_add_fetch(&s_guard,1u,__ATOMIC_ACQ_REL);
    for(uint32_t i=0;i<sizeof(s_ref)/4u;++i) {
        uint32_t w;memcpy(&w,(uint8_t *)&s_ref+4u*i,4u);
        __atomic_store_n(s_words+i,w,__ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&s_guard,1u,__ATOMIC_RELEASE);
    __atomic_store_n(&s_state,s_ref.state,__ATOMIC_RELEASE);
}
static bool now_raw(uint64_t *now)
{
    if(clock_get_hz(clk_sys)!=BOARD_SYS_CLOCK_HZ || timer1_hw->pause ||
        timer1_hw->source!=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS)
        return false;
    const uint32_t high=timer1_hw->timerawh,low=timer1_hw->timerawl;
    if(high!=timer1_hw->timerawh) return false;
    *now=((uint64_t)high<<32u)|low;return true;
}
static bool load(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;
    if(!d || d->id!=SYNC_IO_PERSONA_ID_REFERENCE_MONITOR || dma!=REF_DMA_MASK ||
        pio_sm_is_claimed(REF_PIO,REF_SM) || dma_channel_is_claimed(REF_POP0_DMA) ||
        dma_channel_is_claimed(REF_STAMP0_DMA) || dma_channel_is_busy(REF_POP0_DMA) ||
        dma_channel_is_busy(REF_STAMP0_DMA) || (dma_hw->abort&REF_DMA_MASK)) return false;
    memcpy(s_instructions,sync_reference_cycles_program.instructions,sizeof(s_instructions));
    s_instructions[2]=s_instructions[6]=pio_encode_wait_pin(s_ref.config.edge!=0u,0u);
    s_instructions[3]=s_instructions[7]=pio_encode_wait_pin(s_ref.config.edge==0u,0u);
    s_program=sync_reference_cycles_program;s_program.instructions=s_instructions;
    if(!pio_can_add_program(REF_PIO,&s_program)) return false;
    pio_sm_claim(REF_PIO,REF_SM);s_sm=true;
    dma_channel_claim(REF_STAMP0_DMA);s_stamp_dma=true;
    dma_channel_claim(REF_POP0_DMA);s_pop_dma=true;
    s_offset=pio_add_program(REF_PIO,&s_program);s_loaded=true;return true;
}
static bool arm(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;(void)d;(void)dma;
    pio_sm_config c=sync_reference_cycles_program_get_default_config(s_offset);
    sm_config_set_in_pins(&c,s_ref.input_pin);
    sm_config_set_out_shift(&c,true,false,32u);
    sm_config_set_clkdiv_int_frac(&c,1u,0u);
    if(pio_sm_init(REF_PIO,REF_SM,s_offset,&c)!=0) return false;
    /* Reading a pad through PIO needs no mux change. Preserve the existing
     * input owner configuration and synchronizer; never drive the reference. */
    for(uint32_t ch=REF_POP0_DMA;ch<=REF_STAMP0_DMA;++ch) {
        if(dma_channel_is_busy(ch) || (dma_hw->abort&(1u<<ch))) return false;
        /* Retired channels retain sticky bus faults across unclaim/claim.
         * AL1_CTRL is nontriggering; write W1C fault bits with EN=0 only after
         * ownership and quiescence. Ordinary configuration writes zero to
         * these bits and therefore do not clear a previous fault. */
        const dma_channel_config clear_faults={.ctrl=DMA_CH0_CTRL_TRIG_READ_ERROR_BITS|
            DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS};
        dma_channel_set_config(ch,&clear_faults,false);
        dma_channel_set_irq0_enabled(ch,false);dma_channel_set_irq1_enabled(ch,false);
    }
    return true;
}
static void cleanup(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;(void)d;(void)dma;
    if(s_sm) {pio_sm_set_enabled(REF_PIO,REF_SM,false);pio_sm_clear_fifos(REF_PIO,REF_SM);}
    if(s_loaded) {pio_remove_program(REF_PIO,&s_program,s_offset);s_loaded=false;}
    if(s_stamp_dma) {dma_channel_unclaim(REF_STAMP0_DMA);s_stamp_dma=false;}
    if(s_pop_dma) {dma_channel_unclaim(REF_POP0_DMA);s_pop_dma=false;}
    if(s_sm) {pio_sm_unclaim(REF_PIO,REF_SM);s_sm=false;}
}
bool sync_io_reference_prepare(const sync_io_reference_config_t *c,uint32_t *generation)
{
    uint32_t n;uint64_t now;
    if(get_core_num()!=0u || __atomic_load_n(&s_state,__ATOMIC_ACQUIRE)!=SYNC_IO_REFERENCE_IDLE)
        return false;
    s_ref.schema=SYNC_IO_REFERENCE_SCHEMA;s_ref.state=SYNC_IO_REFERENCE_IDLE;s_ref.valid=0u;
    if(!generation || !sync_io_reference_validate(c,&n) || s_generation==UINT32_MAX) {
        s_ref.reason=SYNC_IO_REFERENCE_INVALID_CONFIG;publish();return false;
    }
    if(clock_get_hz(clk_sys)!=BOARD_SYS_CLOCK_HZ ||
        (uint64_t)BOARD_SYS_CLOCK_HZ*c->timeout_ms/1000u>=UINT32_MAX ||
        !now_raw(&now)) {s_ref.reason=SYNC_IO_REFERENCE_CLOCK_ERROR;publish();return false;}
    if(!sync_io_core_reference_reserve(&s_ref)) {
        s_ref.reason=SYNC_IO_REFERENCE_ADMISSION_BUSY;publish();return false;
    }
    memset(&s_ref,0,sizeof(s_ref));s_ref.schema=SYNC_IO_REFERENCE_SCHEMA;s_ref.config=*c;
    s_ref.tick_hz=BOARD_SYS_CLOCK_HZ;s_ref.reference_cycles=n;
    s_ref.input_pin=BOARD_SYNC_INPUT_BASE_PIN+
        (BOARD_SYNC_INPUT_BITS_REVERSED ? 4u-c->input_port : c->input_port-1u);
    s_ref.pio_bias_ticks=SYNC_IO_REFERENCE_PIO_BIAS_TICKS;
    s_ref.measurement_flags=SYNC_IO_REFERENCE_DMA_LATENCY_UNBOUNDED;
    const sync_io_persona_manager_hooks_t hooks={.load=load,.arm=arm,.cleanup=cleanup};
    if(!sync_io_pio0_runtime_prepare(SYNC_IO_PERSONA_ID_REFERENCE_MONITOR,&hooks,NULL,&s_handle)) {
        (void)sync_io_core_reference_release(&s_ref);
        s_ref.reason=SYNC_IO_REFERENCE_PERSONA_UNAVAILABLE;publish();return false;
    }
    s_lease=true;s_aborting=false;s_restart=false;
    s_ref.generation=++s_generation;s_ref.state=SYNC_IO_REFERENCE_PREPARED;
    __atomic_store_n(&s_cancel,0u,__ATOMIC_RELEASE);publish();
    *generation=s_ref.generation;return true;
}
void sync_io_reference_cancel(void) { __atomic_store_n(&s_cancel,1u,__ATOMIC_RELEASE); }
static void begin_abort(uint32_t reason,bool restart)
{
    pio_sm_set_enabled(REF_PIO,REF_SM,false);
    hw_clear_bits(&dma_hw->ch[REF_POP0_DMA].ctrl_trig,DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[REF_STAMP0_DMA].ctrl_trig,DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_hw->abort=REF_DMA_MASK; /* No synchronous dma_channel_abort busy wait. */
    s_aborting=true;s_restart=restart;s_ref.reason=reason;
    if(reason!=SYNC_IO_REFERENCE_OK) s_ref.valid=0u;
    publish();
}
static void start_window(uint64_t now)
{
    pio_sm_set_enabled(REF_PIO,REF_SM,false);
    pio_sm_clear_fifos(REF_PIO,REF_SM);pio_sm_restart(REF_PIO,REF_SM);
    pio_sm_exec(REF_PIO,REF_SM,pio_encode_jmp(s_offset));
    s_stamps[0]=s_stamps[1]=s_tokens[0]=s_tokens[1]=0u;
    /* POP_DMA alone consumes RX DREQ and pops one token, then chains unpaced
     * STAMP_DMA to read TIMER1. Each trigger reloads count=1 (RP2350 TRANS_COUNT
     * COUNT / DBG_TCR contract); write addresses advance. After timestamp
     * two, POP_DMA waits for nonexistent token three because PIO blocks at pull.
     * Never share RX DREQ with a DMA which does not drain that FIFO. */
    dma_channel_config stamp=dma_channel_get_default_config(REF_STAMP0_DMA);
    channel_config_set_transfer_data_size(&stamp,DMA_SIZE_32);
    channel_config_set_read_increment(&stamp,false);channel_config_set_write_increment(&stamp,true);
    channel_config_set_dreq(&stamp,DREQ_FORCE);
    channel_config_set_chain_to(&stamp,REF_POP0_DMA);
    dma_channel_config pop=dma_channel_get_default_config(REF_POP0_DMA);
    channel_config_set_transfer_data_size(&pop,DMA_SIZE_32);
    channel_config_set_read_increment(&pop,false);channel_config_set_write_increment(&pop,true);
    channel_config_set_dreq(&pop,pio_get_dreq(REF_PIO,REF_SM,false));
    channel_config_set_chain_to(&pop,REF_STAMP0_DMA);
    dma_channel_configure(REF_STAMP0_DMA,&stamp,s_stamps,&timer1_hw->timerawl,1u,false);
    dma_channel_configure(REF_POP0_DMA,&pop,s_tokens,&REF_PIO->rxf[REF_SM],1u,false);
    pio_sm_put(REF_PIO,REF_SM,s_ref.reference_cycles-1u);
    s_started=now;s_ref.deadline_raw=now+(uint64_t)s_ref.tick_hz*s_ref.config.timeout_ms/1000u;
    /* Keep TIMEOUT/invalid visible across retries until a full new window. */
    s_ref.state=SYNC_IO_REFERENCE_RUNNING;
    dma_start_channel_mask(1u<<REF_POP0_DMA);pio_sm_set_enabled(REF_PIO,REF_SM,true);
    publish();
}
void sync_io_reference_service_core1(void)
{
    if(get_core_num()!=1u) return;
    const uint32_t state=__atomic_load_n(&s_state,__ATOMIC_ACQUIRE);
    if(state==SYNC_IO_REFERENCE_IDLE || state==SYNC_IO_REFERENCE_RETIRED) return;
    const bool cancelled=__atomic_load_n(&s_cancel,__ATOMIC_ACQUIRE)!=0u;
    if(s_aborting) {
        if(dma_hw->abort&REF_DMA_MASK) return;
        for(uint ch=REF_POP0_DMA;ch<=REF_STAMP0_DMA;ch++)
            if(dma_channel_is_busy(ch)) return;
        s_aborting=false;
        if(!s_restart || cancelled) {
            if(cancelled) {s_ref.reason=SYNC_IO_REFERENCE_CANCELLED;s_ref.valid=0u;}
            s_ref.state=SYNC_IO_REFERENCE_RETIRED;publish();return;
        }
    } else if(cancelled) {begin_abort(SYNC_IO_REFERENCE_CANCELLED,false);return;}
    uint64_t now;
    if(!now_raw(&now)) {begin_abort(SYNC_IO_REFERENCE_CLOCK_ERROR,false);return;}
    const uint32_t error=DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS|
        DMA_CH0_CTRL_TRIG_READ_ERROR_BITS|DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    if((dma_hw->ch[REF_POP0_DMA].ctrl_trig|dma_hw->ch[REF_STAMP0_DMA].ctrl_trig)&error) {
        begin_abort(SYNC_IO_REFERENCE_DMA_ERROR,false);return;
    }
    if(state==SYNC_IO_REFERENCE_PREPARED || s_restart) {
        s_restart=false;start_window(now);return;
    }
    if(now<s_started) {begin_abort(SYNC_IO_REFERENCE_CLOCK_ERROR,false);return;}
    const uintptr_t done=dma_hw->ch[REF_STAMP0_DMA].write_addr;
    const uintptr_t popped=dma_hw->ch[REF_POP0_DMA].write_addr;
    if(done<(uintptr_t)s_stamps || done>(uintptr_t)(s_stamps+2u) ||
        (done-(uintptr_t)s_stamps)%sizeof(uint32_t) ||
        popped<(uintptr_t)s_tokens || popped>(uintptr_t)(s_tokens+2u) ||
        (popped-(uintptr_t)s_tokens)%sizeof(uint32_t)) {
        begin_abort(SYNC_IO_REFERENCE_BAD_RECORD,false);return;
    }
    if(done==(uintptr_t)(s_stamps+2u) && !dma_channel_is_busy(REF_STAMP0_DMA)) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t ticks;int32_t ppb;
        if(s_tokens[0]!=s_ref.reference_cycles-1u || s_tokens[1]!=UINT32_MAX ||
            dma_hw->ch[REF_POP0_DMA].write_addr!=(uintptr_t)(s_tokens+2u) ||
            s_ref.sample_seq==UINT32_MAX) {begin_abort(SYNC_IO_REFERENCE_BAD_RECORD,false);return;}
        /* A signal-loss-extended window is retryable even after both tokens arrive. */
        if(now>s_ref.deadline_raw) {begin_abort(SYNC_IO_REFERENCE_TIMEOUT,true);return;}
        if(!sync_io_reference_evaluate(&s_ref.config,s_ref.tick_hz,s_stamps[0],s_stamps[1],&ticks,&ppb)) {
            begin_abort(SYNC_IO_REFERENCE_BAD_RECORD,false);return;
        }
        s_ref.start_raw32=s_stamps[0];s_ref.end_raw32=s_stamps[1];s_ref.elapsed_ticks=ticks;
        s_ref.frequency_error_ppb=ppb;s_ref.completed_raw=now;s_ref.sample_seq++;s_ref.valid=1u;
        begin_abort(SYNC_IO_REFERENCE_OK,true);return;
    }
    if(now>s_ref.deadline_raw) begin_abort(SYNC_IO_REFERENCE_TIMEOUT,true);
}
bool sync_io_reference_release(uint32_t generation)
{
    if(get_core_num()!=0u || __atomic_load_n(&s_state,__ATOMIC_ACQUIRE)!=SYNC_IO_REFERENCE_RETIRED ||
        generation!=s_ref.generation) return false;
    if(s_lease && !sync_io_pio0_runtime_release(&s_handle)) return false;
    s_lease=false;
    if(!sync_io_core_reference_release(&s_ref)) return false;
    s_ref.state=SYNC_IO_REFERENCE_IDLE;publish();return true;
}
bool sync_io_reference_get_snapshot(sync_io_reference_snapshot_t *out)
{
    if(!out) return false;
    const uint32_t guard=__atomic_load_n(&s_guard,__ATOMIC_ACQUIRE);
    if(guard&1u) return false;
    sync_io_reference_snapshot_t value;
    for(uint32_t i=0;i<sizeof(value)/4u;++i) {
        uint32_t w=__atomic_load_n(s_words+i,__ATOMIC_RELAXED);
        memcpy((uint8_t *)&value+4u*i,&w,4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if(guard!=__atomic_load_n(&s_guard,__ATOMIC_ACQUIRE)) return false;
    *out=value;return true;
}
