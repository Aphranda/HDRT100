#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_exchange.h"

typedef struct {
    uint32_t flight_origin_record_epoch;
    uint32_t flight_origin_record_guard;
    uint32_t flight_origin_first_readable_epoch;
    uint32_t flight_origin_first_expected_sequence;
    bool flight_origin_record_frozen;
    tdma_origin_first_record_t flight_origin_first_record;
    struct { uint32_t tx_clock_latch_sm; } flight_resources;
    uint32_t tx_csn_pin;
    tdma_origin_live_snapshot_t flight_origin_live;
} tdma_pio_spi_phys_t;
static tdma_pio_spi_phys_t phys;
#define s_tdma_pio_spi_phys phys
static bool s_tdma_runtime_owner_initialized;
static struct {
    tdma_origin_plan_state_t state;
    tdma_origin_exchange_t exchange;
    tdma_origin_record_t record[TDMA_ORIGIN_RECORD_COUNT];
} s_tdma_origin;
static unsigned tick_calls, fence_mode, fence_calls;
static unsigned try_calls, fail_try_call, retire_try_call;
static bool rate_change_on_first;
static uint32_t current_hz = 250000000u;
static uint64_t finish_tick;
static bool healthy;
static void tdma_pio_spi_phys_origin_record_invalidate(tdma_pio_spi_phys_t *);
static void tdma_pio_spi_phys_origin_first_reset(tdma_pio_spi_phys_t *, uint32_t, uint32_t);
static void tdma_pio_spi_phys_origin_collect_live(tdma_pio_spi_phys_t *);
static uint64_t vdc_timestamp_clock_read_ticks64(void)
{ return tick_calls++ ? finish_tick : 100u; }
static uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }
static bool vdc_timestamp_clock_try_read_ticks64(uint32_t expected_hz,uint64_t *ticks)
{
    ++try_calls;
    if(try_calls==fail_try_call || expected_hz==0u || expected_hz!=current_hz) return false;
    *ticks=try_calls==1u?100u:finish_tick;
    if(try_calls==1u && rate_change_on_first) phys.flight_origin_live.sample.record.raw_time.tick_hz/=2u;
    if(try_calls==retire_try_call) tdma_pio_spi_phys_origin_record_invalidate(&phys);
    return true;
}
static void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static void test_fence(int order)
{
    __atomic_thread_fence(order);
    ++fence_calls;
    if(fence_mode==1u) phys.flight_origin_record_guard+=2u;
    if(fence_calls==2u && fence_mode==2u) tdma_pio_spi_phys_origin_record_invalidate(&phys);
    if(fence_calls==2u && fence_mode==3u) {
        phys.flight_origin_record_epoch=8u;
        tdma_pio_spi_phys_origin_first_reset(&phys,8u,40u);
    }
    if(fence_calls==2u && fence_mode==4u) {
        s_tdma_origin.record[1]=s_tdma_origin.record[0];
        s_tdma_origin.record[1].observation.sequence=34u;
        s_tdma_origin.record[1].sequence_end=34u;
        s_tdma_origin.state.record_published_version=4u;
        phys.flight_resources.tx_clock_latch_sm=3u;phys.tx_csn_pin=31u;
        tick_calls=0u;
        tdma_pio_spi_phys_origin_collect_live(&phys);
    }
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
    tick_calls=fence_mode=fence_calls=0u;finish_tick=101u;healthy=true;
    try_calls=fail_try_call=retire_try_call=0u;current_hz=250000000u;
    rate_change_on_first=false;
    s_tdma_runtime_owner_initialized=true;
    phys.flight_origin_record_epoch=7u;
    phys.flight_resources.tx_clock_latch_sm=2u;phys.tx_csn_pin=26u;
    s_tdma_origin.exchange.state=&s_tdma_origin.state;
    s_tdma_origin.state.record_epoch=7u;s_tdma_origin.state.record_published_version=2u;
    s_tdma_origin.record[0]=(tdma_origin_record_t){
        .observation={.sequence=33u,.identity=0x1234u,.local_generation=5u},
        .epoch=7u,.format=TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME,.sequence_end=33u,
        .flags=TDMA_ORIGIN_RECORD_TRANSPORT_CHECKED,
        .raw_time={.arm_before={4u,100u,4u},.arm_after={4u,110u,4u},
            .latch_remaining=UINT32_MAX-99u,.arm_padout=1u<<26u,.tick_hz=250000000u}};
}

