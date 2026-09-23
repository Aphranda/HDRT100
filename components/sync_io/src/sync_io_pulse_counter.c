#include "sync_io_pulse_counter.h"

#include <string.h>
#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "sync_io_core_internal.h"
#include "sync_io_pio0_runtime.h"
#include "sync_pulse_counter.pio.h"

#define COUNTER_PIO BOARD_SYNC_PIO_FAST
#define COUNTER_SM SYNC_IO_PULSE_COUNTER_SM
#define COUNTER_SNAPSHOT_POLLS 64u
/* Four resident FIFO words plus one ISR value captured before the first
 * TIMER1 sample may be stale. The sixth word MUST be a later PIO MOV. */
#define COUNTER_FRESH_WORDS 6u

_Static_assert(sizeof(sync_pulse_counter_program_instructions)/sizeof(uint16_t)==
    SYNC_IO_PULSE_COUNTER_INSTRUCTION_WORDS,"counter instruction accounting");

static sync_io_pulse_counter_snapshot_t s_counter;
static sync_io_persona_manager_handle_t s_handle;
static uint32_t s_busy,s_generation,s_offset;
static bool s_sm_claimed,s_loaded,s_configured,s_lease;

static bool enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_busy,&idle,1u,
        false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void leave(void) {__atomic_store_n(&s_busy,0u,__ATOMIC_RELEASE);}
static bool now_raw(uint64_t *out)
{
    if(clock_get_hz(clk_sys)!=BOARD_SYS_CLOCK_HZ || timer1_hw->pause ||
        timer1_hw->source!=TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS) return false;
    const uint32_t hi=timer1_hw->timerawh,lo=timer1_hw->timerawl;
    if(hi!=timer1_hw->timerawh) return false;
    *out=((uint64_t)hi<<32u)|lo;return true;
}
static void record(uint32_t value,bool before_valid,uint64_t before)
{
    uint64_t after=0u;
    const bool after_valid=now_raw(&after);
    s_counter.count32=value;s_counter.valid=1u;
    s_counter.timestamp_valid=before_valid && after_valid && after>=before;
    s_counter.sample_before_tick=s_counter.timestamp_valid ? before : 0u;
    s_counter.sample_after_tick=s_counter.timestamp_valid ? after : 0u;
}
static bool load(void *context,const sync_io_persona_descriptor_t *descriptor,uint32_t dma)
{
    (void)context;
    if(!descriptor || descriptor->id!=SYNC_IO_PERSONA_ID_PULSE_COUNTER || dma ||
        descriptor->gpio_read_mask!=(1u<<s_counter.input_pin) ||
        pio_sm_is_claimed(COUNTER_PIO,COUNTER_SM) ||
        !pio_can_add_program(COUNTER_PIO,&sync_pulse_counter_program)) return false;
    pio_sm_claim(COUNTER_PIO,COUNTER_SM);s_sm_claimed=true;
    s_offset=pio_add_program(COUNTER_PIO,&sync_pulse_counter_program);s_loaded=true;
    return true;
}
static bool arm(void *context,const sync_io_persona_descriptor_t *descriptor,uint32_t dma)
{
    (void)context;(void)descriptor;(void)dma;
    if(!s_loaded || !s_sm_claimed) return false;
    pio_sm_set_enabled(COUNTER_PIO,COUNTER_SM,false);
    if(!s_configured) {
        pio_sm_config config=sync_pulse_counter_program_get_default_config(s_offset);
        sm_config_set_jmp_pin(&config,s_counter.input_pin);
        sm_config_set_clkdiv_int_frac(&config,1u,0u);
        sm_config_set_in_shift(&config,false,false,32u);
        /* Four-word RX FIFO, no autopush and no DMA: only explicit PUSH. */
        if(pio_sm_init(COUNTER_PIO,COUNTER_SM,s_offset,&config)!=0) return false;
        pio_sm_exec(COUNTER_PIO,COUNTER_SM,pio_encode_mov_not(pio_x,pio_null));
        s_configured=true;
    }
    pio_sm_clear_fifos(COUNTER_PIO,COUNTER_SM);
    pio_sm_restart(COUNTER_PIO,COUNTER_SM);
    pio_sm_clkdiv_restart(COUNTER_PIO,COUNTER_SM);
    pio_sm_exec(COUNTER_PIO,COUNTER_SM,pio_encode_jmp(s_offset));
    /* Observing a pad needs no mux/direction/pull change. This persona never
     * drives any pin; preserving the prior input owner is its restore policy. */
    return true;
}
static void cleanup(void *context,const sync_io_persona_descriptor_t *descriptor,uint32_t dma)
{
    (void)context;(void)descriptor;(void)dma;
    if(s_sm_claimed) {
        pio_sm_set_enabled(COUNTER_PIO,COUNTER_SM,false);
        pio_sm_clear_fifos(COUNTER_PIO,COUNTER_SM);
        pio_sm_restart(COUNTER_PIO,COUNTER_SM);
    }
    if(s_loaded) {pio_remove_program(COUNTER_PIO,&sync_pulse_counter_program,s_offset);s_loaded=false;}
    if(s_sm_claimed) {pio_sm_unclaim(COUNTER_PIO,COUNTER_SM);s_sm_claimed=false;}
    s_configured=false;
}
static bool matching(uint32_t generation)
{return s_lease && generation && generation==s_counter.generation;}

