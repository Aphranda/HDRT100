#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_exchange.h"

typedef struct {
    uint32_t flight_origin_record_epoch;
    uint32_t flight_origin_record_guard;
    tdma_origin_live_snapshot_t flight_origin_live;
} tdma_pio_spi_phys_t;
static tdma_pio_spi_phys_t phys;
static struct {
    tdma_origin_plan_state_t state;
    tdma_origin_exchange_t exchange;
    tdma_origin_record_t record[TDMA_ORIGIN_RECORD_COUNT];
} s_tdma_origin;
static unsigned tick_calls, fence_mode;
static unsigned try_calls, fail_try_call;
static bool rate_change_on_first;
static uint32_t current_hz = 250000000u;
static uint64_t finish_tick;
static bool healthy;
static uint64_t vdc_timestamp_clock_read_ticks64(void)
{ return tick_calls++ ? finish_tick : 100u; }
static uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }
static bool vdc_timestamp_clock_try_read_ticks64(uint32_t expected_hz,uint64_t *ticks)
{
    ++try_calls;
    if(try_calls==fail_try_call || expected_hz==0u || expected_hz!=current_hz) return false;
    *ticks=try_calls==1u?100u:finish_tick;
    if(try_calls==1u && rate_change_on_first) phys.flight_origin_live.sample.record.raw_time.tick_hz/=2u;
    return true;
}
static void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void test_fence(int order)
{
    __atomic_thread_fence(order);
    if(fence_mode==1u) phys.flight_origin_record_guard+=2u;
}
#define __atomic_thread_fence test_fence
static bool tdma_pio_spi_phys_origin_healthy(void *context)
{ return context==&phys && healthy; }
bool tdma_pio_spi_phys_origin_get_live_snapshot(const tdma_pio_spi_phys_t *,tdma_origin_live_snapshot_t *);
static bool tdma_runtime_owner_get_origin_live_snapshot(tdma_origin_live_snapshot_t *out)
{ return tdma_pio_spi_phys_origin_get_live_snapshot(&phys,out); }
static bool tdma_runtime_owner_get_origin_frozen_record(uint32_t age,tdma_origin_record_frozen_t *out)
{ if(age!=3u)return false;*out=phys.flight_origin_live.sample;return true; }
typedef struct {char label[32];uint32_t values[32];unsigned count;} scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR 0
#define TRUE 1
static bool SCPI_ParamUInt32(scpi_t *c,uint32_t *out,int required)
{(void)c;(void)required;*out=3u;return true;}
static void SCPI_ResultText(scpi_t *c,const char *s) {strcpy(c->label,s);}
static void SCPI_ResultUInt32(scpi_t *c,uint32_t n)
{assert(c->count<32u);c->values[c->count++]=n;}
#include "live_impl.inc"

static void setup(void)
{
    memset(&phys,0,sizeof(phys));memset(&s_tdma_origin,0,sizeof(s_tdma_origin));
    tick_calls=fence_mode=0u;finish_tick=101u;healthy=true;
    try_calls=fail_try_call=0u;current_hz=250000000u;
    rate_change_on_first=false;
    phys.flight_origin_record_epoch=7u;
    s_tdma_origin.exchange.state=&s_tdma_origin.state;
    s_tdma_origin.state.record_epoch=7u;s_tdma_origin.state.record_published_version=2u;
    s_tdma_origin.record[0]=(tdma_origin_record_t){
        .observation={.sequence=33u,.identity=0x1234u,.local_generation=5u},
        .epoch=7u,.format=TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME,.sequence_end=33u,
        .raw_time={.arm_before={4u,100u,4u},.arm_after={4u,110u,4u},
            .latch_remaining=UINT32_MAX-99u,.tick_hz=250000000u}};
}

