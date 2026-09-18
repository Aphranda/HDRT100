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
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
typedef struct {
    volatile uint32_t flight_origin_record_guard;
    uint32_t flight_origin_record_epoch, flight_origin_record_published_version, flight_origin_record_fault;
    bool flight_origin_record_frozen, armed, flight_overlay_dma_active, rx_capture_active;
    bool flight_origin_workspace_owned, flight_origin_rx_observation_ready;
    struct { uint32_t stage; } flight_origin_prepare;
    struct { uint32_t armed, last_error; } snapshot;
    tdma_origin_first_record_t flight_origin_first_record;
    uint32_t flight_origin_first_readable_epoch, flight_origin_first_expected_sequence;
    tdma_origin_live_snapshot_t flight_origin_live;
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
static size_t copy_split;
static uint64_t end_tick;
static tdma_pio_spi_phys_t phys;
static uint64_t vdc_timestamp_clock_read_ticks64(void) { return clock_calls++ ? end_tick : 100; }
static uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000; }
static uint64_t tdma_pio_spi_phys_now_us(void) { return 100; }
static void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void tdma_pio_spi_phys_pause_sm_pair(tdma_pio_spi_phys_t *p) { (void)p; }
static void tdma_pio_spi_phys_set_line_drivers(bool enabled) { (void)enabled; }
/* Persona selection also retires independent geometry/event diagnostics.
 * Their lifecycle is covered separately; archive STOP must still compile the
 * complete current selector instead of an obsolete copy of its body. */
static void tdma_geometry_persona(tdma_pio_spi_program_persona_t persona) { (void)persona; }
static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *p) { (void)p; }
static void tdma_priority_stop(void) {}
static bool tdma_pio_spi_phys_stop_dma_chain(uint32_t l,uint32_t e,uint32_t c,uint64_t d)
{ assert(l==64 && e==256 && c==48 && d==1100);return stop_ok; }
static bool tdma_pio_spi_phys_stop_command_dma(tdma_pio_spi_phys_t *p);
static bool tdma_pio_spi_programs_select(int *manager,tdma_pio_spi_phys_t *p,int persona)
{ (void)manager;(void)persona;return tdma_pio_spi_phys_stop_command_dma(p) && select_ok; }
static void controlled_copy(void *to,const void *from,size_t size)
{
    if (copy_mode==1) {
        assert(copy_split<size);
        memcpy(to,from,copy_split);
        phys.flight_origin_record_guard+=2;
        phys.flight_origin_record_frozen=false;
        phys.flight_origin_first_readable_epoch=0u;
        memset(&s_tdma_origin.record,0xaa,sizeof(s_tdma_origin.record));
        memset(&phys.flight_origin_first_record,0xaa,sizeof(phys.flight_origin_first_record));
        memcpy((uint8_t *)to+copy_split,(const uint8_t *)from+copy_split,size-copy_split);
    } else if (copy_mode==2 || copy_mode==3) {
        assert(copy_split<size);
        memcpy(to,from,copy_split);
        if(copy_mode==2)phys.flight_origin_first_record.published_version=0u;
        else phys.flight_origin_first_readable_epoch=0u;
        memcpy((uint8_t *)to+copy_split,(const uint8_t *)from+copy_split,size-copy_split);
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
            .format=TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME,
            .raw_time={.arm_before={7,1000-age,7},.arm_after={7,2000-age,7},
                .latch_remaining=0xfffffff0u-age,.latch_fstat=0x1234u+age,
                .arm_padout=0x4000000,.tick_hz=250000000},.sequence_end=100-age};
    }
    // Next DMA target may have been stopped halfway through overwriting.
    s_tdma_origin.record[2].observation.sequence=12345;
    s_tdma_origin.record[2].sequence_end=67890;
    phys.flight_origin_first_record.record=s_tdma_origin.record[1];
    phys.flight_origin_first_record.record.observation.sequence=42u;
    phys.flight_origin_first_record.record.sequence_end=42u;
    phys.flight_origin_first_record.record.raw_time.arm_before[1]=777u;
    phys.flight_origin_first_record.published_version=TDMA_ORIGIN_FIRST_RECORD_VERSION;
    phys.flight_origin_first_readable_epoch=77u;
    phys.flight_origin_first_expected_sequence=42u;
    stop_ok=select_ok=true;clock_calls=copy_mode=0;end_tick=101;
    copy_split=sizeof(tdma_origin_record_t)/2;
}

