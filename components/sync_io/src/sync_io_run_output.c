#include "sync_io_run_output.h"

#include <string.h>
#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "sync_io_core_internal.h"
#include "sync_io_persona_manager.h"
#include "sync_pulse_stream_out1.pio.h"
#include "sync_pulse_stream_encode.h"
#include "sync_pulse_uniform_out1.pio.h"
#include "sync_pulse_uniform_encode.h"

#define RUN_PIO BOARD_SYNC_PIO_FAST
#define RUN_SM BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM
#define RUN_DMA SYNC_IO_MODEL_PULSE_DMA_CH
#define RUN_PIN BOARD_SYNC_OUTPUT_BASE_PIN

_Static_assert(SYNC_IO_RUN_OUTPUT_MAX_WORDS <= SYNC_IO_SHARED_WORKSPACE_WORDS,
               "finite output block must fit the leased DMA workspace");
_Static_assert(SYNC_IO_RUN_OUTPUT_BLOCK_EDGES <= SYNC_IO_RUN_OUTPUT_MAX_EDGES,
               "compatibility output block must fit the counted entry");

static sync_io_persona_manager_t s_manager;
static sync_io_persona_manager_handle_t s_handle;
static sync_io_run_output_snapshot_t s_run;
static uint32_t s_state, s_cancel, s_guard, s_generation;
static uint32_t s_snapshot[sizeof(sync_io_run_output_snapshot_t)/sizeof(uint32_t)];
static uint32_t s_offset, s_duration_ms;
static bool s_sm_claimed, s_dma_claimed, s_loaded, s_lease;
static bool s_source_pending;
enum { RUN_MODE_PAIRED, RUN_MODE_UNIFORM };
static uint32_t s_mode, s_fixed_high_ticks;

/* Keep the bounded cached-refill backend in main SRAM. Attributes on these
 * declarations also apply to their definitions below; noinline prevents a
 * flash caller from absorbing a RAM helper back into its own XIP body. */
static __attribute__((noinline)) void __not_in_flash_func(end_write)(void);
static __attribute__((noinline)) void __not_in_flash_func(retire)(uint32_t reason);
__attribute__((noinline)) void __not_in_flash_func(sync_io_run_output_cancel)(void);
__attribute__((noinline)) void __not_in_flash_func(sync_io_run_output_service_core1)(void);
__attribute__((noinline)) bool __not_in_flash_func(sync_io_run_output_can_submit_core1)(uint32_t generation);
__attribute__((noinline)) bool __not_in_flash_func(sync_io_run_output_submit_count_core1)(uint32_t generation,
    const sync_io_run_output_edge_t *edges, uint32_t count);
__attribute__((noinline)) bool __not_in_flash_func(sync_io_run_output_submit_core1)(uint32_t generation,
    const sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES]);
__attribute__((noinline)) bool __not_in_flash_func(sync_io_run_output_snapshot)(sync_io_run_output_snapshot_t *out);

