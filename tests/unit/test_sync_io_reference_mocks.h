#ifndef REFERENCE_MOCKS_H
#define REFERENCE_MOCKS_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sync_io_persona_manager.h"
typedef unsigned uint;
#define PICO_PIO_VERSION 0
#define BOARD_SYS_CLOCK_HZ 250000000u
#define BOARD_SYNC_INPUT_BASE_PIN 20u
#define BOARD_SYNC_INPUT_BITS_REVERSED 1u
#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 1u
#define clk_sys 0u
#define DMA_SIZE_32 2u
#define DREQ_FORCE 63u
#define DMA_IRQ_0 0u
#define DMA_CH0_CTRL_TRIG_EN_BITS 1u
#define DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS (1u<<31)
#define DMA_CH0_CTRL_TRIG_READ_ERROR_BITS (1u<<30)
#define DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS (1u<<29)
typedef struct {uint32_t rxf[4];} mock_pio_t;
typedef mock_pio_t *PIO;
struct pio_program {const uint16_t *instructions;uint length;int origin;uint pio_version;};
typedef struct {uint32_t pause,source,timerawh,timerawl;} mock_timer_t;
typedef struct {uintptr_t write_addr;uint32_t ctrl_trig;} mock_dma_ch_t;
typedef struct {mock_dma_ch_t ch[16];uint32_t abort;} mock_dma_t;
typedef struct {uint lo,hi,pin,div,frac;} pio_sm_config;
typedef struct {uint ctrl,size,dreq,chain;bool read_inc,write_inc;} dma_channel_config;
static mock_pio_t hw_pio;
static mock_timer_t hw_timer;
static mock_dma_t hw_dma;
#define BOARD_SYNC_PIO_FAST (&hw_pio)
#define timer1_hw (&hw_timer)
#define dma_hw (&hw_dma)
static uint mock_core,mock_hz=BOARD_SYS_CLOCK_HZ;
static bool claimed[16],busy[16],sm_claimed,enabled,gate,deny_add;
static dma_channel_config cfg[16];
static uint reload[16],remaining[16],fifo_count;
static const volatile uint32_t *reads[16];
static uint32_t fifo[4];
static inline uint get_core_num(void){return mock_core;}
static inline uint clock_get_hz(uint c){(void)c;return mock_hz;}
static inline void raw(uint64_t t){hw_timer.timerawl=(uint32_t)t;hw_timer.timerawh=t>>32;}
static inline void hw_clear_bits(uint32_t *p,uint32_t mask){*p&=~mask;}
static inline bool pio_sm_is_claimed(PIO p,uint sm){(void)p;(void)sm;return sm_claimed;}
static inline void pio_sm_claim(PIO p,uint sm){(void)p;assert(sm==2&&!sm_claimed);sm_claimed=true;}
static inline void pio_sm_unclaim(PIO p,uint sm){(void)p;(void)sm;assert(sm_claimed);sm_claimed=false;}
static inline uint pio_encode_wait_pin(bool pol,uint pin){return 0x2020u|((uint)pol<<7)|pin;}
static inline uint pio_encode_jmp(uint addr){return addr;}
static inline bool pio_can_add_program(PIO p,const struct pio_program *g){(void)p;assert(g->length==11);return !deny_add;}
static inline uint pio_add_program(PIO p,const struct pio_program *g){(void)p;(void)g;return 7;}
static inline void pio_remove_program(PIO p,const struct pio_program *g,uint o){(void)p;(void)g;assert(o==7);}
static inline pio_sm_config pio_get_default_sm_config(void){return (pio_sm_config){0};}
static inline void sm_config_set_wrap(pio_sm_config *c,uint l,uint h){c->lo=l;c->hi=h;}
static inline void sm_config_set_in_pins(pio_sm_config *c,uint p){c->pin=p;}
static inline void sm_config_set_out_shift(pio_sm_config *c,bool r,bool a,uint n){(void)c;assert(r&&!a&&n==32);}
static inline void sm_config_set_clkdiv_int_frac(pio_sm_config *c,uint d,uint f){c->div=d;c->frac=f;}
static inline int pio_sm_init(PIO p,uint sm,uint o,const pio_sm_config *c){(void)p;assert(sm==2&&o==7&&c->pin==20&&c->div==1&&!c->frac);return 0;}
static inline void pio_sm_set_enabled(PIO p,uint sm,bool e){(void)p;assert(sm==2);enabled=e;}
static inline void pio_sm_clear_fifos(PIO p,uint sm){(void)p;(void)sm;fifo_count=0;}
static inline void pio_sm_restart(PIO p,uint sm){(void)p;(void)sm;}
static inline void pio_sm_exec(PIO p,uint sm,uint ins){(void)p;(void)sm;assert(ins==7);}
static inline void pio_sm_put(PIO p,uint sm,uint v){(void)p;(void)sm;assert(v==9999999);}
static inline uint pio_get_dreq(PIO p,uint sm,bool tx){(void)p;assert(sm==2&&!tx);return 2;}
static inline bool dma_channel_is_claimed(uint ch){return claimed[ch];}
static inline void dma_channel_claim(uint ch){assert(!claimed[ch]);claimed[ch]=true;}
static inline void dma_channel_unclaim(uint ch){assert(claimed[ch]);claimed[ch]=false;}
static inline bool dma_channel_is_busy(uint ch){return busy[ch];}
static inline void dma_channel_set_irq0_enabled(uint ch,bool e){assert((ch==9||ch==10)&&!e);}
static inline void dma_channel_set_irq1_enabled(uint ch,bool e){assert((ch==9||ch==10)&&!e);}
static inline void dma_channel_set_config(uint ch,const dma_channel_config *c,bool trigger){
    assert(claimed[ch]&&!busy[ch]&&!(hw_dma.abort&(1u<<ch))&&!trigger);
    assert(c->ctrl==(DMA_CH0_CTRL_TRIG_READ_ERROR_BITS|DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS));
    hw_dma.ch[ch].ctrl_trig=0; /* W1C faults; EN remains disabled. */
}
static inline dma_channel_config dma_channel_get_default_config(uint ch){return (dma_channel_config){.chain=ch,.dreq=DREQ_FORCE};}
static inline void channel_config_set_transfer_data_size(dma_channel_config *c,uint n){c->size=n;}
static inline void channel_config_set_read_increment(dma_channel_config *c,bool b){c->read_inc=b;}
static inline void channel_config_set_write_increment(dma_channel_config *c,bool b){c->write_inc=b;}
static inline void channel_config_set_dreq(dma_channel_config *c,uint n){c->dreq=n;}
static inline void channel_config_set_chain_to(dma_channel_config *c,uint n){c->chain=n;}
static inline void trigger(uint ch){assert(!busy[ch]);remaining[ch]=reload[ch];busy[ch]=true;}
static inline void dma_channel_configure(uint ch,const dma_channel_config *c,volatile void *to,const volatile void *from,uint n,bool start){
    assert(!start&&n==1&&c->size==DMA_SIZE_32&&!c->read_inc&&c->write_inc);
    cfg[ch]=*c;reload[ch]=remaining[ch]=n;reads[ch]=from;hw_dma.ch[ch].write_addr=(uintptr_t)to;
    /* A normal configuration leaves sticky W1C faults intact. */
    hw_dma.ch[ch].ctrl_trig|=1;busy[ch]=false;
}
static inline void dma_start_channel_mask(uint mask){assert(mask==(1u<<SYNC_IO_REFERENCE_POP_DMA_CH));trigger(SYNC_IO_REFERENCE_POP_DMA_CH);}
static inline bool step_dma(uint ch){
    if(!busy[ch]||!(hw_dma.ch[ch].ctrl_trig&1))return false;
    if(cfg[ch].dreq!=DREQ_FORCE&&!fifo_count)return false;
    uint32_t value;
    if(reads[ch]==&hw_pio.rxf[2]){
        assert(cfg[ch].dreq==2&&fifo_count);value=fifo[0];
        memmove(fifo,fifo+1,(--fifo_count)*sizeof(fifo[0]));
    }else {assert(reads[ch]==&hw_timer.timerawl&&cfg[ch].dreq==DREQ_FORCE);value=*reads[ch];}
    *(volatile uint32_t *)hw_dma.ch[ch].write_addr=value;
    hw_dma.ch[ch].write_addr+=4;
    if(!--remaining[ch]){busy[ch]=false;if(cfg[ch].chain!=ch)trigger(cfg[ch].chain);}
    return true;
}
static inline void pump(void){for(unsigned n=0;n<8;n++){bool progress=step_dma(SYNC_IO_REFERENCE_POP_DMA_CH);progress=step_dma(SYNC_IO_REFERENCE_STAMP_DMA_CH)||progress;if(!progress)return;}assert(false);}
static inline void fifo_push(uint32_t value){assert(enabled&&fifo_count<4);fifo[fifo_count++]=value;}
static inline void abort_ack(void){hw_dma.abort=0;busy[SYNC_IO_REFERENCE_POP_DMA_CH]=busy[SYNC_IO_REFERENCE_STAMP_DMA_CH]=false;}
bool sync_io_core_reference_reserve(const void *p){assert(p&&mock_core==0);if(gate)return false;gate=true;return true;}
bool sync_io_core_reference_release(const void *p){assert(p&&gate&&mock_core==0);gate=false;return true;}
static sync_io_persona_manager_hooks_t hooks;
static void *hook_ctx;
static const sync_io_persona_descriptor_t descriptor={.id=SYNC_IO_PERSONA_ID_REFERENCE_MONITOR};
bool sync_io_pio0_runtime_prepare(sync_io_persona_id_t id,const sync_io_persona_manager_hooks_t *h,void *ctx,sync_io_persona_manager_handle_t *out){
    assert(id==descriptor.id);hooks=*h;hook_ctx=ctx;
    bool ok=h->load(ctx,&descriptor,SYNC_IO_REFERENCE_DMA_MASK)&&h->arm(ctx,&descriptor,SYNC_IO_REFERENCE_DMA_MASK);
    if(!ok)h->cleanup(ctx,&descriptor,SYNC_IO_REFERENCE_DMA_MASK);else out->generation=1;return ok;
}
bool sync_io_pio0_runtime_release(sync_io_persona_manager_handle_t *h){assert(h->generation==1);hooks.cleanup(hook_ctx,&descriptor,SYNC_IO_REFERENCE_DMA_MASK);return true;}
#endif