static void test_raw_reference_provenance(void)
{
    tdma_origin_raw_reference_t out,sentinel;
    memset(&sentinel,0xa5,sizeof(sentinel));
    /* Two scalar provenance words; no change to the DMA record/wire body. */
    _Static_assert(sizeof(tdma_origin_live_snapshot_t)==
        sizeof(tdma_origin_record_frozen_t)+6u*sizeof(uint32_t),"LIVE metadata budget");
    _Static_assert(sizeof(tdma_origin_record_t)==88u,"DMA record ABI unchanged");
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.latch_sm==2u && phys.flight_origin_live.csn_pin==26u);
    /* The mutable resource fields may already describe another persona.
     * The frozen record must still be interpreted with its own provenance. */
    phys.flight_resources.tx_clock_latch_sm=0u;phys.tx_csn_pin=25u;
    phys.flight_origin_live.sample.record.raw_time.latch_fstat=1u<<8u;
    tick_calls=0u;out=sentinel;
    assert(tdma_pio_spi_phys_origin_get_raw_reference(&phys,&out));
    assert(out.epoch==7u && out.sequence==33u && out.published_version==2u);
    assert(out.timer_lower==0x40000012aull && out.timer_upper==0x400000134ull);
    assert(tick_calls==0u && try_calls==2u);
    /* Conversely, a currently nonempty SM/high pin cannot rescue a raw
     * sample whose captured SM was empty or whose captured CS was low. */
    for(unsigned mode=0;mode<4u;++mode) {
        setup();
        if(mode==0u)s_tdma_origin.record[0].raw_time.latch_fstat=1u<<(8u+2u);
        if(mode==1u)s_tdma_origin.record[0].raw_time.arm_padout=1u<<25u;
        if(mode==2u)phys.flight_resources.tx_clock_latch_sm=4u;
        if(mode==3u)phys.tx_csn_pin=32u;
        tdma_pio_spi_phys_origin_collect_live(&phys);
        phys.flight_resources.tx_clock_latch_sm=0u;phys.tx_csn_pin=25u;
        tick_calls=0u;out=sentinel;
        assert(!tdma_pio_spi_phys_origin_get_raw_reference(&phys,&out));
        assert(!memcmp(&out,&sentinel,sizeof(out)) && tick_calls==0u);
    }
    /* Repeated collection of the same record does not replace its resource
     * association; only a new accepted record can publish new provenance. */
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);
    const tdma_origin_live_snapshot_t first=phys.flight_origin_live;
    phys.flight_resources.tx_clock_latch_sm=3u;phys.tx_csn_pin=31u;
    tick_calls=0u;tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(!memcmp(&first,&phys.flight_origin_live,sizeof(first)));
    s_tdma_origin.record[1]=s_tdma_origin.record[0];
    s_tdma_origin.record[1].observation.sequence=s_tdma_origin.record[1].sequence_end=34u;
    s_tdma_origin.record[1].raw_time.arm_padout=1u<<31u;
    s_tdma_origin.state.record_published_version=4u;tick_calls=0u;
    tdma_pio_spi_phys_origin_collect_live(&phys);
    assert(phys.flight_origin_live.latch_sm==3u && phys.flight_origin_live.csn_pin==31u);
    assert(phys.flight_origin_live.sample.record.sequence_end==34u);
    tick_calls=0u;assert(tdma_pio_spi_phys_origin_get_raw_reference(&phys,&out));
    assert(out.sequence==34u && tick_calls==0u);
}

static void test_raw_reference_retirement_races(void)
{
    tdma_origin_raw_reference_t out,sentinel;
    memset(&sentinel,0x5a,sizeof(sentinel));
    for(unsigned mode=0;mode<10u;++mode) {
        setup();tdma_pio_spi_phys_origin_collect_live(&phys);
        tick_calls=0u;out=sentinel;
        if(mode==0u)phys.flight_origin_record_guard|=1u;
        if(mode==1u)fence_mode=1u;
        if(mode==2u)retire_try_call=1u;
        if(mode==3u)retire_try_call=2u;
        if(mode==4u)fence_mode=2u;
        if(mode==5u)fence_mode=3u;
        if(mode==6u)fence_mode=4u;
        if(mode==7u){phys.flight_origin_record_guard=UINT32_MAX-1u;fence_mode=2u;}
        if(mode==8u)tdma_pio_spi_phys_origin_record_invalidate(&phys);
        if(mode==9u)tdma_pio_spi_phys_origin_first_reset(&phys,8u,40u);
        assert(!tdma_pio_spi_phys_origin_get_raw_reference(&phys,&out));
        assert(!memcmp(&out,&sentinel,sizeof(out)));
        assert(try_calls<=2u);
        if(mode!=6u)assert(tick_calls==0u); /* Only injected Core1 collection reads lazily. */
    }
    setup();out=sentinel;
    assert(!tdma_pio_spi_phys_origin_get_raw_reference(NULL,&out));
    assert(!tdma_pio_spi_phys_origin_get_raw_reference(&phys,NULL));
    assert(!tdma_pio_spi_phys_origin_get_raw_reference(&phys,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)) && tick_calls==0u && try_calls==0u);
    /* A new ARM seed explicitly cancels permission but retains the exact
     * old record and resources for stopped diagnostic readback. */
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);
    tdma_origin_live_snapshot_t expected=phys.flight_origin_live;expected.active=0u;
    tdma_pio_spi_phys_origin_first_reset(&phys,8u,40u);
    assert(!memcmp(&phys.flight_origin_live,&expected,sizeof(expected)));
}