static void begin_write(void) { (void)__atomic_add_fetch(&s_guard,1u,__ATOMIC_ACQ_REL); }
static void end_write(void)
{
    const uint32_t state=s_run.state;
    for (uint32_t i=0;i<sizeof(s_run)/sizeof(uint32_t);++i) {
        uint32_t word;
        memcpy(&word,(const uint8_t *)&s_run+i*4u,4u);
        __atomic_store_n(s_snapshot+i,word,__ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&s_guard,1u,__ATOMIC_RELEASE);
    /* This is the ownership handoff. No old owner accesses runtime,
     * registers, snapshot guard or storage after publishing it. */
    __atomic_store_n(&s_state,state,__ATOMIC_RELEASE);
}
static void publish_state(uint32_t state)
{
    s_run.state=state;
}

static bool dma_failed(void)
{
    return (dma_hw->ch[RUN_DMA].ctrl_trig &
        (DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS | DMA_CH0_CTRL_TRIG_READ_ERROR_BITS |
         DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS))!=0u;
}

/* TIMER1 is owned/initialized by the timestamp-clock domain. This capability
 * only observes it; the caller excludes timer reset and change-and-restore.
 * A single high/low/high observation has no wrap retry. */
static __attribute__((noinline)) bool __not_in_flash_func(read_raw)(uint64_t *out)
{
    if (timer1_hw->pause || timer1_hw->source != TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS)
        return false;
    const uint32_t hi=timer1_hw->timerawh;
    const uint32_t lo=timer1_hw->timerawl;
    if (hi!=timer1_hw->timerawh) return false;
    *out=((uint64_t)hi<<32u)|lo;
    return true;
}

static bool load(void *context,const sync_io_persona_descriptor_t *d,uint32_t mask)
{
    (void)context;
    const struct pio_program *program=s_mode==RUN_MODE_UNIFORM ?
        &sync_pulse_uniform_out1_program : &sync_pulse_stream_out1_program;
    if (!d || d->id!=SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER ||
        mask!=(1u<<RUN_DMA) || pio_sm_is_claimed(RUN_PIO,RUN_SM) ||
        dma_channel_is_claimed(RUN_DMA) ||
        !pio_can_add_program(RUN_PIO,program)) return false;
    pio_sm_claim(RUN_PIO,RUN_SM); s_sm_claimed=true;
    dma_channel_claim(RUN_DMA); s_dma_claimed=true;
    s_offset=pio_add_program(RUN_PIO,program); s_loaded=true;
    return true;
}

static bool arm(void *context,const sync_io_persona_descriptor_t *d,uint32_t mask)
{
    (void)context; (void)d; (void)mask;
    if (!s_loaded || !s_dma_claimed || !s_sm_claimed) return false;
    pio_sm_set_enabled(RUN_PIO,RUN_SM,false);
    pio_sm_clear_fifos(RUN_PIO,RUN_SM);
    pio_sm_restart(RUN_PIO,RUN_SM);
    if (s_mode==RUN_MODE_UNIFORM) {
        if (!sync_pulse_uniform_out1_program_init(RUN_PIO,RUN_SM,s_offset,RUN_PIN,
                s_fixed_high_ticks)) return false;
    } else if (!sync_pulse_stream_out1_program_init(RUN_PIO,RUN_SM,s_offset,RUN_PIN)) return false;
    dma_channel_set_irq0_enabled(RUN_DMA,false);
    dma_channel_set_irq1_enabled(RUN_DMA,false);
    dma_channel_config c_dma=dma_channel_get_default_config(RUN_DMA);
    channel_config_set_transfer_data_size(&c_dma,DMA_SIZE_32);
    channel_config_set_read_increment(&c_dma,true);
    channel_config_set_write_increment(&c_dma,false);
    channel_config_set_dreq(&c_dma,pio_get_dreq(RUN_PIO,RUN_SM,true));
    dma_channel_configure(RUN_DMA,&c_dma,&RUN_PIO->txf[RUN_SM],
        sync_io_shared_workspace,0u,false);
    return true;
}

/* Only after retirement (or before any enable). Never abort a live transfer
 * synchronously from Core0. The manager calls this on partial-load failure. */
static void cleanup(void *context,const sync_io_persona_descriptor_t *d,uint32_t mask)
{
    (void)context; (void)d; (void)mask;
    if (s_sm_claimed) {
        pio_sm_set_enabled(RUN_PIO,RUN_SM,false);
        pio_sm_set_pins_with_mask(RUN_PIO,RUN_SM,0u,1u<<RUN_PIN);
        pio_sm_clear_fifos(RUN_PIO,RUN_SM);
        pio_sm_restart(RUN_PIO,RUN_SM);
    }
    if (s_loaded) {
        pio_remove_program(RUN_PIO,s_mode==RUN_MODE_UNIFORM ?
            &sync_pulse_uniform_out1_program : &sync_pulse_stream_out1_program,s_offset);
        s_loaded=false;
    }
    if (s_dma_claimed) { dma_channel_unclaim(RUN_DMA); s_dma_claimed=false; }
    if (s_sm_claimed) {
        gpio_put(RUN_PIN,false); gpio_set_dir(RUN_PIN,GPIO_OUT);
        gpio_set_function(RUN_PIN,GPIO_FUNC_SIO);
        pio_sm_unclaim(RUN_PIO,RUN_SM); s_sm_claimed=false;
    }
}

static bool prepare_mode(uint32_t hz,uint32_t duration_ms,uint32_t high_ticks,
                         uint32_t mode,uint32_t *generation)
{
    if (get_core_num()!=0u || !generation || hz!=BOARD_SYS_CLOCK_HZ ||
        (mode==RUN_MODE_UNIFORM && high_ticks<SYNC_PULSE_UNIFORM_HIGH_OVERHEAD) ||
        clock_get_hz(clk_sys)!=hz || duration_ms==0u || duration_ms>20000u ||
        s_generation==UINT32_MAX ||
        __atomic_load_n(&s_state,__ATOMIC_ACQUIRE)!=SYNC_IO_RUN_OUTPUT_IDLE ||
        !sync_io_core_run_output_reserve(&s_run)) return false;
    if (!sync_io_workspace_claim(&s_run)) {
        (void)sync_io_core_run_output_release(&s_run); return false;
    }
    s_mode=mode; s_fixed_high_ticks=high_ticks;
    const sync_io_persona_manager_hooks_t hooks={.load=load,.arm=arm,.cleanup=cleanup};
    sync_io_persona_manager_init(&s_manager,&hooks,NULL);
    if (!sync_io_persona_manager_claim(&s_manager,SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER,&s_handle,NULL) ||
        !sync_io_persona_manager_load(&s_manager,&s_handle) ||
        !sync_io_persona_manager_arm(&s_manager,&s_handle)) {
        if (sync_io_persona_manager_handle_valid(&s_manager,&s_handle))
            (void)sync_io_persona_manager_release(&s_manager,&s_handle);
        (void)sync_io_workspace_release(&s_run);
        (void)sync_io_core_run_output_release(&s_run);
        return false;
    }
    begin_write();
    memset(&s_run,0,sizeof(s_run));
    s_run.schema=SYNC_IO_RUN_OUTPUT_SCHEMA;
    s_run.fifo_words_per_edge=mode==RUN_MODE_UNIFORM ? 1u : 2u;
    s_run.fixed_high_ticks=high_ticks;
    s_run.generation=++s_generation; s_run.tick_hz=hz;
    s_run.program_offset=s_offset;
    s_duration_ms=duration_ms; s_lease=true; s_source_pending=false;
    __atomic_store_n(&s_cancel,0u,__ATOMIC_RELEASE);
    publish_state(SYNC_IO_RUN_OUTPUT_PREPARED);
    end_write();
    *generation=s_generation;
    return true;
}

bool sync_io_run_output_prepare(uint32_t hz,uint32_t duration_ms,uint32_t *generation)
{
    return prepare_mode(hz,duration_ms,0u,RUN_MODE_PAIRED,generation);
}

bool sync_io_run_output_prepare_uniform(uint32_t hz,uint32_t duration_ms,
                                      uint32_t high_ticks,uint32_t *generation)
{
    return prepare_mode(hz,duration_ms,high_ticks,RUN_MODE_UNIFORM,generation);
}

void sync_io_run_output_cancel(void) { __atomic_store_n(&s_cancel,1u,__ATOMIC_RELEASE); }

static void retire(uint32_t reason)
{
    if (s_run.state==SYNC_IO_RUN_OUTPUT_RETIRING || s_run.state==SYNC_IO_RUN_OUTPUT_RETIRED) return;
    s_run.reason=reason;
    uint64_t observed;
    s_run.retire_raw_valid=read_raw(&observed) ? 1u : 0u;
    s_run.retire_raw_tick=s_run.retire_raw_valid ? observed : 0u;
    s_run.retire_pc=RUN_PIO->sm[RUN_SM].addr;
    s_run.retire_fstat=RUN_PIO->fstat;
    s_run.retire_fdebug=RUN_PIO->fdebug;
    s_run.retire_dma_ctrl=dma_hw->ch[RUN_DMA].ctrl_trig;
    s_run.retire_dma_remaining=dma_hw->ch[RUN_DMA].transfer_count;
    s_run.retire_pio_ctrl=RUN_PIO->ctrl;
    pio_sm_set_enabled(RUN_PIO,RUN_SM,false);
    pio_sm_set_pins_with_mask(RUN_PIO,RUN_SM,0u,1u<<RUN_PIN);
    hw_clear_bits(&dma_hw->ch[RUN_DMA].al1_ctrl,DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_hw->abort=1u<<RUN_DMA;
    publish_state(SYNC_IO_RUN_OUTPUT_RETIRING);
}

void sync_io_run_output_service_core1(void)
{
    const uint32_t state=__atomic_load_n(&s_state,__ATOMIC_ACQUIRE);
    if (get_core_num()!=1u || state==SYNC_IO_RUN_OUTPUT_IDLE || state==SYNC_IO_RUN_OUTPUT_RETIRED) return;
    begin_write();
    if (__atomic_load_n(&s_cancel,__ATOMIC_ACQUIRE)) retire(SYNC_IO_RUN_OUTPUT_CANCELLED);
    if (s_run.state==SYNC_IO_RUN_OUTPUT_RUNNING) {
        uint64_t now;
        if (clock_get_hz(clk_sys)!=s_run.tick_hz || !read_raw(&now) ||
            RUN_PIO->sm[RUN_SM].clkdiv!=(1u<<16u)) retire(SYNC_IO_RUN_OUTPUT_CLOCK);
        else {
            s_run.service_last_gap_ticks=s_run.service_observations && now>=s_run.service_last_tick ?
                now-s_run.service_last_tick : 0u;
            if (s_run.service_last_gap_ticks>s_run.service_max_gap_ticks)
                s_run.service_max_gap_ticks=s_run.service_last_gap_ticks;
            s_run.service_last_tick=now;
            if (s_run.service_observations!=UINT32_MAX) ++s_run.service_observations;
            if (dma_failed()) retire(SYNC_IO_RUN_OUTPUT_DMA);
            else if (now>=s_run.expires_tick) retire(SYNC_IO_RUN_OUTPUT_EXPIRED);
            else if (RUN_PIO->fdebug&(1u<<(PIO_FDEBUG_TXSTALL_LSB+RUN_SM))) retire(SYNC_IO_RUN_OUTPUT_STARVED);
        }
    }
    if (s_run.state==SYNC_IO_RUN_OUTPUT_RETIRING &&
        !(dma_hw->abort&(1u<<RUN_DMA)) && !dma_channel_is_busy(RUN_DMA)) {
        pio_sm_clear_fifos(RUN_PIO,RUN_SM);
        s_source_pending=false;
        publish_state(SYNC_IO_RUN_OUTPUT_RETIRED);
    }
    s_run.transfer_count=dma_hw->ch[RUN_DMA].transfer_count;
    s_run.dma_busy=dma_channel_is_busy(RUN_DMA);
    s_run.pio_enabled=(RUN_PIO->ctrl&(1u<<RUN_SM))!=0u;
    end_write();
}

bool sync_io_run_output_can_submit_core1(uint32_t generation)
{
    const uint32_t state=__atomic_load_n(&s_state,__ATOMIC_ACQUIRE);
    if (get_core_num()!=1u ||
        (state!=SYNC_IO_RUN_OUTPUT_PREPARED && state!=SYNC_IO_RUN_OUTPUT_RUNNING) ||
        !sync_io_core_run_output_held(&s_run) || generation!=s_run.generation ||
        __atomic_load_n(&s_cancel,__ATOMIC_ACQUIRE)) return false;
    if (state==SYNC_IO_RUN_OUTPUT_PREPARED) return true;
    return state==SYNC_IO_RUN_OUTPUT_RUNNING && !dma_failed() && !dma_channel_is_busy(RUN_DMA) &&
        dma_hw->ch[RUN_DMA].transfer_count==0u &&
        !(RUN_PIO->fdebug&(1u<<(PIO_FDEBUG_TXSTALL_LSB+RUN_SM)));
}

bool sync_io_run_output_submit_core1(uint32_t generation,
    const sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES])
{
    return sync_io_run_output_submit_count_core1(generation,edges,SYNC_IO_RUN_OUTPUT_BLOCK_EDGES);
}

bool sync_io_run_output_submit_count_core1(uint32_t generation,
    const sync_io_run_output_edge_t *edges,uint32_t count)
{
    if (!edges || !count || count>SYNC_IO_RUN_OUTPUT_MAX_EDGES ||
        !sync_io_run_output_can_submit_core1(generation)) return false;
    const bool uniform=s_mode==RUN_MODE_UNIFORM;
    const uint32_t words_per_edge=uniform ? 1u : 2u;
    const uint32_t word_count=words_per_edge*count;
    const uint32_t first_low_overhead=uniform ? SYNC_PULSE_UNIFORM_FIRST_LOW_OVERHEAD :
                                               SYNC_PULSE_STREAM_FIRST_LOW_OVERHEAD;
    const bool first=s_run.state==SYNC_IO_RUN_OUTPUT_PREPARED;
    uint64_t submitted_at=0u;
    uint64_t now;
    if (!read_raw(&now)) return false;
    const uint64_t guard=s_run.tick_hz/1000000u*SYNC_IO_RUN_OUTPUT_MIN_GUARD_US;
    if (now>UINT64_MAX-guard || (!first && s_run.last_falling_tick<=now+guard)) return false;
    uint32_t words[SYNC_IO_RUN_OUTPUT_MAX_WORDS];
    uint64_t previous=first ? now : s_run.last_falling_tick;
    for (uint32_t i=0;i<count;++i) {
        if (!edges[i].model_token || edges[i].falling_tick<=edges[i].rising_tick ||
            (i && edges[i].ordinal<=edges[i-1u].ordinal) ||
            (!i && !first && edges[i].ordinal<=s_run.last_ordinal)) return false;
        if (uniform) {
            if (!sync_pulse_uniform_encode(first ? now : s_run.anchor_before,
                    first && i==0u,previous,edges[i].rising_tick,
                    edges[i].falling_tick,s_fixed_high_ticks,&words[i])) return false;
        } else {
            /* Compatibility encoder outputs [high countdown, low countdown]. */
            sync_pulse_stream_words_t pair;
            if (!sync_pulse_stream_encode(first ? now : s_run.anchor_before,
                    first && i==0u,previous,edges[i].rising_tick,
                    edges[i].falling_tick,&pair)) return false;
            words[2u*i]=pair.high_count; words[2u*i+1u]=pair.low_count;
        }
        previous=edges[i].falling_tick;
    }
    if (first && edges[0].rising_tick<=now+guard) return false;
    if (clock_get_hz(clk_sys)!=s_run.tick_hz ||
        __atomic_load_n(&s_cancel,__ATOMIC_ACQUIRE) || dma_failed()) return false;
    begin_write();
    if (s_source_pending) { ++s_run.source_retirements; s_source_pending=false; }
    memcpy(sync_io_shared_workspace,words,word_count*sizeof(words[0]));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (first) {
        if (!sync_io_persona_manager_start(&s_manager,&s_handle)) {
            retire(SYNC_IO_RUN_OUTPUT_ARGUMENT); end_write(); return false;
        }
        /* Re-encode only the first low against the final raw observation.
         * No callback, model inverse or resource operation occurs inside
         * the retained enable bracket. Its width is evidence, not precision. */
        uint64_t before,observed,after;
        if (__atomic_load_n(&s_cancel,__ATOMIC_ACQUIRE) ||
            !sync_io_core_run_output_held(&s_run) || clock_get_hz(clk_sys)!=s_run.tick_hz ||
            !read_raw(&before) || before>UINT64_MAX-guard || edges[0].rising_tick<=before+guard ||
            edges[0].rising_tick-before-first_low_overhead>UINT32_MAX ||
            before>UINT64_MAX-(uint64_t)s_duration_ms*s_run.tick_hz/1000u) {
            retire(SYNC_IO_RUN_OUTPUT_DEADLINE); end_write(); return false;
        }
        RUN_PIO->fdebug=1u<<(PIO_FDEBUG_TXSTALL_LSB+RUN_SM);
        if (!uniform) pio_sm_put(RUN_PIO,RUN_SM,words[0]);
        pio_sm_put(RUN_PIO,RUN_SM,(uint32_t)(edges[0].rising_tick-before-first_low_overhead));
        /* A one-pulse first block is entirely in the CPU-preloaded FIFO.
         * Do not trigger a zero-length DMA transfer. */
        if (word_count>words_per_edge) {
            dma_channel_set_read_addr(RUN_DMA,sync_io_shared_workspace+words_per_edge,false);
            dma_channel_set_trans_count(RUN_DMA,word_count-words_per_edge,true);
        }
        pio_sm_set_enabled(RUN_PIO,RUN_SM,true);
        /* The enable write and an immediate PC read need not observe the
         * same SM cycle. Take one raw observation before sampling PC, then
         * another after it. No waiting loop or PC acceptance relaxation. */
        const bool observed_valid=read_raw(&observed);
        const uint32_t pc=pio_sm_get_pc(RUN_PIO,RUN_SM);
        const bool after_valid=read_raw(&after);
        s_run.start_pc=pc;
        s_run.start_raw_flags=(observed_valid ? 1u : 0u)|(after_valid ? 2u : 0u);
        s_run.start_raw_observed=observed_valid ? observed : 0u;
        s_run.start_raw_after=after_valid ? after : 0u;
        const bool raw_valid=observed_valid && after_valid && after!=UINT64_MAX &&
            observed>=before && after>=observed;
        const bool valid=raw_valid && pc>s_offset && pc<=s_offset+(uniform ? 3u : 4u);
        s_run.anchor_before=before;
        submitted_at=before;
        s_run.anchor_after=raw_valid ? after+1u : UINT64_MAX;
        s_run.expires_tick=before+(uint64_t)s_duration_ms*s_run.tick_hz/1000u;
        s_run.first_ordinal=edges[0].ordinal; s_run.first_model=edges[0].model_token;
        publish_state(SYNC_IO_RUN_OUTPUT_RUNNING);
        if (!valid || after<before || after+1u-before>guard) retire(SYNC_IO_RUN_OUTPUT_CLOCK);
    } else {
        uint64_t ready;
        if (__atomic_load_n(&s_cancel,__ATOMIC_ACQUIRE) ||
            !sync_io_core_run_output_held(&s_run) || dma_failed() ||
            clock_get_hz(clk_sys)!=s_run.tick_hz ||
            !read_raw(&ready) || ready>UINT64_MAX-guard ||
            s_run.last_falling_tick<=ready+guard ||
            (RUN_PIO->fdebug&(1u<<(PIO_FDEBUG_TXSTALL_LSB+RUN_SM)))) {
            retire(SYNC_IO_RUN_OUTPUT_DEADLINE); end_write(); return false;
        }
        dma_channel_set_read_addr(RUN_DMA,sync_io_shared_workspace,false);
        dma_channel_set_trans_count(RUN_DMA,word_count,true);
        submitted_at=ready;
        const uint64_t margin=s_run.last_falling_tick-ready;
        if (!s_run.refill_min_margin_ticks || margin<s_run.refill_min_margin_ticks)
            s_run.refill_min_margin_ticks=margin;
    }
    if (!first && submitted_at>=s_run.submit_last_tick &&
        submitted_at-s_run.submit_last_tick>s_run.submit_max_gap_ticks)
        s_run.submit_max_gap_ticks=submitted_at-s_run.submit_last_tick;
    s_run.submit_last_tick=submitted_at;
    s_run.submit_service_observation=s_run.service_observations;
    s_source_pending=true;
    for (uint32_t i=0;i<count;++i) {
        if (s_run.last_model && s_run.last_model!=edges[i].model_token) ++s_run.model_changes;
        s_run.last_model=edges[i].model_token;
    }
    ++s_run.blocks; s_run.edges+=count;
    s_run.last_rising_tick=edges[count-1u].rising_tick;
    s_run.last_falling_tick=edges[count-1u].falling_tick;
    s_run.last_ordinal=edges[count-1u].ordinal;
    end_write();
    return true;
}

bool sync_io_run_output_release(uint32_t generation)
{
    if (get_core_num()!=0u ||
        __atomic_load_n(&s_state,__ATOMIC_ACQUIRE)!=SYNC_IO_RUN_OUTPUT_RETIRED ||
        generation!=s_run.generation) return false;
    if (s_lease && !sync_io_persona_manager_release(&s_manager,&s_handle)) return false;
    s_lease=false;
    (void)sync_io_workspace_release(&s_run);
    if (!sync_io_core_run_output_release(&s_run)) return false;
    begin_write(); publish_state(SYNC_IO_RUN_OUTPUT_IDLE); end_write();
    return true;
}

bool sync_io_run_output_snapshot(sync_io_run_output_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard=__atomic_load_n(&s_guard,__ATOMIC_ACQUIRE);
    if (guard&1u) return false;
    sync_io_run_output_snapshot_t value;
    for (uint32_t i=0;i<sizeof(value)/sizeof(uint32_t);++i) {
        const uint32_t word=__atomic_load_n(s_snapshot+i,__ATOMIC_RELAXED);
        memcpy((uint8_t *)&value+i*4u,&word,4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard!=__atomic_load_n(&s_guard,__ATOMIC_ACQUIRE)) return false;
    *out=value; return true;
}