bool sync_io_pulse_counter_load(uint32_t input_port,uint32_t *generation)
{
    if(!generation || input_port<1u || input_port>BOARD_SYNC_INPUT_PIN_COUNT || !enter()) return false;
    if(s_lease || s_generation==UINT32_MAX ||
        !sync_io_core_pulse_counter_reserve(&s_counter)) {leave();return false;}
    const sync_io_pulse_counter_snapshot_t previous=s_counter;
    memset(&s_counter,0,sizeof(s_counter));s_counter.schema=SYNC_IO_PULSE_COUNTER_SCHEMA;
    s_counter.input_port=input_port;
    s_counter.input_pin=BOARD_SYNC_INPUT_BASE_PIN+
        (BOARD_SYNC_INPUT_BITS_REVERSED ? BOARD_SYNC_INPUT_PIN_COUNT-input_port : input_port-1u);
    s_counter.tick_hz=BOARD_SYS_CLOCK_HZ;
    const sync_io_persona_manager_hooks_t hooks={.load=load,.arm=arm,.cleanup=cleanup};
    if(!sync_io_pio0_runtime_prepare_io(SYNC_IO_PERSONA_ID_PULSE_COUNTER,
            1u<<s_counter.input_pin,0u,&hooks,NULL,&s_handle)) {
        (void)sync_io_core_pulse_counter_release(&s_counter);s_counter=previous;leave();return false;
    }
    s_lease=true;s_counter.generation=++s_generation;s_counter.state=SYNC_IO_PULSE_COUNTER_LOADED;
    uint64_t before=0u;const bool valid=now_raw(&before);record(0u,valid,before);
    *generation=s_counter.generation;leave();return true;
}
bool sync_io_pulse_counter_start(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation) || (s_counter.state!=SYNC_IO_PULSE_COUNTER_LOADED &&
        s_counter.state!=SYNC_IO_PULSE_COUNTER_STOPPED) ||
        clock_get_hz(clk_sys)!=s_counter.tick_hz ||
        !sync_io_pio0_runtime_counter_start_core0(&s_handle)) {leave();return false;}
    pio_sm_set_enabled(COUNTER_PIO,COUNTER_SM,true);
    s_counter.state=SYNC_IO_PULSE_COUNTER_RUNNING;leave();return true;
}
bool sync_io_pulse_counter_stop(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation)) {leave();return false;}
    if(s_counter.state!=SYNC_IO_PULSE_COUNTER_RUNNING) {leave();return true;}
    if(!sync_io_pio0_runtime_counter_stop_core0(&s_handle)) {leave();return false;}
    uint64_t before=0u;const bool valid=now_raw(&before);
    /* No WAIT, blocking PUSH, IRQ or DMA exists in this program. Disabling
     * freezes X at an instruction boundary; forced reads are now harmless. */
    pio_sm_set_enabled(COUNTER_PIO,COUNTER_SM,false);
    pio_sm_clear_fifos(COUNTER_PIO,COUNTER_SM);
    pio_sm_exec(COUNTER_PIO,COUNTER_SM,pio_encode_mov_not(pio_isr,pio_x));
    pio_sm_exec(COUNTER_PIO,COUNTER_SM,pio_encode_push(false,false));
    s_counter.state=SYNC_IO_PULSE_COUNTER_STOPPED;
    if(pio_sm_is_rx_fifo_empty(COUNTER_PIO,COUNTER_SM)) {
        s_counter.valid=0u;s_counter.timestamp_valid=0u;leave();return false;
    }
    record(pio_sm_get(COUNTER_PIO,COUNTER_SM),valid,before);
    leave();return true;
}
bool sync_io_pulse_counter_clear(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation) || s_counter.state==SYNC_IO_PULSE_COUNTER_RUNNING ||
        s_counter.clear_generation==UINT32_MAX) {leave();return false;}
    uint64_t before=0u;const bool valid=now_raw(&before);
    pio_sm_clear_fifos(COUNTER_PIO,COUNTER_SM);
    pio_sm_restart(COUNTER_PIO,COUNTER_SM);
    pio_sm_exec(COUNTER_PIO,COUNTER_SM,pio_encode_mov_not(pio_x,pio_null));
    pio_sm_exec(COUNTER_PIO,COUNTER_SM,pio_encode_jmp(s_offset));
    ++s_counter.clear_generation;record(0u,valid,before);leave();return true;
}
bool sync_io_pulse_counter_release(uint32_t generation)
{
    if(!enter()) return false;
    if(!matching(generation) || s_counter.state==SYNC_IO_PULSE_COUNTER_RUNNING ||
        !sync_io_pio0_runtime_release(&s_handle)) {leave();return false;}
    s_lease=false;
    const bool released=sync_io_core_pulse_counter_release(&s_counter);
    s_counter.state=SYNC_IO_PULSE_COUNTER_IDLE;leave();return released;
}
bool sync_io_pulse_counter_snapshot(sync_io_pulse_counter_snapshot_t *out)
{
    if(!out || !enter()) return false;
    if(s_counter.state==SYNC_IO_PULSE_COUNTER_RUNNING) {
        uint64_t before=0u;const bool valid=now_raw(&before);
        uint32_t read=0u,value=0u;
        for(uint32_t i=0u;i<COUNTER_SNAPSHOT_POLLS && read<COUNTER_FRESH_WORDS;++i) {
            if(!pio_sm_is_rx_fifo_empty(COUNTER_PIO,COUNTER_SM)) {
                value=pio_sm_get(COUNTER_PIO,COUNTER_SM);++read;
            }
        }
        if(read!=COUNTER_FRESH_WORDS) {leave();return false;}
        record(value,valid,before);
    }
    *out=s_counter;out->schema=SYNC_IO_PULSE_COUNTER_SCHEMA;
    if(s_lease) out->input_pin=(COUNTER_PIO->sm[COUNTER_SM].execctrl&PIO_SM0_EXECCTRL_JMP_PIN_BITS)>>
        PIO_SM0_EXECCTRL_JMP_PIN_LSB;
    leave();return true;
}