static void check_raw_retirement(void)
{
    tdma_origin_record_frozen_t out;
    for (size_t split=1;split<sizeof(tdma_origin_record_t);++split) {
        setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
        copy_mode=1;copy_split=split;
        assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    }
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    phys.flight_origin_record_guard=UINT32_MAX-1u;
    copy_mode=1;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    assert(phys.flight_origin_record_guard==0);

    // A retained fault is raw diagnostic evidence, never an eligibility flag.
    setup();s_tdma_origin.state.fault=9;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    assert(out.fault==9 && out.record.raw_time.arm_before[1]==1000);

    // Publication counter wrap still chooses the last complete record, not
    // the next target that may have been interrupted by STOP.
    setup();
    tdma_origin_record_t newest=s_tdma_origin.record[1];
    memset(s_tdma_origin.record,0,sizeof(s_tdma_origin.record));
    s_tdma_origin.record[7]=newest;
    s_tdma_origin.state.record_published_version=0;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    assert(out.published_version==0 && out.record.raw_time.arm_after[1]==2000);
    clock_calls=0;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,1,&out));

    // Preparing another persona makes the previous epoch unavailable before
    // the union is reused; a later STOP cannot revive the retained archive.
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(tdma_pio_spi_phys_select_program_persona(&phys,1));
    memset(s_tdma_origin.record,0x5a,sizeof(s_tdma_origin.record));
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));

    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    s_tdma_origin.record[1].format=TDMA_ORIGIN_RECORD_FORMAT_RTT;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    s_tdma_origin.record[1].epoch=phys.flight_origin_record_epoch-1;
    assert(!tdma_pio_spi_phys_origin_get_frozen_record(&phys,0,&out));
}

