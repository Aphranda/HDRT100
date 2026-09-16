"""Run actual Core0 harvest/pair/getter with bounded owner publications."""
import subprocess
import pytest

from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def local_prepare_executable(tmp_path_factory):
    physical = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    begin = physical.rfind("enum {", 0, physical.index("TDMA_EVENT_LIVE_RETAINED"))
    end = physical.index("} tdma_pio_spi_event_window_t;") + len("} tdma_pio_spi_event_window_t;")
    source = (ROOT / "components/vdc_dpll_manager/src/vdc_dpll_feedback_match.inc").read_text(encoding="utf-8")
    return compile_executable(tmp_path_factory.mktemp("local-prepare"), "local_prepare",
        FIXTURE.replace("PHYSICAL_TYPES", physical[begin:end]) + source + CASES,
        [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c"])


@pytest.mark.parametrize("case", ["matched", "chunks", "busy", "stop", "request_aba", "model",
    "reference_lifetime", "path", "delay", "age", "projection", "wrap", "getter", "paused",
    "model_busy", "path_busy", "remote_miss", "eviction", "epoch", "clock", "remote_bounds",
    "pending_clock_fresh", "pending_clock_expired", "pending_clock_wrap"])
def test_local_prepare_lifecycle(local_prepare_executable, case):
    result = subprocess.run([str(local_prepare_executable), case], capture_output=True, text=True, timeout=5)
    (local_prepare_executable.parent / (case + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


FIXTURE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "distributed_refmem.h"
#include "vdc_dpll_manager.h"
#include "vdc_local_follow.h"
#include "tdma_origin_plan.h"
#include "tdma_event_history.h"
PHYSICAL_TYPES
#undef assert
#define assert(c) do { if(!(c)) { fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#c);_Exit(99); } } while(0)
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static vdc_domain_context_t s_vdc_domain;
static tdma_ring_clock_snapshot_t ring;
static tdma_pio_spi_event_live_snapshot_t live;
static tdma_event_history_record_t records[40];
static unsigned available, oldest, window_calls, copied_records, model_calls;
static bool window_busy, model_busy, projection_ok=true, path_ok=true;
static unsigned pause_point;
static uint32_t request=2, session=123, now_ms=100, path_crc=77, delay_ns=321;
static uint32_t osal_now_ms=100;
static uint64_t raw_now;
static vdc_dpll_manager_committed_model_t model;
static distributed_refmem_vdc_feedback_rx_snapshot_t rx;
bool vdc_dpll_manager_try_local_follow_request(uint32_t *out) { *out=request;return true; }
bool vdc_dpll_manager_try_boundary_auto_enabled(bool *out) { *out=false;return true; }
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
static uint32_t board_uptime_ms(void) { return now_ms; }
uint32_t osal_tick_ms(void) { return osal_now_ms; }
bool tdma_ring_runtime_get_clock_snapshot(const tdma_ring_runtime_t *r,tdma_ring_clock_snapshot_t *out)
{ assert(r==&owner.ring_runtime);*out=ring;return true; }
bool tdma_runtime_owner_get_event_live_snapshot(tdma_pio_spi_event_live_snapshot_t *out) { *out=live;return true; }
tdma_event_window_result_t tdma_runtime_owner_copy_event_history_window(uint32_t epoch,uint64_t next,
    tdma_pio_spi_event_window_t *out) {
    ++window_calls;
    if(window_busy)return TDMA_EVENT_WINDOW_BUSY;
    assert(epoch==live.record.epoch && next<=available);
    memset(out,0,sizeof(*out));out->observer_epoch=epoch;out->arm_epoch=live.arm_epoch;
    out->tick_hz=live.tick_hz;out->timer1_enable_before=live.timer1_enable_before;
    out->timer1_enable_after=live.timer1_enable_after;
    if(next<oldest){out->lost_count=(uint32_t)(oldest-next);next=oldest;}
    out->count=(uint32_t)(available-next);if(out->count>4)out->count=4;
    out->available_end_ordinal=available;out->next_ordinal=next+out->count;
    for(unsigned i=0;i<out->count;i++)out->records[i]=records[next+i];
    copied_records+=out->count;
    if(pause_point==1){pause_point=0;request+=2;}
    return TDMA_EVENT_WINDOW_OK;
}
bool vdc_dpll_manager_project_feedback_event(uint32_t ses,uint32_t role,uint32_t epoch,uint32_t run,
    uint32_t local,uint32_t schedule,uint32_t hz,uint64_t lo,uint64_t hi,vdc_dpll_manager_projected_event_t *out) {
    assert(ses==session && role==9 && epoch==3 && run==4 && local==2 && schedule==0xabc && hz==250000000);
    if(!projection_ok || model_busy)return false;
    *out=(vdc_dpll_manager_projected_event_t){.output_ns_lo=lo*4,.output_ns_hi=hi*4,
        .model_token=model.token};return true;
}
bool vdc_dpll_manager_project_rate_reference(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,
    uint32_t f,uint32_t g,uint64_t h,uint64_t i,vdc_dpll_manager_projected_event_t *o)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)o;return false; }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ ++model_calls;if(model_busy)return false;*out=model;return true; }
bool vdc_dpll_manager_copy_local_follow_path(uint32_t local,uint32_t reference,uint32_t schedule,
    uint32_t *delay,uint32_t *crc)
{ assert(local==2 && reference==0 && schedule==0xabc);if(!path_ok)return false;*delay=delay_ns;*crc=path_crc;return true; }
static bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{ assert(hz==250000000);*out=raw_now;return true; }
bool distributed_refmem_copy_vdc_feedback_rx(uint32_t slot,distributed_refmem_vdc_feedback_rx_snapshot_t *out)
{ assert(slot==0);*out=rx;return true; }
static bool tdma_runtime_owner_get_origin_reference_epoch(uint32_t *out) { *out=0;return true; }
static bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out) { (void)out;return false; }
bool vdc_dpll_manager_get_refmem_snapshot(vdc_dpll_manager_refmem_snapshot_t *out)
{ memset(out,0,sizeof(*out));return false; }
'''

CASES = r'''
static void setup(void) {
    ring=(tdma_ring_clock_snapshot_t){.enabled=1,.data_enabled=1,.adapter_started=1,.node_count=4,
        .local_slot_id=2,.reference_slot_id=0,.config_seq=7,.applied_config_seq=7,.schedule_crc32=0xabc};
    s_vdc_domain.control.profile=(vdc_dpll_control_profile_t){.valid=1,.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER,
        .generation=9,.follow_master_slot_id=0};
    s_vdc_domain.schedule.local_slot_id=2;s_vdc_domain.schedule.schedule_crc32=0xabc;
    s_vdc_domain.clock.epoch_id=3;s_vdc_domain.clock.run_id=4;
    live=(tdma_pio_spi_event_live_snapshot_t){.arm_epoch=11,.record={.epoch=22},.tick_hz=250000000,
        .timer1_enable_before=10000,.timer1_enable_after=10005,
        .flags=TDMA_EVENT_LIVE_RETAINED|TDMA_EVENT_LIVE_ACTIVE|TDMA_EVENT_LIVE_ANCHOR_VALID};
    model=(vdc_dpll_manager_committed_model_t){.token=7,.session=session,.role_generation=9,
        .clock_epoch_id=3,.clock_run_id=4,.local_slot=2,.dco={.dco_update_seq=5,.tdma_schedule_crc32=0xabc}};
    rx=(distributed_refmem_vdc_feedback_rx_snapshot_t){.active=1,.retained=1,.source_slot=0,
        .ring_config_seq=7,.role_generation=9,.schedule_crc32=0xabc,.clock_epoch_id=3,.clock_run_id=4,
        .sample={.schema_version=REFMEM_VDC_FEEDBACK_MODEL_SCHEMA,.source_slot=0,.target_slot=2,
        .source_arm_epoch=111,.source_clock_epoch_id=30,.source_clock_run_id=40,.observer_epoch=222,.tick_hz=250000000,
        .model={.control_session=123,.model_token=70}}};
}
static void event(uint32_t sequence,uint64_t raw,uint64_t remote) {
    assert(available<40);
    records[available]=(tdma_event_history_record_t){.ordinal=available,.sequence=sequence,
        .rx_elapsed_cycles=raw,.tx_elapsed_cycles=raw+1};++available;
    live.record.sequence=sequence;live.record.ordinal=available-1;
    raw_now=raw+live.timer1_enable_after+100;
    now_ms=(uint32_t)(raw_now/250000u);
    osal_now_ms=now_ms;
    rx.sample.measurement_sequence=sequence;rx.sample.model.output_ns_lo=remote;
    rx.sample.model.output_ns_hi=remote+12;rx.last_rx_ms=now_ms;++rx.receive_count;
}
static void beat(void){vdc_dpll_manager_feedback_match_service();vdc_dpll_manager_feedback_prepare_core0();}
static vdc_local_follow_candidate_t candidate(void) {
    vdc_local_follow_candidate_t out;assert(vdc_dpll_manager_get_local_follow_candidate(&out));return out;
}
static void establish(void) {
    event(10,25000000,5000000000ULL);beat();assert(!candidate().active);
    event(11,300000275,6100000000ULL);beat();assert(candidate().active);
}
int main(int argc,char **argv) {
    assert(argc==2);setup();const char *c=argv[1];
    if(!strcmp(c,"pending_clock_fresh") || !strcmp(c,"pending_clock_expired") ||
       !strcmp(c,"pending_clock_wrap")) {
        for(unsigned i=0;i<7;i++)event(10+i,25000000+250000*i,5000000000ULL+1000000*i);
        /* The two host clocks deliberately differ, as on the four-board run.
         * First harvest sees seq10..13; retained reference seq16 is pending. */
        osal_now_ms=2000000u;rx.last_rx_ms=osal_now_ms-100u;
        if(!strcmp(c,"pending_clock_expired"))rx.last_rx_ms=osal_now_ms-121u;
        if(!strcmp(c,"pending_clock_wrap")){osal_now_ms=20u;rx.last_rx_ms=UINT32_MAX-79u;}
        beat();assert(window_calls==1 && copied_records==4);
        if(!strcmp(c,"pending_clock_expired")) {
            assert(s_feedback_matches[0].consumed==7 && s_feedback_matches[2].local.missed==1);
            beat();assert(s_feedback_matches[0].consumed==7 && !s_feedback_matches[0].peer.has_baseline);
        } else {
            assert(!s_feedback_matches[0].consumed);
            beat();assert(copied_records==7 && s_feedback_matches[0].consumed==7);
            assert(s_feedback_matches[0].peer.has_baseline && !candidate().active);
        }
    } else if(!strcmp(c,"chunks")) {
        for(unsigned i=0;i<7;i++)event(10+i,25000000+250000*i,5000000000ULL+1000000*i);
        beat();assert(window_calls==1 && copied_records==4 && !s_feedback_matches[0].consumed);
        beat();assert(window_calls==2 && copied_records==7 && s_feedback_matches[0].consumed==7);
        assert(s_feedback_match_insert_count==7 && s_feedback_match_cache.latest_published_version==14);
    } else if(!strcmp(c,"eviction")) {
        for(unsigned i=0;i<7;i++)event(10+i,25000000+250000*i,5000000000ULL+1000000*i);
        oldest=3;beat();assert(s_feedback_matches[2].local.missed==3 && copied_records==4);
    } else if(!strcmp(c,"busy")) {
        event(10,25000000,5000000000ULL);window_busy=true;beat();
        assert(!s_feedback_matches[2].local.next_ordinal && !s_feedback_match_insert_count);
        window_busy=false;beat();assert(s_feedback_match_insert_count==1);
    } else if(!strcmp(c,"projection")) {
        event(10,25000000,5000000000ULL);projection_ok=false;beat();
        assert(s_feedback_matches[2].local.next_ordinal==1 && s_feedback_match_reject_count==1);
        projection_ok=true;event(11,25250000,5001000000ULL);beat();assert(s_feedback_match_insert_count==1);
    } else if(!strcmp(c,"wrap")) {
        event(UINT32_MAX,25000000,5000000000ULL);beat();const uint32_t gen=s_feedback_match_cache.generation;
        event(0,25250000,5001000000ULL);beat();assert(s_feedback_match_cache.generation>gen);
        assert(s_feedback_match_cache.latest_sequence==0 && !candidate().active);
    } else if(!strcmp(c,"paused")) {
        event(10,25000000,5000000000ULL);beat();event(11,300000275,6100000000ULL);
        pause_point=1;beat();vdc_local_follow_candidate_t out;assert(!vdc_dpll_manager_get_local_follow_candidate(&out));
        const uint32_t token=s_feedback_match_owner_token;beat();assert(s_feedback_match_owner_token!=token);
    } else {
        establish();const vdc_local_follow_candidate_t saved=candidate();
        if(!strcmp(c,"matched")) {
            assert(saved.result.raw_ppb_lo>0 && saved.result.raw_ppb_hi>0);
            assert(saved.result.source.source_arm_epoch==11 && saved.reference.source_arm_epoch==111);
            assert(saved.result.pairs[1].reference_tx_lo==6100000000ULL+delay_ns);
            assert(saved.local_model_token==7 && saved.expected_dco_update_seq==5 && saved.reference_model_token==70);
            assert(saved.path_table_crc32==77 && saved.directed_delay_ns==321);
            const unsigned reads=model_calls;model_busy=true;assert(candidate().active && model_calls==reads);
            model_busy=false;beat();assert(candidate().serial==saved.serial);
            vdc_dpll_manager_feedback_match_status_t legacy;assert(!vdc_dpll_manager_get_feedback_match(0,&legacy));
        } else if(!strcmp(c,"model_busy") || !strcmp(c,"path_busy")) {
            event(12,575000550,7200000000ULL);const uint32_t consumed=s_feedback_matches[0].consumed;
            /* Harvest first; then simulate contention only at pairing. */
            rx.receive_count=consumed;beat();rx.receive_count=consumed+1;
            if(!strcmp(c,"model_busy"))model_busy=true;else path_ok=false;
            beat();assert(s_feedback_matches[0].consumed==consumed);
            model_busy=false;path_ok=true;beat();assert(candidate().active);
        } else if(!strcmp(c,"remote_miss")) {
            ++rx.sample.source_arm_epoch;rx.sample.measurement_sequence=8;++rx.receive_count;beat();
            assert(!s_feedback_matches[0].peer.has_baseline && !candidate().active);
            --rx.sample.source_arm_epoch;event(12,575000550,7200000000ULL);beat();assert(!candidate().active);
        } else if(!strcmp(c,"epoch")) {
            ++live.record.epoch;vdc_local_follow_candidate_t out;assert(!vdc_dpll_manager_get_local_follow_candidate(&out));
            beat();assert(s_feedback_match_owner_token!=saved.owner_token);
        } else if(!strcmp(c,"clock")) {
            live.flags&=~TDMA_EVENT_LIVE_ACTIVE;beat();assert(!s_feedback_match_owner_token);
        } else if(!strcmp(c,"remote_bounds")) {
            ++rx.sample.model.output_ns_hi;assert(!candidate().active);
        } else if(!strcmp(c,"stop")) {
            ring.enabled=0;beat();assert(!s_feedback_match_owner_token);
            vdc_local_follow_candidate_t out;assert(!vdc_dpll_manager_get_local_follow_candidate(&out));
        } else if(!strcmp(c,"request_aba")) {
            request+=4;vdc_local_follow_candidate_t out;assert(!vdc_dpll_manager_get_local_follow_candidate(&out));
            beat();assert(s_feedback_match_owner_token!=saved.owner_token && !candidate().active);
        } else if(!strcmp(c,"age")) {
            now_ms=saved.local_event_ms+VDC_LOCAL_FOLLOW_MAX_AGE_MS+1;assert(!candidate().active);
        } else if(!strcmp(c,"getter")) {
            vdc_local_follow_candidate_t out,sentinel;memset(&sentinel,0xaa,sizeof(sentinel));out=sentinel;
            s_feedback_matches[0].guard|=1;assert(!vdc_dpll_manager_get_local_follow_candidate(&out));
            assert(!memcmp(&out,&sentinel,sizeof(out)));
        } else {
            if(!strcmp(c,"model")){++model.token;++model.dco.dco_update_seq;}
            else if(!strcmp(c,"reference_lifetime")){++rx.sample.source_arm_epoch;}
            else if(!strcmp(c,"path"))++path_crc;
            else if(!strcmp(c,"delay"))++delay_ns;
            else assert(!"unknown case");
            event(12,575000550,7200000000ULL);beat();assert(!candidate().active);
            event(13,850000825,8300000000ULL);beat();assert(candidate().active);
        }
    }
    printf("local prepare %s passed\n",c);return 0;
}
'''