int main(void)
{
    tdma_origin_observation_t observation;
    tdma_origin_live_snapshot_t out,sentinel;
    setup();
    /* Collection does not consume the adapter's distinct boundary cursor;
     * raw capture works even when the transport observer has no new value. */
    assert(!tdma_pio_spi_phys_origin_observe(&phys,&observation));
    assert(phys.flight_origin_live.copy_count==1u && phys.flight_origin_live.active==1u);
    assert(s_tdma_origin.exchange.observation_version==0u);
    const tdma_origin_live_snapshot_t first=phys.flight_origin_live;
    tick_calls=0;tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(memcmp(&first,&phys.flight_origin_live,sizeof(first))==0);
    /* A later complete physical event advances independently of wire success. */
    s_tdma_origin.record[1]=s_tdma_origin.record[0];
    s_tdma_origin.record[1].observation.sequence=34u;s_tdma_origin.record[1].sequence_end=34u;
    s_tdma_origin.state.record_published_version=4u;tick_calls=0;
    tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.copy_count==2u);
    assert(phys.flight_origin_live.sample.record.sequence_end==34u);
    s_tdma_origin.state.fault=1u;tick_calls=0;
    tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.reject_count==1u && phys.flight_origin_live.copy_count==2u);
    assert(phys.flight_origin_live.sample.record.sequence_end==34u);
    /* Expired copies cannot publish even when the exchange copy was coherent. */
    setup();finish_tick=250101u;tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.retained==0u && phys.flight_origin_live.reject_count==1u);
    setup();finish_tick=99u;tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.retained==0u);
    setup();phys.flight_origin_live.copy_count=UINT32_MAX;
    tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.copy_count==UINT32_MAX);
    phys.flight_origin_live.reject_count=UINT32_MAX;s_tdma_origin.state.fault=1u;tick_calls=0;
    tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.reject_count==UINT32_MAX);
    setup();healthy=false;assert(!tdma_pio_spi_phys_origin_observe(&phys,&observation));
    assert(phys.flight_origin_live.copy_count==0u);
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);
    tick_calls=0;assert(tdma_pio_spi_phys_origin_get_live_snapshot(&phys,&out));
    assert(memcmp(&out,&phys.flight_origin_live,sizeof(out))==0);
    assert(tick_calls==0u && try_calls==2u);
    memset(&sentinel,0x5a,sizeof(sentinel));
    for(unsigned mode=0;mode<10u;++mode) {
        setup();tdma_pio_spi_phys_origin_collect_live(&phys);tick_calls=0;out=sentinel;
        if(mode==0u)phys.flight_origin_record_guard=1u;
        if(mode==1u)fence_mode=1u;
        if(mode==2u)finish_tick=250101u;
        if(mode==3u)finish_tick=99u;
        if(mode==4u)fail_try_call=1u;
        if(mode==5u)fail_try_call=2u;
        if(mode==6u)rate_change_on_first=true;
        if(mode==7u)phys.flight_origin_live.sample.record.raw_time.tick_hz=0u;
        if(mode==8u){phys.flight_origin_record_guard=UINT32_MAX-1u;fence_mode=1u;}
        if(mode==9u)current_hz=125000000u;
        assert(!tdma_pio_spi_phys_origin_get_live_snapshot(&phys,&out));
        assert(memcmp(&out,&sentinel,sizeof(out))==0);
        assert(tick_calls==0u && try_calls<=2u);
        if(mode==4u)assert(try_calls==1u);
        if(mode==5u)assert(try_calls==2u);
    }
    assert(!tdma_pio_spi_phys_origin_get_live_snapshot(NULL,&out));
    assert(!tdma_pio_spi_phys_origin_get_live_snapshot(&phys,NULL));
    setup();out=sentinel;
    assert(tdma_pio_spi_phys_origin_get_live_snapshot(&phys,&out));
    assert(!out.retained && !out.active && try_calls==0u && tick_calls==0u);
    scpi_t empty={0};
    assert(scpi_calibration_origin_live_q(&empty)==SCPI_RES_OK);
    assert(!strcmp(empty.label,"ORIGINLIVE") && empty.count==29u);
    for(unsigned i=0;i<empty.count;i++)assert(empty.values[i]==0u);
    assert(try_calls==0u && tick_calls==0u);
    for(unsigned mode=0;mode<3;mode++) {
        setup();out=sentinel;
        if(mode==0)phys.flight_origin_record_guard=1u;
        if(mode==1)fence_mode=1u;
        if(mode==2)phys.flight_origin_live.active=1u;
        assert(!tdma_pio_spi_phys_origin_get_live_snapshot(&phys,&out));
        assert(!memcmp(&out,&sentinel,sizeof(out)) && try_calls==0u && tick_calls==0u);
    }
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);tick_calls=0;finish_tick=250100u;
    assert(tdma_pio_spi_phys_origin_get_live_snapshot(&phys,&out));
    assert(try_calls==2u && tick_calls==0u);
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);tick_calls=0;
    phys.flight_origin_live.sample.record.raw_time.tick_hz=current_hz=125000000u;
    finish_tick=125100u;
    assert(tdma_pio_spi_phys_origin_get_live_snapshot(&phys,&out));
    assert(out.sample.record.raw_time.tick_hz==125000000u && try_calls==2u && tick_calls==0u);
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);tick_calls=0;
    scpi_t live={0},old={0};
    assert(scpi_calibration_origin_live_q(&live)==SCPI_RES_OK);
    assert(strcmp(live.label,"ORIGINLIVE")==0 && live.count==29u);
    assert(live.values[0]==1u && live.values[1]==1u && live.values[2]==1u && live.values[3]==0u);
    assert(live.values[4]==7u && live.values[5]==2u && live.values[7]==33u);
    assert(live.values[28]==250000000u);
    assert(scpi_calibration_origin_record_q(&old)==SCPI_RES_OK);
    assert(strcmp(old.label,"ORIGINRECORD")==0 && old.count==26u && old.values[0]==3u);
    assert(memcmp(old.values+1,live.values+4,25u*sizeof(uint32_t))==0);
    phys.flight_origin_record_guard|=1u;live=(scpi_t){0};tick_calls=0;
    assert(scpi_calibration_origin_live_q(&live)==SCPI_RES_OK);
    assert(strcmp(live.label,"UNAVAILABLE")==0 && live.count==0u);
    puts("actual live collection, retirement companion, readback and wire serialization passed");
    return 0;
}