static void check_first_record_lifetime(void)
{
    tdma_origin_first_record_t out;
    assert(!tdma_pio_spi_phys_origin_get_first_record(NULL,&out));
    setup();assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,NULL));
    assert(tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    assert(out.published_version==2u && out.record.observation.sequence==42u);
    assert(out.record.raw_time.arm_before[1]==777u);
    /* First commit can precede ring0 publication. STOP and later faults must
     * preserve it without pretending a later successful return was first. */
    setup();s_tdma_origin.state.record_published_version=0u;
    memset(s_tdma_origin.record,0,sizeof(s_tdma_origin.record));
    phys.flight_origin_first_record.record.flags=0u;
    phys.flight_origin_first_record.record.raw_time.latch_remaining=0u;
    phys.flight_origin_first_record.record.raw_time.latch_fstat=1u<<10;
    phys.flight_origin_first_record.record.raw_time.arm_after[2]++;
    s_tdma_origin.state.fault=9u;
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    assert(out.record.flags==0u && out.record.raw_time.latch_remaining==0u);
    assert(out.record.raw_time.arm_after[0]!=out.record.raw_time.arm_after[2]);
    setup();stop_ok=false;assert(!tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(phys.flight_origin_workspace_owned);
    assert(tdma_pio_spi_phys_origin_get_first_record(&phys,&out));

    for(uint32_t mode=1u;mode<=3u;++mode)for(size_t split=1;split<sizeof(out.record);++split) {
        setup();copy_mode=mode;copy_split=split;
        assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    }
    setup();phys.flight_origin_record_guard=UINT32_MAX-1u;copy_mode=1;
    assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    assert(phys.flight_origin_record_guard==0u);
    for(uint32_t mode=0u;mode<10u;++mode) {
        setup();
        switch(mode) {
        case 0:phys.flight_origin_first_record.published_version=0u;break;
        case 1:phys.flight_origin_first_record.published_version=4u;break;
        case 2:phys.flight_origin_first_readable_epoch=0u;break;
        case 3:phys.flight_origin_first_readable_epoch++;break;
        case 4:phys.flight_origin_first_record.record.epoch++;break;
        case 5:phys.flight_origin_first_record.record.format=TDMA_ORIGIN_RECORD_FORMAT_RTT;break;
        case 6:phys.flight_origin_first_record.record.observation.sequence++;break;
        case 7:phys.flight_origin_first_record.record.sequence_end++;break;
        case 8:end_tick=99u;break;
        case 9:end_tick=250101u;break;
        }
        assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    }
    setup();assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    const tdma_origin_first_record_t prior=phys.flight_origin_first_record;
    assert(tdma_pio_spi_phys_select_program_persona(&phys,s_tdma_pio_spi_program_persona));
    assert(phys.flight_origin_first_record.published_version==2u);
    assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    assert(tdma_pio_spi_phys_stop_command_dma(&phys));
    assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    setup();select_ok=false;assert(!tdma_pio_spi_phys_select_program_persona(&phys,1));
    assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));

    /* Actual SEED reset clears the whole old body/commit. A new epoch may
     * wrap and the expected sequence may be zero; neither revives old data. */
    setup();phys.flight_origin_record_epoch=1u;
    tdma_pio_spi_phys_origin_first_reset(&phys,1u,0u);
    for(size_t i=0;i<sizeof(phys.flight_origin_first_record);++i)
        assert(((uint8_t *)&phys.flight_origin_first_record)[i]==0u);
    assert(phys.flight_origin_first_expected_sequence==0u);
    assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    phys.flight_origin_first_record=prior;clock_calls=0;
    assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    phys.flight_origin_first_record.record.epoch=1u;
    phys.flight_origin_first_record.record.observation.sequence=0u;
    phys.flight_origin_first_record.record.sequence_end=0u;clock_calls=0;
    assert(tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
    tdma_pio_spi_phys_origin_first_reset(&phys,0u,1u);clock_calls=0;
    phys.flight_origin_first_record=prior;
    assert(!tdma_pio_spi_phys_origin_get_first_record(&phys,&out));
}

int main(void)
{
    tdma_origin_record_frozen_t out;
    /* Actual successful/failed STOP and persona invalidation retire only the
     * live permission. A copied steady-state record survives for readback. */
    for (unsigned mode=0;mode<3;++mode) {
        setup();
        phys.flight_origin_live=(tdma_origin_live_snapshot_t){
            .retained=1,.active=1,.copy_count=17,
            .sample={.epoch=77,.published_version=20,.record={.sequence_end=100}}};
        tdma_origin_live_snapshot_t expected=phys.flight_origin_live;
        expected.active=0;
        if(mode==0) assert(tdma_pio_spi_phys_stop_command_dma(&phys));
        else if(mode==1) {stop_ok=false;assert(!tdma_pio_spi_phys_stop_command_dma(&phys));}
        else tdma_pio_spi_phys_origin_record_invalidate(&phys);
        assert(memcmp(&phys.flight_origin_live,&expected,sizeof(expected))==0);
    }
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
        assert(out.record.raw_time.arm_before[1]==1000-age);
        assert(out.record.raw_time.arm_after[1]==2000-age);
        assert(out.record.raw_time.latch_remaining==0xfffffff0u-age);
        assert(out.record.raw_time.latch_fstat==0x1234u+age);
        assert(out.record.raw_time.tick_hz==250000000);
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
    check_raw_retirement();
    check_first_record_lifetime();
    puts("stopped origin archive, pending STOP, persona reuse and bounded copy passed");
    return 0;
}
