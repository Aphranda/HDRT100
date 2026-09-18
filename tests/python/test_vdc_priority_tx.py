"""Run the real Core1 typed-offer provider and wire codec with bounded owner fakes."""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module", params=[(6, False), (6, True), (8, False), (8, True)])
def provider_executable(request, tmp_path_factory):
    capacity, short_enums = request.param
    directory = tmp_path_factory.mktemp(f"priority-tx-{capacity}-{int(short_enums)}")
    source = directory / "priority_tx.c"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = directory / ("priority_tx.exe" if os.name == "nt" else "priority_tx")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fstack-usage",
               f"-DPROJECT_NODE_CAPACITY={capacity}",
               *(["-fshort-enums"] if short_enums else []),
               *[f"-I{ROOT / 'components' / component / 'inc'}" for component in (
                   "tdma", "vdc_domain", "vdc_dpll_manager", "distributed_refmem",
                   "calibration_manager", "ota_manager")],
               f"-I{ROOT / 'components/vdc_dpll_manager/src'}",
               str(source), str(ROOT / "components/vdc_dpll_manager/src/vdc_priority_codec.c"),
               "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.json").write_text(json.dumps({
        "command": command, "returncode": result.returncode,
        "stdout": result.stdout, "stderr": result.stderr,
    }, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


CASES = [
    "new_event", "repeat_freeze", "model_transition", "model_unavailable", "stop",
    "unbound_stop", "generation", "exhaustion", "guard", "wrong_core", "nulls",
    "source_unavailable", "old_model", "stale", "future", "zero_width", "wide",
    "reverse", "max_width", "upper_limit", "model_race", "event_token_race",
    "epoch_race", "session_race", "config_race", "projection_failure", "epoch_read_failure",
    "sequence_wrap", "sequence_backward", "sequence_ambiguous", "sequence_zero",
    "source_identity", "source_version", "source_interval", "source_epoch", "source_clock",
    "binding_session", "binding_role", "binding_clock_epoch", "binding_clock_run",
    "binding_model_slot", "config_enabled", "config_nodes", "config_local", "config_reference",
    "config_schedule", "config_profile", "config_operating", "config_period", "config_geometry",
    "config_owner", "config_baud", "config_flags", "config_loop", "config_tolerance",
    "config_timeout", "config_tx_dma", "config_rx_dma", "config_up", "config_down",
    "invalid_config", "unavailable_session", "target_last_slot",
]


@pytest.mark.parametrize("case", CASES)
def test_priority_tx_provider(provider_executable, case):
    command = [str(provider_executable), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=5)
    (provider_executable.parent / f"run-{case}.json").write_text(json.dumps({
        "command": command, "returncode": result.returncode,
        "stdout": result.stdout, "stderr": result.stderr,
    }, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "priority TX provider passed" in result.stdout


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "vdc_priority_tx.h"
#include "vdc_priority_codec.h"
#include "tdma_origin_plan.h"
static unsigned core;
static unsigned get_core_num(void) { return core; }
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static uint32_t session=17u, project_calls, model_calls, action;
static bool stopped=true, model_ok=true, source_ok=true, clock_ok=true;
static bool projection_ok=true, epoch_ok=true;
static uint64_t now=1200u, projected_lo=100000u, projected_hi=100080u;
static vdc_dpll_manager_committed_model_t committed;
static tdma_origin_raw_reference_t raw;
static tdma_ring_runtime_config_t config;
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool tdma_service_update_stopped_metadata(tdma_service_service_t *s,
    bool (*publish)(void *), void *context)
{ assert(s==&owner); return stopped && publish(context); }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ ++model_calls; if(!model_ok) return false; *out=committed; return true; }
bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out)
{ if(!source_ok) return false; *out=raw; return true; }
bool tdma_runtime_owner_get_origin_reference_epoch(uint32_t *out)
{ if(!epoch_ok) return false; *out=raw.epoch; return true; }
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{ assert(hz==raw.tick_hz); if(!clock_ok) return false; *out=now; return true; }
bool vdc_dpll_manager_project_timer1_feedback_event(uint32_t ses,uint32_t role,uint32_t epoch,
    uint32_t run,uint32_t local,uint32_t schedule,uint32_t hz,uint64_t lo,uint64_t hi,
    vdc_dpll_manager_timer1_projection_t *out)
{
    ++project_calls;
    assert(ses==session && role==committed.role_generation && epoch==committed.clock_epoch_id);
    assert(run==committed.clock_run_id && local==config.local_slot_id && schedule==config.schedule_crc32);
    assert(hz==raw.tick_hz && lo==raw.timer_lower && hi==raw.timer_upper);
    assert(lo>=committed.valid_from_raw);
    if(!projection_ok) return false;
    /* Provider policy fixture only; real mapping/projector/Domain are linked
     * independently by test_vdc_priority_mapping. */
    memset(out,0,sizeof(*out));out->model=committed;
    out->output_lo=projected_lo;out->output_hi=projected_hi;
    if(action==1u) ++committed.token;
    if(action==2u) ++out->model.token;
    if(action==3u) ++raw.epoch;
    if(action==4u) ++session;
    if(action==5u) ++config.owner_config_seq;
    return true;
}
#include "vdc_priority_tx.inc"

static uint16_t le16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8u); }
static vdc_priority_tx_snapshot_t snapshot(void) {
    vdc_priority_tx_snapshot_t out;
    assert(vdc_dpll_manager_get_priority_tx(&out));
    return out;
}
static bool request_generation(uint32_t generation) {
    const unsigned saved=core; core=0u;
    const bool result=vdc_dpll_manager_set_priority_sync(generation);
    core=saved; return result;
}
static void init(void) {
    config=(tdma_ring_runtime_config_t){.enabled=1u,.node_count=PROJECT_NODE_CAPACITY,
        .local_slot_id=0u,.reference_slot_id=0u,.up_group_id=1u,.down_group_id=2u,
        .ring_profile_crc32=0x456u,.schedule_crc32=0x123u,.operating_profile_crc32=0x789u,
        .cycle_period_ns=1500000u,.geometry_generation=12u,.owner_config_seq=23u};
    committed=(vdc_dpll_manager_committed_model_t){.token=8u,.session=session,
        .role_generation=3u,.clock_epoch_id=4u,.clock_run_id=5u,.local_slot=0u,
        .valid_from_raw=900u,.dco={.valid=1u,.nominal_period_ns=1500000u,
            .tdma_schedule_crc32=0x123u}};
    raw=(tdma_origin_raw_reference_t){.epoch=29u,.sequence=47u,.identity=0x2468u,
        .published_version=90u,.tick_hz=250000000u,.timer_lower=1000u,.timer_upper=1020u};
    assert(request_generation(101u));
    core=1u;
}
static void next_event(void) {
    ++raw.sequence; raw.identity+=9u; raw.published_version+=2u;
    raw.timer_lower+=100u; raw.timer_upper+=100u; now+=100u;
}
static void expect_empty(uint32_t reason, bool retired) {
    uint8_t mailbox[32], saved[32]; memset(mailbox,0xa5,sizeof(mailbox)); memcpy(saved,mailbox,32u);
    assert(vdc_priority_tx_core1(&config,mailbox)==TDMA_PRIORITY_TX_EMPTY);
    assert(!memcmp(mailbox,saved,32u));
    const vdc_priority_tx_snapshot_t s=snapshot();
    assert(s.last_reject==reason && (s.retired!=0u)==retired);
}
static void offer(uint8_t *mailbox) {
    assert(vdc_priority_tx_core1(&config,mailbox)==TDMA_PRIORITY_TX_READY);
    assert(le16(mailbox)==TDMA_FLIGHT_MAILBOX_MAGIC && mailbox[2]==TDMA_FLIGHT_MAILBOX_VERSION);
    assert(mailbox[3]==TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS);
    assert(mailbox[4]==config.local_slot_id && mailbox[5]==
        (uint8_t)(((1u<<config.node_count)-1u)&~(1u<<config.local_slot_id)));
    assert(le16(mailbox+6u)==(uint16_t)raw.sequence);
    assert(le16(mailbox+30u)==tdma_process_image_crc16_ccitt(mailbox,30u));
    vdc_priority_codec_record_t decoded;
    assert(vdc_priority_codec_decode(mailbox+8u,&decoded));
    assert(decoded.binding_generation==vdc_dpll_manager_priority_sync_generation());
    assert(decoded.event_sequence==raw.sequence && decoded.flags==0u);
}

