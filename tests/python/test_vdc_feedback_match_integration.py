"""Execute Core1 matcher service with real cache, matching and public structs."""
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def matcher_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("feedback-match-integration")
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "distributed_refmem.h"
#include "vdc_dpll_manager.h"
#include "tdma_origin_plan.h"
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static vdc_domain_context_t s_vdc_domain;
static tdma_ring_clock_snapshot_t ring;
static tdma_origin_raw_reference_t reference;
static distributed_refmem_vdc_feedback_rx_snapshot_t feedback[PROJECT_NODE_CAPACITY];
static bool ring_available=true,reference_available=true,feedback_available=true,clock_available=true;
static unsigned ring_calls,reference_calls,feedback_calls,clock_calls;
static uint32_t requested_slot,*race_guard;
static uint64_t now_ticks;
bool tdma_ring_runtime_get_clock_snapshot(const tdma_ring_runtime_t *r,tdma_ring_clock_snapshot_t *out)
{ assert(r==&owner.ring_runtime);++ring_calls;*out=ring;return ring_available; }
bool vdc_dpll_manager_get_refmem_snapshot(vdc_dpll_manager_refmem_snapshot_t *out)
{ memset(out,0,sizeof(*out));out->control_profile=s_vdc_domain.control.profile;out->schedule=s_vdc_domain.schedule;
  out->clock_epoch_id=s_vdc_domain.clock.epoch_id;out->clock_run_id=s_vdc_domain.clock.run_id;return true; }
static bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out)
{ ++reference_calls;*out=reference;return reference_available; }
bool distributed_refmem_copy_vdc_feedback_rx(uint32_t slot,distributed_refmem_vdc_feedback_rx_snapshot_t *out)
{ ++feedback_calls;requested_slot=slot;assert(slot<PROJECT_NODE_CAPACITY);*out=feedback[slot];return feedback_available; }
static bool vdc_timestamp_clock_try_read_ticks64(uint32_t expected,uint64_t *out)
{ ++clock_calls;assert(expected==250000000u);*out=now_ticks;return clock_available; }
static void controlled_fence(int order)
{ __atomic_thread_fence(order);if(race_guard)*race_guard+=2; }
#define __atomic_thread_fence controlled_fence
''' + (ROOT / "components/vdc_dpll_manager/src/vdc_dpll_feedback_match.inc").read_text(encoding="utf-8") + r'''
#undef __atomic_thread_fence
static vdc_dpll_manager_feedback_match_status_t status(uint32_t slot)
{ vdc_dpll_manager_feedback_match_status_t out;assert(vdc_dpll_manager_get_feedback_match(slot,&out));return out; }
static void setup(void)
{
    ring=(tdma_ring_clock_snapshot_t){.enabled=1,.data_enabled=1,.adapter_started=1,
        .config_seq=7,.applied_config_seq=7,.node_count=4,.local_slot_id=0,.reference_slot_id=0,.schedule_crc32=0xabc};
    s_vdc_domain.control.profile=(vdc_dpll_control_profile_t){.valid=1,.mode=VDC_DPLL_CONTROL_MODE_MASTER,.generation=9};
    s_vdc_domain.schedule.local_slot_id=0;s_vdc_domain.schedule.schedule_crc32=0xabc;
    s_vdc_domain.clock.epoch_id=3;s_vdc_domain.clock.run_id=4;
    reference=(tdma_origin_raw_reference_t){.timer_lower=1000000,.timer_upper=1000000,
        .epoch=2,.sequence=10,.identity=0xabc001,.published_version=2,.tick_hz=250000000};
    now_ticks=reference.timer_upper+100;
    for(unsigned slot=1;slot<4;slot++) feedback[slot]=(distributed_refmem_vdc_feedback_rx_snapshot_t){
        .schema=1,.active=1,.retained=1,.source_slot=slot,.ring_config_seq=7,.role_generation=9,
        .schedule_crc32=0xabc,.clock_epoch_id=3,.clock_run_id=4,.receive_count=1,
        .sample={.source_slot=slot,.target_slot=0,.source_arm_epoch=77,.source_clock_epoch_id=21,
            .source_clock_run_id=22,.observer_epoch=23,.measurement_sequence=10,.tick_hz=250000000,
            .rx_elapsed_cycles=100000000}};
}
static void service(void)
{
    const unsigned old_ref=reference_calls,old_rx=feedback_calls;
    vdc_dpll_manager_feedback_match_service();
    assert(reference_calls-old_ref<=1 && feedback_calls-old_rx<=1);
}
static void roundtrip(void) { for(unsigned i=0;i<4;i++)service(); }
static void next(void)
{
    reference.sequence++;reference.published_version+=2;reference.timer_lower+=250000;reference.timer_upper+=250000;
    now_ticks=reference.timer_upper+100;
    for(unsigned s=1;s<4;s++){feedback[s].receive_count++;feedback[s].sample.measurement_sequence=reference.sequence;feedback[s].sample.rx_elapsed_cycles+=250010;}
}
static void establish(void)
{
    roundtrip();assert(status(1).baseline_count==1 && status(1).match_count==0);
    next();roundtrip();
    for(unsigned s=1;s<4;s++) {
        const vdc_dpll_manager_feedback_match_status_t out=status(s);
        assert(out.active && out.match_count==1 && out.result.has_pair);
        assert(out.result.raw_ppb_lo==40000 && out.result.raw_ppb_hi==40000);
        assert(out.result.pairs[0].measurement_sequence==10 && out.result.pairs[1].measurement_sequence==11);
        assert(out.result.source.source_clock_epoch_id==21 && out.clock_epoch_id==3);
    }
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *mode=argv[1];setup();
    if(!strcmp(mode,"bounded")) {
        service();assert(reference_calls==1 && feedback_calls==0);
        service();assert(reference_calls==2 && feedback_calls==1 && requested_slot==1);
        service();assert(reference_calls==3 && feedback_calls==2 && requested_slot==2);
        service();assert(reference_calls==4 && feedback_calls==3 && requested_slot==3);
        assert(s_feedback_match_insert_count==1);
        next();roundtrip();assert(s_feedback_match_insert_count==2 && status(1).match_count==1);
        roundtrip();assert(s_feedback_match_insert_count==2 && status(1).match_count==1);
    } else if(!strcmp(mode,"exact")) establish();
    else if(!strcmp(mode,"miss")) {
        roundtrip();next();for(unsigned s=1;s<4;s++)feedback[s].sample.measurement_sequence=reference.sequence+128;
        roundtrip();assert(status(1).miss_count==1 && !status(1).match_count && status(1).last_result==VDC_FEEDBACK_MATCH_NO_REFERENCE);
    } else if(!strcmp(mode,"no_cache")) {
        reference_available=false;roundtrip();assert(!feedback_calls && !status(1).receive_count);
        reference_available=true;roundtrip();assert(status(1).baseline_count==1);
    } else if(!strcmp(mode,"age")) {
        now_ticks=reference.timer_upper+30000000;roundtrip();assert(status(1).baseline_count==1);
        next();now_ticks=reference.timer_upper+30000001;roundtrip();
        assert(status(1).stale_count==1 && !status(1).match_count && status(1).last_result==VDC_FEEDBACK_MATCH_STALE);
    } else if(!strcmp(mode,"age_wide") || !strcmp(mode,"age_wide_boundary")) {
        reference.timer_upper+=100;
        const bool boundary=!strcmp(mode,"age_wide_boundary");
        now_ticks=reference.timer_lower+30000000+(boundary?0:1);
        assert(now_ticks>=reference.timer_upper && now_ticks-reference.timer_upper<30000000);
        roundtrip();
        if(boundary) assert(status(1).baseline_count==1 && !status(1).stale_count && status(1).last_age_ticks==30000000);
        else assert(!status(1).baseline_count && status(1).stale_count==1 && !status(1).active);
    } else if(!strcmp(mode,"future")) {
        now_ticks=reference.timer_upper-1;roundtrip();assert(status(1).stale_count==1 && !status(1).baseline_count);
    } else if(!strcmp(mode,"busy")) {
        establish();const vdc_dpll_manager_feedback_match_status_t saved=status(1);
        ring_available=false;roundtrip();
        vdc_dpll_manager_feedback_match_status_t out=status(1);assert(!out.active);out.active=saved.active;assert(!memcmp(&saved,&out,sizeof(out)));
        ring_available=true;next();feedback_available=false;roundtrip();out=status(1);assert(!memcmp(&saved,&out,sizeof(out)));
        feedback_available=true;clock_available=false;roundtrip();out=status(1);assert(!memcmp(&saved,&out,sizeof(out)));
        clock_available=true;roundtrip();assert(status(1).match_count==2);
    } else if(!strcmp(mode,"retire")) {
        establish();const vdc_dpll_manager_feedback_match_status_t before=status(1);const uint32_t generation=before.result.reference_generation;
        ring.enabled=0;service();assert(!status(1).active && status(1).match_count==before.match_count);
        ring.enabled=1;reference_available=false;roundtrip();assert(!status(1).active);
        reference_available=true;roundtrip();assert(!status(1).active && status(1).baseline_count==2);
        assert(s_feedback_match_cache.generation!=generation);
        next();roundtrip();assert(status(1).active && status(1).result.reference_generation!=generation);
    } else if(!strcmp(mode,"retire_hook")) {
        establish();const vdc_feedback_match_snapshot_t before=status(1).result;
        vdc_dpll_manager_feedback_match_retire();
        const vdc_dpll_manager_feedback_match_status_t after=status(1);
        assert(!after.active && !memcmp(&before,&after.result,sizeof(before)));
        reference_available=false;roundtrip();assert(!status(1).active);
    } else if(!strncmp(mode,"cancel_",7)) {
        establish();const vdc_feedback_match_snapshot_t before=status(1).result;
        if(!strcmp(mode,"cancel_data"))ring.data_enabled=0;
        else if(!strcmp(mode,"cancel_role"))s_vdc_domain.control.profile.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER;
        else if(!strcmp(mode,"cancel_config")){ring.config_seq++;ring.applied_config_seq++;}
        else if(!strcmp(mode,"cancel_generation"))s_vdc_domain.control.profile.generation++;
        else if(!strcmp(mode,"cancel_epoch"))s_vdc_domain.clock.epoch_id++;
        else if(!strcmp(mode,"cancel_run"))s_vdc_domain.clock.run_id++;
        else if(!strcmp(mode,"cancel_reference_epoch"))reference.epoch++;
        else assert(!"unknown cancellation");
        roundtrip();const vdc_dpll_manager_feedback_match_status_t after=status(1);
        assert(!after.active && !memcmp(&before,&after.result,sizeof(before)));
    } else if(!strcmp(mode,"wrong_binding")) {
        feedback[1].sample.target_slot=2;feedback[2].role_generation=8;feedback[3].sample.source_slot=1;
        roundtrip();for(unsigned s=1;s<4;s++)assert(!status(s).receive_count);
    } else if(!strncmp(mode,"namespace_",10)) {
        /* An inadmissible sample from another source lifetime still retires
         * the old baseline. Returning to the old lifetime must not revive it. */
        unsigned kind,stale,restore;
        assert(sscanf(mode,"namespace_%u_%u_%u",&kind,&stale,&restore)==3);
        establish();const vdc_dpll_manager_feedback_match_status_t before=status(1);
        const refmem_sync_vdc_feedback_record_t old_sample=feedback[1].sample;
        next();
        if(kind==0)feedback[1].sample.source_arm_epoch++;
        if(kind==1)feedback[1].sample.source_clock_epoch_id++;
        if(kind==2)feedback[1].sample.source_clock_run_id++;
        if(kind==3)feedback[1].sample.observer_epoch++;
        if(kind==4)feedback[1].sample.tick_hz=125000000;
        if(stale)now_ticks=reference.timer_upper+30000001;
        else feedback[1].sample.measurement_sequence=999;
        roundtrip();
        const vdc_dpll_manager_feedback_match_status_t invalid=status(1);
        assert(!invalid.active && invalid.match_count==before.match_count);
        assert(!memcmp(&invalid.result,&before.result,sizeof(before.result)));
        assert(!s_feedback_matches[1].peer.has_baseline);
        if(restore) {
            feedback[1].sample.source_arm_epoch=old_sample.source_arm_epoch;
            feedback[1].sample.source_clock_epoch_id=old_sample.source_clock_epoch_id;
            feedback[1].sample.source_clock_run_id=old_sample.source_clock_run_id;
            feedback[1].sample.observer_epoch=old_sample.observer_epoch;
            feedback[1].sample.tick_hz=old_sample.tick_hz;
        }
        next();roundtrip();
        assert(!status(1).active && status(1).baseline_count==before.baseline_count+1 && status(1).match_count==before.match_count);
        next();roundtrip();
        assert(status(1).active && status(1).match_count==before.match_count+1);
        assert(status(1).result.pairs[0].measurement_sequence==13 && status(1).result.pairs[1].measurement_sequence==14);
    } else if(!strncmp(mode,"getter_binding_",15)) {
        establish();const vdc_feedback_match_snapshot_t before=status(1).result;
        if(!strcmp(mode,"getter_binding_reference"))ring.reference_slot_id=1;
        else if(!strcmp(mode,"getter_binding_schedule_local"))s_vdc_domain.schedule.local_slot_id=1;
        else if(!strcmp(mode,"getter_binding_schedule_crc"))s_vdc_domain.schedule.schedule_crc32++;
        else if(!strcmp(mode,"getter_binding_data"))ring.data_enabled=0;
        else if(!strcmp(mode,"getter_binding_role"))s_vdc_domain.control.profile.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER;
        else if(!strcmp(mode,"getter_binding_nodes"))ring.node_count=PROJECT_NODE_CAPACITY+1;
        else if(!strcmp(mode,"getter_binding_config"))ring.config_seq++;
        else assert(!"unknown getter binding");
        const vdc_dpll_manager_feedback_match_status_t after=status(1);
        assert(!after.active && !memcmp(&before,&after.result,sizeof(before)));
    } else if(!strcmp(mode,"getter")) {
        establish();vdc_dpll_manager_feedback_match_status_t out,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));out=sentinel;
        assert(!vdc_dpll_manager_get_feedback_match(PROJECT_NODE_CAPACITY,&out));
        assert(!vdc_dpll_manager_get_feedback_match(1,NULL));
        s_feedback_matches[1].guard=1;assert(!vdc_dpll_manager_get_feedback_match(1,&out));assert(!memcmp(&out,&sentinel,sizeof(out)));
        s_feedback_matches[1].guard=2;race_guard=&s_feedback_matches[1].guard;
        assert(!vdc_dpll_manager_get_feedback_match(1,&out));assert(!memcmp(&out,&sentinel,sizeof(out)));
    } else assert(!"unknown mode");
    printf("feedback match service %s passed\n",mode);return 0;
}
'''
    return compile_executable(directory, "match_integration", harness,
                              [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c"])


@pytest.mark.parametrize("case", [
    "bounded", "exact", "miss", "no_cache", "age", "age_wide", "age_wide_boundary", "future", "busy", "retire", "retire_hook",
    "cancel_data", "cancel_role", "cancel_config", "cancel_generation", "cancel_epoch", "cancel_run",
    "cancel_reference_epoch", "wrong_binding", "getter",
    "getter_binding_reference", "getter_binding_schedule_local", "getter_binding_schedule_crc",
    "getter_binding_data", "getter_binding_role",
    "getter_binding_nodes", "getter_binding_config",
] + [f"namespace_{kind}_{stale}_{restore}" for kind in range(4) for stale in range(2) for restore in range(2)]
  + [f"namespace_4_{stale}_1" for stale in range(2)])
def test_match_integration(matcher_executable, case):
    result = subprocess.run([str(matcher_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
