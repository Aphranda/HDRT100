#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_plan.h"
#include "tdma_origin_build_job.h"

static tdma_origin_build_job_t s_tdma_origin_build_job;
static tdma_origin_plan_builder_t test_builder;
tdma_origin_build_result_t tdma_origin_plan_step(tdma_origin_plan_builder_t *b)
{ (void)b; return TDMA_ORIGIN_BUILD_FAILED; }
void tdma_origin_plan_cancel(tdma_origin_plan_builder_t *b)
{ b->active = b->complete = false; b->failed = true; }

typedef unsigned int uint;
typedef int tdma_pio_spi_program_persona_t;
typedef struct {
    volatile uint32_t flight_origin_record_guard;
    uint32_t flight_origin_record_epoch, flight_origin_record_published_version, flight_origin_record_fault;
    bool flight_origin_record_frozen, armed, flight_overlay_dma_active, rx_capture_active;
    bool flight_origin_workspace_owned, flight_origin_rx_observation_ready;
    struct { uint32_t stage; } flight_origin_prepare;
    struct { uint32_t armed, last_error; } snapshot;
} tdma_pio_spi_phys_t;
static struct { struct {
    tdma_origin_plan_state_t state;
    tdma_origin_record_t record[TDMA_ORIGIN_RECORD_COUNT];
} origin; } s_tdma_pio_spi_workspace;
#define s_tdma_origin (s_tdma_pio_spi_workspace.origin)
enum { TDMA_ORIGIN_PREPARE_COMPLETE = 9, TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US = 1000,
       TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY = 37 };
static int s_tdma_pio_spi_command_dma_channel=6, s_tdma_pio_spi_executor_dma_channel=8;
static int s_tdma_pio_spi_tx_dma_channel=5, s_tdma_pio_spi_rx_dma_channel=4;
static int s_tdma_pio_spi_program_manager;
static bool stop_ok, select_ok;
static uint32_t clock_calls, copy_mode;
static uint64_t end_tick;
static tdma_pio_spi_phys_t phys;
static uint64_t vdc_timestamp_clock_read_ticks64(void) { return clock_calls++ ? end_tick : 100; }
static uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000; }
static uint64_t tdma_pio_spi_phys_now_us(void) { return 100; }
static void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void tdma_pio_spi_phys_pause_sm_pair(tdma_pio_spi_phys_t *p) { (void)p; }
static void tdma_pio_spi_phys_set_line_drivers(bool enabled) { (void)enabled; }
static bool tdma_pio_spi_phys_stop_dma_chain(uint32_t l,uint32_t e,uint32_t c,uint64_t d)
{ assert(l==64 && e==256 && c==48 && d==1100);return stop_ok; }
static bool tdma_pio_spi_phys_stop_command_dma(tdma_pio_spi_phys_t *p);
static bool tdma_pio_spi_programs_select(int *manager,tdma_pio_spi_phys_t *p,int persona)
{ (void)manager;(void)persona;return tdma_pio_spi_phys_stop_command_dma(p) && select_ok; }
static void controlled_copy(void *to,const void *from,size_t size)
{
    if (copy_mode==1) {
        memcpy(to,from,size/2);
        phys.flight_origin_record_guard+=2;
        phys.flight_origin_record_frozen=false;
        memset(&s_tdma_origin.record,0xaa,sizeof(s_tdma_origin.record));
        memcpy((uint8_t *)to+size/2,(const uint8_t *)from+size/2,size-size/2);
    } else memcpy(to,from,size);
}
#define memcpy controlled_copy
#include "origin_record_frozen_impl.inc"
#undef memcpy

static void setup(void)
{
    memset(&phys,0,sizeof(phys));memset(&s_tdma_origin,0,sizeof(s_tdma_origin));
    phys.flight_origin_record_epoch=77;
    phys.flight_origin_workspace_owned=true;
    phys.flight_origin_prepare.stage=TDMA_ORIGIN_PREPARE_COMPLETE;
    phys.armed=true;
    s_tdma_origin.state.record_published_version=20;
    for (uint32_t age=0;age<7;age++) {
        tdma_origin_record_t *r=&s_tdma_origin.record[(9-age)%8];
        *r=(tdma_origin_record_t){.observation={100-age,0xabc000u+age,5,0,17+age,1},
            .returned_trailer=0x80000000u+age,.epoch=77,.flags=1,
            .format=TDMA_ORIGIN_RECORD_FORMAT_RTT,.sequence_end=100-age};
    }
    // Next DMA target may have been stopped halfway through overwriting.
    s_tdma_origin.record[2].observation.sequence=12345;
    s_tdma_origin.record[2].sequence_end=67890;
    stop_ok=select_ok=true;clock_calls=copy_mode=0;end_tick=101;
}

int main(void)
{
    tdma_origin_record_frozen_t out;
    setup();
    test_builder.active = true;
    assert(tdma_origin_build_job_request(&s_tdma_origin_build_job, &test_builder));
    assert(tdma_origin_build_job_core0_claim(&s_tdma_origin_build_job));
    assert(!tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(phys.flight_origin_workspace_owned && !phys.flight_origin_record_frozen);
    tdma_origin_build_job_core0_build_claimed(&s_tdma_origin_build_job);
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!phys.flight_origin_workspace_owned);
    setup();
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    stop_ok=false;
    assert(!tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(phys.flight_origin_workspace_owned && !phys.flight_origin_record_frozen);
    stop_ok=true;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!phys.flight_origin_workspace_owned && phys.flight_origin_record_frozen);
    uint32_t guard=phys.flight_origin_record_guard;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(phys.flight_origin_record_guard==guard);
    for (uint32_t age=0;age<7;age++) {
        clock_calls=0;
        assert(tdma_pio_spi_phys_origin_get_frozen_record(&phys,age,&out));
        assert(out.epoch==77 && out.published_version==20 && out.fault==0);
        assert(out.record.observation.sequence==100-age);
        assert(out.record.returned_trailer==0x80000000u+age);
    }
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,7,&out));
    clock_calls=0;copy_mode=1;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    end_tick=250101;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    phys.flight_origin_record_guard|=1;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    s_tdma_origin.record[1].epoch++;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    s_tdma_origin.record[1].sequence_end++;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    // Selection may itself STOP/freeze an active origin; invalidate again
    // before its caller can initialize another persona in the shared union.
    setup();assert(tdma_pio_spi_phys_select_program_persona(&phys,1));
    assert(!phys.flight_origin_record_frozen);
    setup();select_ok=false;assert(!tdma_pio_spi_phys_select_program_persona(&phys,1));
    assert(!phys.flight_origin_record_frozen);
    puts("stopped origin archive, pending STOP, persona reuse and bounded copy passed");
    return 0;
}