int main(int argc,char **argv) {
    assert(argc==2); const char *c=argv[1]; init(); uint8_t mailbox[32], first[32];
    if(!strcmp(c,"new_event")) {
        offer(first); next_event(); projected_lo+=99u; projected_hi+=199u; offer(mailbox);
        assert(memcmp(first,mailbox,32u) && project_calls==2u && snapshot().encoded==2u);
    } else if(!strcmp(c,"repeat_freeze")) {
        offer(first); projected_lo+=2000u; projected_hi+=4000u; projection_ok=false;
        offer(mailbox); assert(!memcmp(first,mailbox,32u) && project_calls==1u);
        assert(snapshot().encoded==1u && snapshot().repeated==1u);
    } else if(!strcmp(c,"model_transition")) {
        offer(first); ++committed.token; committed.valid_from_raw=1050u;
        expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false); assert(project_calls==1u);
        next_event(); offer(mailbox); assert(snapshot().generation==101u && snapshot().encoded==2u);
    } else if(!strcmp(c,"model_unavailable")) {
        offer(first); model_ok=false; expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false);
        model_ok=true; committed.token=0u; expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false);
        committed.token=8u; offer(mailbox); assert(!memcmp(first,mailbox,32u));
    } else if(!strcmp(c,"stop") || !strcmp(c,"unbound_stop")) {
        const bool bound=!strcmp(c,"stop"); if(bound) offer(first);
        assert(vdc_priority_tx_core1(NULL,NULL)==TDMA_PRIORITY_TX_EMPTY);
        assert(vdc_dpll_manager_priority_sync_generation()==101u);
        assert(snapshot().retired && !snapshot().active);
        if(bound) assert(snapshot().have_offer && !memcmp(snapshot().mailbox,first,32u));
        expect_empty(VDC_PRIORITY_TX_REJECT_RETIRED,true);
        assert(!request_generation(101u)); assert(request_generation(102u));
        ++raw.epoch; offer(mailbox); assert(snapshot().generation==102u && !snapshot().retired);
    } else if(!strcmp(c,"generation")) {
        stopped=false; assert(!request_generation(102u)); assert(!request_generation(0u));
        stopped=true; assert(!request_generation(101u)); assert(!request_generation(100u));
        assert(request_generation(0u)); memset(mailbox,0xa5,32u); memcpy(first,mailbox,32u);
        assert(vdc_priority_tx_core1(&config,mailbox)==TDMA_PRIORITY_TX_DISABLED);
        assert(!memcmp(first,mailbox,32u)); session=0u; assert(!request_generation(102u));
        session=17u; assert(request_generation(102u)); offer(mailbox);
    } else if(!strcmp(c,"exhaustion")) {
        assert(request_generation(UINT32_MAX)); offer(mailbox); assert(request_generation(0u));
        assert(!request_generation(UINT32_MAX)); assert(!request_generation(1u));
    } else if(!strcmp(c,"guard")) {
        offer(first); vdc_priority_tx_snapshot_t out, sentinel;
        memset(&sentinel,0xa5,sizeof(sentinel)); out=sentinel;
        ++s_priority_tx_guard; assert(!vdc_dpll_manager_get_priority_tx(&out));
        assert(!memcmp(&out,&sentinel,sizeof(out))); --s_priority_tx_guard;
        assert(vdc_dpll_manager_get_priority_tx(&out));
        s_priority_tx_guard=UINT32_MAX-1u; offer(mailbox); assert(s_priority_tx_guard==0u);
    } else if(!strcmp(c,"wrong_core")) {
        assert(!vdc_dpll_manager_set_priority_sync(102u)); offer(first);
        const uint32_t calls=snapshot().calls; core=0u;
        assert(vdc_priority_tx_core1(NULL,NULL)==TDMA_PRIORITY_TX_EMPTY);
        assert(!snapshot().retired && snapshot().calls==calls);
    } else if(!strcmp(c,"nulls")) {
        assert(!vdc_dpll_manager_get_priority_tx(NULL));
        assert(vdc_priority_tx_core1(&config,NULL)==TDMA_PRIORITY_TX_EMPTY);
        s_vdc_tdma_service=NULL; assert(!request_generation(102u));
    } else if(!strcmp(c,"source_unavailable")) {
        source_ok=false; expect_empty(VDC_PRIORITY_TX_REJECT_SOURCE,false);
        source_ok=true; raw.published_version|=1u; expect_empty(VDC_PRIORITY_TX_REJECT_SOURCE,false);
        --raw.published_version; offer(mailbox);
    } else if(!strcmp(c,"old_model")) {
        committed.valid_from_raw=raw.timer_lower+1u;
        expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false); assert(!project_calls);
    } else if(!strcmp(c,"stale")) {
        offer(first); now=raw.timer_lower+(uint64_t)raw.tick_hz*2u+1u;
        expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false); assert(project_calls==1u);
    } else if(!strcmp(c,"future")) {
        now=raw.timer_upper-1u; expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false);
        now=1200u; clock_ok=false; expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false);
    } else if(!strcmp(c,"zero_width") || !strcmp(c,"wide") || !strcmp(c,"reverse")) {
        projected_hi=!strcmp(c,"zero_width") ? projected_lo :
            !strcmp(c,"wide") ? projected_lo+(uint64_t)UINT32_MAX+1u : projected_lo-1u;
        expect_empty(VDC_PRIORITY_TX_REJECT_WIDTH,false); assert(!snapshot().have_offer);
    } else if(!strcmp(c,"max_width") || !strcmp(c,"upper_limit")) {
        if(!strcmp(c,"max_width")) { projected_lo=0u; projected_hi=UINT32_MAX; }
        else { projected_lo=UINT64_MAX-1u; projected_hi=UINT64_MAX; }
        offer(mailbox); assert(snapshot().event_time_lower==projected_lo);
        assert(snapshot().uncertainty_width==projected_hi-projected_lo);
    } else if(!strcmp(c,"model_race") || !strcmp(c,"event_token_race")) {
        action=!strcmp(c,"model_race") ? 1u : 2u;
        expect_empty(VDC_PRIORITY_TX_REJECT_CHANGED,false); assert(!snapshot().have_offer);
    } else if(!strcmp(c,"epoch_race") || !strcmp(c,"session_race") || !strcmp(c,"config_race")) {
        action=!strcmp(c,"epoch_race") ? 3u : !strcmp(c,"session_race") ? 4u : 5u;
        expect_empty(VDC_PRIORITY_TX_REJECT_BINDING,true); assert(!snapshot().have_offer);
    } else if(!strcmp(c,"projection_failure")) {
        projection_ok=false; expect_empty(VDC_PRIORITY_TX_REJECT_PROJECTION,false);
    } else if(!strcmp(c,"epoch_read_failure")) {
        epoch_ok=false; expect_empty(VDC_PRIORITY_TX_REJECT_CHANGED,false);
    } else if(!strcmp(c,"sequence_wrap")) {
        raw.sequence=UINT32_MAX; offer(first); next_event();
        expect_empty(VDC_PRIORITY_TX_REJECT_SEQUENCE,true);
    } else if(!strcmp(c,"sequence_backward") || !strcmp(c,"sequence_ambiguous")) {
        offer(first); raw.sequence+=!strcmp(c,"sequence_backward") ? UINT32_MAX : 0x80000000u;
        expect_empty(VDC_PRIORITY_TX_REJECT_SEQUENCE,true);
    } else if(!strcmp(c,"sequence_zero")) {
        raw.sequence=0u; offer(first); next_event(); offer(mailbox);
    } else if(!strcmp(c,"source_identity") || !strcmp(c,"source_version") || !strcmp(c,"source_interval")) {
        offer(first);
        if(!strcmp(c,"source_identity")) ++raw.identity;
        if(!strcmp(c,"source_version")) raw.published_version+=2u;
        if(!strcmp(c,"source_interval")) ++raw.timer_upper;
        expect_empty(VDC_PRIORITY_TX_REJECT_SEQUENCE,true);
    } else if(!strcmp(c,"source_epoch") || !strcmp(c,"source_clock")) {
        offer(first); if(!strcmp(c,"source_epoch")) ++raw.epoch; else ++raw.tick_hz;
        expect_empty(VDC_PRIORITY_TX_REJECT_BINDING,true);
    } else if(!strncmp(c,"binding_",8u)) {
        offer(first);
        if(!strcmp(c,"binding_session")) ++session;
        else if(!strcmp(c,"binding_role")) ++committed.role_generation;
        else if(!strcmp(c,"binding_clock_epoch")) ++committed.clock_epoch_id;
        else if(!strcmp(c,"binding_clock_run")) ++committed.clock_run_id;
        else if(!strcmp(c,"binding_model_slot")) ++committed.local_slot;
        else assert(!"unknown binding case");
        expect_empty(VDC_PRIORITY_TX_REJECT_BINDING,true);
    } else if(!strncmp(c,"config_",7u)) {
        offer(first);
        if(!strcmp(c,"config_enabled")) config.enabled=0u;
        else if(!strcmp(c,"config_nodes")) --config.node_count;
        else if(!strcmp(c,"config_local")) ++config.local_slot_id;
        else if(!strcmp(c,"config_reference")) ++config.reference_slot_id;
        else if(!strcmp(c,"config_schedule")) ++config.schedule_crc32;
        else if(!strcmp(c,"config_profile")) ++config.ring_profile_crc32;
        else if(!strcmp(c,"config_operating")) ++config.operating_profile_crc32;
        else if(!strcmp(c,"config_period")) ++config.cycle_period_ns;
        else if(!strcmp(c,"config_geometry")) ++config.geometry_generation;
        else if(!strcmp(c,"config_owner")) ++config.owner_config_seq;
        else if(!strcmp(c,"config_baud")) ++config.baud_hz;
        else if(!strcmp(c,"config_flags")) ++config.flags;
        else if(!strcmp(c,"config_loop")) ++config.loop_delay_ns;
        else if(!strcmp(c,"config_tolerance")) ++config.loop_delay_tolerance_ns;
        else if(!strcmp(c,"config_timeout")) ++config.feedback_timeout_ns;
        else if(!strcmp(c,"config_tx_dma")) ++config.tx_dma_channel_id;
        else if(!strcmp(c,"config_rx_dma")) ++config.rx_dma_channel_id;
        else if(!strcmp(c,"config_up")) ++config.up_group_id;
        else if(!strcmp(c,"config_down")) ++config.down_group_id;
        else assert(!"unknown config case");
        expect_empty(priority_tx_config_valid(&config) ? VDC_PRIORITY_TX_REJECT_BINDING :
            VDC_PRIORITY_TX_REJECT_CONFIG,true);
    } else if(!strcmp(c,"invalid_config")) {
        config.node_count=PROJECT_NODE_CAPACITY+1u; expect_empty(VDC_PRIORITY_TX_REJECT_CONFIG,false);
        config.node_count=PROJECT_NODE_CAPACITY; offer(mailbox);
    } else if(!strcmp(c,"unavailable_session")) {
        session=0u; expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false); assert(!project_calls);
    } else if(!strcmp(c,"target_last_slot")) {
        config.local_slot_id=config.reference_slot_id=PROJECT_NODE_CAPACITY-1u;
        committed.local_slot=config.local_slot_id; offer(mailbox);
    } else assert(!"unknown scenario");
    printf("priority TX provider passed: %s; work=%zu snapshot=%zu\n",c,
        sizeof(s_priority_tx_work),sizeof(vdc_priority_tx_snapshot_t));
    return 0;
}
'''