static void test_reference_epoch_permission(void)
{
    uint32_t epoch;
    const uint32_t sentinel=0xa5a5a5a5u;
    setup();epoch=sentinel;
    /* Empty/confirmed inactive is available even before TIMER1 exists. */
    current_hz=0u;fail_try_call=1u;
    assert(tdma_runtime_owner_get_origin_reference_epoch(&epoch) && epoch==0u);
    assert(tick_calls==0u && try_calls==0u);
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);tick_calls=0u;
    current_hz=0u;fail_try_call=1u;
    assert(tdma_runtime_owner_get_origin_reference_epoch(&epoch) && epoch==7u);
    assert(tick_calls==0u && try_calls==0u);
    tdma_pio_spi_phys_origin_record_invalidate(&phys);epoch=sentinel;
    assert(tdma_runtime_owner_get_origin_reference_epoch(&epoch) && epoch==0u);
    assert(phys.flight_origin_live.retained && phys.flight_origin_live.sample.epoch==7u);
    assert(tick_calls==0u && try_calls==0u);
    /* Only a new accepted publication supplies a new nonzero epoch. */
    setup();tdma_pio_spi_phys_origin_collect_live(&phys);
    tdma_pio_spi_phys_origin_first_reset(&phys,8u,40u);epoch=sentinel;
    assert(tdma_runtime_owner_get_origin_reference_epoch(&epoch) && epoch==0u);
    phys.flight_origin_record_epoch=8u;s_tdma_origin.state.record_epoch=8u;
    s_tdma_origin.record[0].epoch=8u;tick_calls=0u;
    tdma_pio_spi_phys_origin_collect_live(&phys);tick_calls=0u;
    assert(tdma_runtime_owner_get_origin_reference_epoch(&epoch) && epoch==8u);
    assert(tick_calls==0u && try_calls==0u);
    for(unsigned mode=0;mode<10u;++mode) {
        setup();tdma_pio_spi_phys_origin_collect_live(&phys);tick_calls=0u;epoch=sentinel;
        if(mode==0u)phys.flight_origin_record_guard|=1u;
        if(mode==1u)fence_mode=1u;
        if(mode==2u){fence_mode=2u;fence_calls=1u;}
        if(mode==3u){fence_mode=3u;fence_calls=1u;}
        if(mode==4u){phys.flight_origin_record_guard=UINT32_MAX-1u;fence_mode=2u;fence_calls=1u;}
        if(mode==5u)phys.flight_origin_live.active=2u;
        if(mode==6u)phys.flight_origin_live.retained=2u;
        if(mode==7u)phys.flight_origin_live.retained=0u;
        if(mode==8u)phys.flight_origin_live.sample.epoch=0u;
        if(mode==9u)s_tdma_runtime_owner_initialized=false;
        assert(!tdma_runtime_owner_get_origin_reference_epoch(&epoch));
        assert(epoch==sentinel && tick_calls==0u && try_calls==0u);
    }
    setup();epoch=sentinel;
    assert(!tdma_pio_spi_phys_origin_get_reference_epoch(NULL,&epoch));
    assert(!tdma_pio_spi_phys_origin_get_reference_epoch(&phys,NULL));
    assert(!tdma_runtime_owner_get_origin_reference_epoch(NULL));
    assert(epoch==sentinel && tick_calls==0u && try_calls==0u);
}

int main(void)
{
    test_raw_reference_provenance();
    test_raw_reference_retirement_races();
    test_reference_epoch_permission();
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
