"""Compile the production matcher with owner-boundary fakes and TIMER1 projection.

The policy matrix isolates external owner races. Additional integration cases
link the production coordinate conversion and Domain forward projection; owner
publications and raw observations are controlled inputs. No hardware
FIFO or physical timestamp accuracy is qualified by these host tests.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
CASES = ["match", "signed", "duplicate", "next", "full_sequence", "out_of_order", "generation",
    "unbound_stop", "bound_stop", "rx_retired", "fresh_request", "disable", "wrong_core",
    "pending_retry", "busy_retry", "evicted", "superseded", "exact_retired", "exact_bad",
    "exact_wrong_sequence", "exact_wrong_anchor", "raw_overflow", "old_model", "projection_failure",
    "remote_overflow", "delay_overflow", "residual_overflow", "difference_limits", "reverse_projection",
    "model_revision", "remote_revision", "model_race", "model_field_race", "event_token_race",
    "session_race", "ring_race", "arm_race", "observer_race", "rx_epoch_race", "path_race",
    "model_busy", "live_busy", "rx_busy", "role_master", "wrong_source", "wrong_target",
    "path_wrong_direction", "path_missing", "binding_config", "binding_profile", "binding_session",
    "binding_role", "binding_clock", "binding_rx_epoch", "binding_arm", "binding_observer",
    "binding_tick", "binding_path", "guard", "saturation", "request_session", "unserviced_stop_arm",
    "remote_generation_race", "remote_source_race", "remote_target_race", "event_applied_race"]


@pytest.fixture(scope="module", params=[(6, False), (6, True), (8, False), (8, True)])
def match_executable(request, tmp_path_factory):
    capacity, short_enums = request.param
    directory = tmp_path_factory.mktemp(f"priority-match-{capacity}-{int(short_enums)}")
    source = directory / "priority_match.c"
    # Reuse the actual public event types without importing unrelated board
    # MMIO resource declarations into this host-only owner-boundary harness.
    physical_header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    begin = physical_header.index("enum {\n    TDMA_EVENT_LIVE_RETAINED")
    end = physical_header.index("} tdma_pio_spi_event_exact_t;", begin) + len("} tdma_pio_spi_event_exact_t;")
    source.write_text(HARNESS.replace('#include "tdma_pio_spi_phys.h"',
        '#include "tdma_event_history.h"\n' + physical_header[begin:end]), encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = directory / ("priority_match.exe" if os.name == "nt" else "priority_match")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fstack-usage",
        f"-DPROJECT_NODE_CAPACITY={capacity}", *(["-fshort-enums"] if short_enums else []),
        *[f"-I{ROOT / 'components' / component / 'inc'}" for component in (
            "tdma", "vdc_domain", "vdc_dpll_manager", "distributed_refmem", "calibration_manager", "ota_manager")],
        f"-I{ROOT / 'components/vdc_dpll_manager/src'}", str(source), "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.json").write_text(json.dumps(dict(command=command, returncode=result.returncode,
        stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize("case", CASES)
def test_priority_match(match_executable, case):
    command = [str(match_executable), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=5)
    (match_executable.parent / f"run-{case}.json").write_text(json.dumps(dict(command=command,
        returncode=result.returncode, stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


MAPPING_CASES = ["direct", "duplicate", "model_duplicate", "final_model_reject_retry",
    "final_field_reject_retry", "final_arithmetic_reject_retry", "model_new_event",
    "stop", "disable", "observer_retire", "exact_retire", "new_generation",
    "timer0_independent", "future_event", "final_binding_retire", "many_events",
    "long_gap", "projection_unavailable_retry"]


@pytest.fixture(scope="module")
def real_match_mapping_executable(tmp_path_factory):
    from test_vdc_command_owner import compile_executable
    from test_vdc_priority_follow import domain_sources

    directory = tmp_path_factory.mktemp("priority-match-real-mapping")
    physical = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    begin = physical.index("enum {\n    TDMA_EVENT_LIVE_RETAINED")
    end = physical.index("} tdma_pio_spi_event_exact_t;", begin) + len("} tdma_pio_spi_event_exact_t;")
    prelude = HARNESS.split("int main(int argc,char **argv)")[0].replace(
        '#include "tdma_pio_spi_phys.h"', '#include "tdma_event_history.h"\n' + physical[begin:end])
    prelude = prelude.replace('#include "vdc_priority_match.inc"',
        (ROOT / "components/vdc_dpll_manager/src/vdc_priority_match.inc").read_text(encoding="utf-8"))
    # Rename only external-owner fakes which share symbols with separately
    # linked Domain/TDMA code. The matcher, mapping and forward arithmetic
    # remain unchanged production code, not rewritten test formulas.
    prelude = ('#define MATCH_REAL_MAPPING 1\n'
        '#define tdma_service_update_stopped_metadata match_test_stopped_metadata\n'
        '#define vdc_domain_active_observation_path_delay_lookup match_test_path_lookup\n' + prelude)
    return compile_executable(directory, "priority_match_mapping", prelude + REAL_MAPPING_MAIN,
                              domain_sources())


@pytest.mark.parametrize("case", MAPPING_CASES)
def test_real_match_mapping_commits_only_final_success(real_match_mapping_executable, case):
    command = [str(real_match_mapping_executable), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    (real_match_mapping_executable.parent / f"run-{case}.json").write_text(json.dumps(dict(
        command=command, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr),
        indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "vdc_priority_match.h"
#include "vdc_priority_rx.h"
#include "tdma_pio_spi_phys.h"
static unsigned core, model_calls, project_calls, exact_calls, action;
static unsigned get_core_num(void) { return core; }
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static vdc_domain_context_t s_vdc_domain;
static tdma_ring_clock_snapshot_t ring;
static tdma_pio_spi_event_live_snapshot_t live;
static tdma_pio_spi_event_exact_t exact;
static vdc_priority_rx_snapshot_t rx;
static vdc_dpll_manager_committed_model_t model;
static uint32_t session=17u;
#ifndef MATCH_REAL_MAPPING
static uint64_t output_lo=10100u, output_hi=10120u;
#endif
static bool stopped=true, model_ok=true, live_ok=true, rx_ok=true, path_ok=true, project_ok=true;
#ifdef MATCH_REAL_MAPPING
static vdc_timestamp_clock_bridge_t mapping_bridge;
#endif
static tdma_event_exact_result_t exact_result=TDMA_EVENT_EXACT_OK;
static const uint32_t *collision_word;
static uint32_t *collision_guard;
static uint32_t read_atomic(const uint32_t *p, int order) {
    uint32_t n=__atomic_load_n(p,order);
    if(p==collision_word) { __atomic_add_fetch(collision_guard,2u,__ATOMIC_RELEASE); collision_word=NULL; }
    return n;
}
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool tdma_service_update_stopped_metadata(tdma_service_service_t *s, bool (*fn)(void *), void *context)
{ assert(s==&owner); return stopped && fn(context); }
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{ *out=ring; return true; }
bool tdma_runtime_owner_get_event_live_snapshot(tdma_pio_spi_event_live_snapshot_t *out)
{ if(!live_ok)return false; *out=live; return true; }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ ++model_calls; if(!model_ok)return false; *out=model; return true; }
bool vdc_priority_rx_copy_live(vdc_priority_rx_snapshot_t *out)
{ if(!rx_ok || !rx.active || !rx.have_record)return false; *out=rx; return true; }
bool vdc_domain_active_observation_path_delay_lookup(const vdc_path_delay_table_t *table,
    uint32_t source,uint32_t reference,vdc_path_delay_entry_t *out)
{
    assert(table==&s_vdc_domain.path_delay);
    assert(source==ring.reference_slot_id && reference==ring.local_slot_id); /* MUST transpose. */
    if(!path_ok)return false;
    *out=(vdc_path_delay_entry_t){.valid=1u,.cal_crc32=table->table_crc32,.delay_ns=table->entries[0].delay_ns};
    return true;
}
tdma_event_exact_result_t tdma_runtime_owner_copy_event_history_exact(uint64_t arm,
    uint32_t observer,uint32_t sequence,tdma_pio_spi_event_exact_t *out)
{
    ++exact_calls; assert(arm==live.arm_epoch && observer==live.record.epoch);
    assert(sequence==rx.typed_record.event_sequence);
    if(exact_result!=TDMA_EVENT_EXACT_OK)return exact_result;
    *out=exact; return TDMA_EVENT_EXACT_OK;
}
bool vdc_dpll_manager_project_timer1_feedback_event(uint32_t ses,uint32_t role,uint32_t epoch,
    uint32_t run,uint32_t local_slot,uint32_t schedule,uint32_t tick,uint64_t lo,uint64_t hi,
    vdc_dpll_manager_timer1_projection_t *out)
{
    ++project_calls;
    assert(ses==session && role==model.role_generation && epoch==model.clock_epoch_id);
    assert(run==model.clock_run_id && local_slot==ring.local_slot_id && schedule==ring.schedule_crc32);
    assert(tick==live.tick_hz && lo==exact.timer1_enable_before+exact.record.rx_elapsed_cycles);
    assert(hi==exact.timer1_enable_after+exact.record.rx_elapsed_cycles);
    if(!project_ok)return false;
    memset(out,0,sizeof(*out));out->model=model;
#ifdef MATCH_REAL_MAPPING
    {
        if(hi>mapping_bridge.raw_after || mapping_bridge.raw_after-lo>(uint64_t)tick*2u ||
            !vdc_timer1_interval_to_ns(tick,lo,hi,&out->local_lo,&out->local_hi) ||
            !vdc_domain_dco_local_to_output_ns(&model.dco,out->local_lo,&out->output_lo) ||
            !vdc_domain_dco_local_to_output_ns(&model.dco,out->local_hi,&out->output_hi))return false;
        out->raw_now=mapping_bridge.raw_after;
    }
#else
    { out->output_lo=output_lo;out->output_hi=output_hi; }
#endif
    if(action==1u)++model.token;
    if(action==2u)++model.dco.period_adjust_ppb;
    if(action==3u)++out->model.token;
    if(action==4u)++session;
    if(action==5u)++ring.config_seq;
    if(action==6u)++live.arm_epoch;
    if(action==7u)++live.record.epoch;
    if(action==8u)++rx.epoch;
    if(action==9u)++s_vdc_domain.path_delay.table_crc32;
    if(action==10u)++rx.typed_record.binding_generation;
    if(action==11u)++rx.source_slot;
    if(action==12u)rx.target_mask=1u;
    if(action==13u)++out->model.applied_command_seq;
    return true;
}
#define __atomic_load_n read_atomic
#include "vdc_priority_match.inc"
#undef __atomic_load_n
static bool request(uint32_t gen)
{ const unsigned saved=core; core=0u; bool ok=vdc_dpll_manager_set_priority_match(gen); core=saved; return ok; }
static vdc_priority_match_snapshot_t sample(void)
{ vdc_priority_match_core1(); vdc_priority_match_snapshot_t out; assert(vdc_dpll_manager_get_priority_match(&out)); return out; }
static void next(void)
{ ++rx.typed_record.event_sequence; ++rx.carrier_sequence; ++exact.record.sequence; }
static void expect(uint32_t reason,bool retired)
{ vdc_priority_match_snapshot_t out=sample(); assert(out.last_reason==reason && !!out.retired==retired); }
static void init(void)
{
    ring=(tdma_ring_clock_snapshot_t){.enabled=1u,.adapter_started=1u,.config_seq=9u,.applied_config_seq=9u,
        .node_count=4u,.local_slot_id=1u,.reference_slot_id=0u,.schedule_crc32=0x123u};
    owner.ring_runtime.ring_profile_crc32=0x456u;
    s_vdc_domain.control.profile=(vdc_dpll_control_profile_t){.valid=1u,.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER,
        .follow_master_slot_id=0u,.generation=3u};
    s_vdc_domain.path_delay.schedule_crc32=0x123u; s_vdc_domain.path_delay.table_crc32=0x678u;
    s_vdc_domain.path_delay.entry_count=4u;
    for(unsigned i=0;i<4;++i)s_vdc_domain.path_delay.entries[i]=(vdc_path_delay_entry_t){.valid=1u,
        .direction=VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE,.delay_ns=82u};
    model=(vdc_dpll_manager_committed_model_t){.token=8u,.session=session,.role_generation=3u,
        .clock_epoch_id=4u,.clock_run_id=5u,.local_slot=1u,.valid_from_raw=900u,
        .dco={.valid=1u,.nominal_period_ns=1500000u,.tdma_schedule_crc32=0x123u}};
    live=(tdma_pio_spi_event_live_snapshot_t){.arm_epoch=UINT32_MAX+11ull,.tick_hz=250000000u,
        .record={.epoch=29u,.sequence=9999u},.flags=7u,.timer1_enable_before=1000u,.timer1_enable_after=1010u};
    exact=(tdma_pio_spi_event_exact_t){.arm_epoch=live.arm_epoch,.tick_hz=live.tick_hz,
        .observer_epoch=29u,.flags=7u,.timer1_enable_before=1000u,.timer1_enable_after=1010u,
        .record={.sequence=0u,.rx_elapsed_cycles=200u,.tx_elapsed_cycles=8000u}};
    rx=(vdc_priority_rx_snapshot_t){.schema=1u,.active=1u,.epoch=5u,.have_record=1u,
        .source_slot=0u,.target_mask=14u,.carrier_sequence=100u,
        .typed_record={.binding_generation=101u,.event_sequence=0u,.event_time_lower=10000u,.uncertainty_width=7u}};
    assert(request(101u)); core=1u;
}
int main(int argc,char **argv)
{
    assert(argc==2); const char *mode=argv[1]; init(); vdc_priority_match_snapshot_t out;
    if(!strcmp(mode,"match")) {
        out=sample(); assert(out.matched==1u && out.active && out.have_match && !out.retired);
        assert(out.event_sequence==0u && out.carrier_sequence==100u && out.arm_epoch==live.arm_epoch);
        assert(out.raw_lo==1200u && out.raw_hi==1210u); /* Not latest LIVE or TX elapsed. */
        assert(out.local_lo==10100u && out.expected_lo==10082u && out.expected_hi==10089u);
        assert(out.residual_lo==11 && out.residual_hi==38);
        assert(out.path_direction==VDC_PATH_DELAY_DIRECTION_MARKER_FORWARD);
        assert(out.path_qualification==VDC_PRIORITY_MATCH_PATH_PROVISIONAL_TRANSPOSE);
    } else if(!strcmp(mode,"signed")) {
        output_lo=10050u;output_hi=10060u;out=sample();assert(out.residual_lo==-39 && out.residual_hi==-22);
    } else if(!strcmp(mode,"duplicate")) {
        out=sample();unsigned calls=project_calls;out=sample();assert(out.matched==1u && out.repeated==1u && project_calls==calls);
    } else if(!strcmp(mode,"next") || !strcmp(mode,"full_sequence")) {
        out=sample();next(); if(!strcmp(mode,"full_sequence")){rx.typed_record.event_sequence=65536u;exact.record.sequence=65536u;}
        out=sample();assert(out.matched==2u && out.event_sequence==exact.record.sequence);
    } else if(!strcmp(mode,"out_of_order")) {
        next();out=sample();--rx.typed_record.event_sequence;--exact.record.sequence;
        expect(VDC_PRIORITY_MATCH_SEQUENCE,false);assert(project_calls==1u);
    } else if(!strcmp(mode,"generation")) {
        assert(!request(101u) && !request(100u));stopped=false;assert(!request(102u));stopped=true;
        session=0u;assert(!request(102u));session=17u;assert(request(0u));assert(!request(101u));assert(request(102u));
        expect(VDC_PRIORITY_MATCH_GENERATION,false);
    } else if(!strcmp(mode,"unbound_stop")) {
        ring.enabled=0u;expect(VDC_PRIORITY_MATCH_STOP,false);ring.enabled=1u;out=sample();assert(out.matched==1u);
    } else if(!strcmp(mode,"bound_stop") || !strcmp(mode,"rx_retired")) {
        out=sample();ring.enabled=0u;if(!strcmp(mode,"rx_retired"))rx.active=0u;
        expect(VDC_PRIORITY_MATCH_STOP,true);ring.enabled=rx.active=1u;out=sample();assert(out.retired && out.matched==1u);
    } else if(!strcmp(mode,"fresh_request")) {
        out=sample();ring.enabled=0u;out=sample();assert(request(102u));rx.typed_record.binding_generation=102u;
        ring.enabled=1u;out=sample();assert(out.matched==1u && !out.retired && out.generation==102u);
    } else if(!strcmp(mode,"disable")) {
        out=sample();assert(request(0u));expect(VDC_PRIORITY_MATCH_DISABLED,true);
    } else if(!strcmp(mode,"request_session")) {
        ++session;expect(VDC_PRIORITY_MATCH_BINDING,true);
    } else if(!strcmp(mode,"unserviced_stop_arm")) {
        out=sample();++live.arm_epoch;rx.active=0u;
        expect(VDC_PRIORITY_MATCH_BINDING,true);
    } else if(!strcmp(mode,"wrong_core")) {
        core=0u;out=sample();assert(!out.calls);core=1u;assert(!vdc_dpll_manager_set_priority_match(102u));
    } else if(!strcmp(mode,"pending_retry") || !strcmp(mode,"busy_retry")) {
        exact_result=!strcmp(mode,"pending_retry")?TDMA_EVENT_EXACT_PENDING:TDMA_EVENT_EXACT_BUSY;
        out=sample();assert(!out.matched && !out.retired && (out.busy+out.pending)==1u);
        exact_result=TDMA_EVENT_EXACT_OK;out=sample();assert(out.matched==1u && exact_calls==2u);
    } else if(!strcmp(mode,"evicted")) {
        exact_result=TDMA_EVENT_EXACT_EVICTED;out=sample();assert(out.history_miss==1u && !out.superseded);
    } else if(!strcmp(mode,"superseded")) {
        exact_result=TDMA_EVENT_EXACT_PENDING;out=sample();next();exact_result=TDMA_EVENT_EXACT_OK;
        out=sample();assert(out.superseded==1u && out.matched==1u && !out.history_miss);
    } else if(!strcmp(mode,"exact_retired") || !strcmp(mode,"exact_bad")) {
        out=sample();next();exact_result=!strcmp(mode,"exact_retired")?TDMA_EVENT_EXACT_RETIRED:TDMA_EVENT_EXACT_BAD_STATE;
        expect(VDC_PRIORITY_MATCH_SOURCE,!strcmp(mode,"exact_retired"));
    } else if(!strcmp(mode,"exact_wrong_sequence") || !strcmp(mode,"exact_wrong_anchor")) {
        if(!strcmp(mode,"exact_wrong_sequence"))++exact.record.sequence;else ++exact.timer1_enable_before;
        expect(VDC_PRIORITY_MATCH_SOURCE,false);
    } else if(!strcmp(mode,"raw_overflow")) {
        exact.record.rx_elapsed_cycles=UINT64_MAX;expect(VDC_PRIORITY_MATCH_OVERFLOW,false);
    } else if(!strcmp(mode,"old_model")) {
        model.valid_from_raw=1201u;expect(VDC_PRIORITY_MATCH_MODEL,false);assert(!project_calls);
    } else if(!strcmp(mode,"projection_failure")) {
        project_ok=false;expect(VDC_PRIORITY_MATCH_PROJECTION,false);
    } else if(!strcmp(mode,"remote_overflow") || !strcmp(mode,"delay_overflow")) {
        rx.typed_record.event_time_lower=UINT64_MAX-(!strcmp(mode,"remote_overflow")?1u:10u);
        expect(VDC_PRIORITY_MATCH_OVERFLOW,false);
    } else if(!strcmp(mode,"residual_overflow")) {
        output_lo=output_hi=UINT64_MAX;expect(VDC_PRIORITY_MATCH_OVERFLOW,false);
    } else if(!strcmp(mode,"difference_limits")) {
        int64_t value;assert(priority_match_difference(0u,UINT64_C(1)<<63,&value) && value==INT64_MIN);
        assert(priority_match_difference(INT64_MAX,0u,&value) && value==INT64_MAX);
        assert(!priority_match_difference(0u,(UINT64_C(1)<<63)+1u,&value));
        assert(!priority_match_difference(UINT64_C(1)<<63,0u,&value));
    } else if(!strcmp(mode,"reverse_projection")) {
        output_hi=output_lo-1u;expect(VDC_PRIORITY_MATCH_OVERFLOW,false);
    } else if(!strcmp(mode,"model_revision") || !strcmp(mode,"remote_revision")) {
        out=sample();next();if(!strcmp(mode,"model_revision")){++model.token;++model.dco.period_adjust_ppb;}
        else rx.typed_record.event_time_lower+=10u;
        out=sample();assert(out.matched==2u && !out.retired);
    } else if(!strcmp(mode,"model_race") || !strcmp(mode,"model_field_race") ||
              !strcmp(mode,"event_token_race") || !strcmp(mode,"event_applied_race")) {
        action=!strcmp(mode,"model_race")?1u:!strcmp(mode,"model_field_race")?2u:
            !strcmp(mode,"event_token_race")?3u:13u;
        expect(VDC_PRIORITY_MATCH_CHANGED,false);action=0u;out=sample();assert(out.matched==1u);
    } else if(!strcmp(mode,"session_race") || !strcmp(mode,"ring_race") || !strcmp(mode,"arm_race") ||
              !strcmp(mode,"observer_race") || !strcmp(mode,"rx_epoch_race") || !strcmp(mode,"path_race") ||
              !strcmp(mode,"remote_generation_race") || !strcmp(mode,"remote_source_race") || !strcmp(mode,"remote_target_race")) {
        action=!strcmp(mode,"session_race")?4u:!strcmp(mode,"ring_race")?5u:!strcmp(mode,"arm_race")?6u:
            !strcmp(mode,"observer_race")?7u:!strcmp(mode,"rx_epoch_race")?8u:!strcmp(mode,"path_race")?9u:
            !strcmp(mode,"remote_generation_race")?10u:!strcmp(mode,"remote_source_race")?11u:12u;
        expect(VDC_PRIORITY_MATCH_BINDING,true);
    } else if(!strcmp(mode,"model_busy") || !strcmp(mode,"live_busy") || !strcmp(mode,"rx_busy")) {
        if(!strcmp(mode,"model_busy"))model_ok=false;else if(!strcmp(mode,"live_busy"))live_ok=false;else rx_ok=false;
        expect(!strcmp(mode,"rx_busy")?VDC_PRIORITY_MATCH_RX:VDC_PRIORITY_MATCH_BUSY,false);
    } else if(!strcmp(mode,"role_master")) {
        s_vdc_domain.control.profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;expect(VDC_PRIORITY_MATCH_ROLE,false);
    } else if(!strcmp(mode,"wrong_source") || !strcmp(mode,"wrong_target")) {
        if(!strcmp(mode,"wrong_source"))rx.source_slot=2u;else rx.target_mask=1u;
        expect(VDC_PRIORITY_MATCH_RX,false);
    } else if(!strcmp(mode,"path_wrong_direction") || !strcmp(mode,"path_missing")) {
        if(!strcmp(mode,"path_missing"))path_ok=false;else s_vdc_domain.path_delay.entries[2].direction=0u;
        expect(VDC_PRIORITY_MATCH_PATH,false);
    } else if(!strncmp(mode,"binding_",8)) {
        out=sample();next();
        if(!strcmp(mode,"binding_config"))++ring.config_seq,++ring.applied_config_seq;
        else if(!strcmp(mode,"binding_profile"))++owner.ring_runtime.ring_profile_crc32;
        else if(!strcmp(mode,"binding_session"))++session;
        else if(!strcmp(mode,"binding_role"))++model.role_generation;
        else if(!strcmp(mode,"binding_clock"))++model.clock_run_id;
        else if(!strcmp(mode,"binding_rx_epoch"))++rx.epoch;
        else if(!strcmp(mode,"binding_arm"))++live.arm_epoch;
        else if(!strcmp(mode,"binding_observer"))++live.record.epoch;
        else if(!strcmp(mode,"binding_tick"))++live.tick_hz;
        else if(!strcmp(mode,"binding_path"))++s_vdc_domain.path_delay.table_crc32;
        else assert(!"unknown binding case");
        expect(VDC_PRIORITY_MATCH_BINDING,true);
    } else if(!strcmp(mode,"guard")) {
        out=sample();vdc_priority_match_snapshot_t sentinel;memset(&sentinel,0xa5,sizeof(sentinel));out=sentinel;
        assert(!vdc_dpll_manager_get_priority_match(NULL));++s_priority_match_guard;
        assert(!vdc_dpll_manager_get_priority_match(&out) && !memcmp(&out,&sentinel,sizeof(out)));
        ++s_priority_match_guard;collision_guard=&s_priority_match_guard;collision_word=s_priority_match_words+3u;
        assert(!vdc_dpll_manager_get_priority_match(&out) && !memcmp(&out,&sentinel,sizeof(out)));
    } else if(!strcmp(mode,"saturation")) {
        out=sample();s_priority_match_work.status.matched=UINT32_MAX;s_priority_match_work.status.calls=UINT32_MAX;
        s_priority_match_work.status.repeated=UINT32_MAX;out=sample();assert(out.repeated==UINT32_MAX);
        next();out=sample();assert(out.matched==UINT32_MAX && out.calls==UINT32_MAX);
    } else assert(!"unknown case");
    printf("snapshot=%zu workspace=%zu totalstatic=%zu match passed\n",sizeof(out),sizeof(s_priority_match_work),
        sizeof(s_priority_match_work)+sizeof(s_priority_match_words)+16u);
    return 0;
}
'''


REAL_MAPPING_MAIN = r'''
static void mapping_setup(void) {
    init();
    model.valid_from_raw=999000u;
    model.dco.base_local_tick64=3000000u;
    model.dco.base_vdc_time64_ns=UINT64_C(12000000000);
    model.dco.period_adjust_ppb=777;model.dco.phase_offset_ns=-17;
    live.timer1_enable_before=exact.timer1_enable_before=1000000u;
    live.timer1_enable_after=exact.timer1_enable_after=1000004u;
    exact.record.rx_elapsed_cycles=0u;
    mapping_bridge=(vdc_timestamp_clock_bridge_t){.raw_before=1000125u,.raw_after=1000129u,
        .local_ns=UINT64_C(10000000000),.tick_hz=250000000u};
    rx.typed_record.event_time_lower=UINT64_C(12001000000);
}
static void mapping_fresh(uint64_t ticks) {
    next();exact.record.rx_elapsed_cycles+=ticks;
    mapping_bridge.raw_after+=ticks;rx.typed_record.event_time_lower+=ticks*4u;
}
static void assert_interval_sound(const vdc_priority_match_snapshot_t *out) {
    uint64_t lo,hi;
    assert(vdc_domain_dco_local_to_output_ns(&model.dco,out->raw_lo*4u,&lo));
    assert(vdc_domain_dco_local_to_output_ns(&model.dco,out->raw_hi*4u,&hi));
    assert(out->local_lo==lo && out->local_hi==hi);
    assert(out->residual_lo==(int64_t)(out->local_lo-out->expected_hi));
    assert(out->residual_hi==(int64_t)(out->local_hi-out->expected_lo));
}
int main(int argc,char **argv) {
    assert(argc==2);const char *name=argv[1];mapping_setup();
    vdc_priority_match_snapshot_t first=sample(),out;
    assert(first.matched==1u && first.active && !first.retired);assert_interval_sound(&first);
    const unsigned calls=project_calls;
    if(!strcmp(name,"duplicate") || !strcmp(name,"model_duplicate")) {
        if(!strcmp(name,"model_duplicate")) {++model.token;++model.dco.period_adjust_ppb;}
        project_ok=false;out=sample();
        assert(out.matched==1u && out.repeated==1u && project_calls==calls);return 0;
    }
    if(!strcmp(name,"stop")) {ring.enabled=0;expect(VDC_PRIORITY_MATCH_STOP,true);return 0;}
    if(!strcmp(name,"disable")) {assert(request(0));expect(VDC_PRIORITY_MATCH_DISABLED,true);return 0;}
    if(!strcmp(name,"observer_retire")) {++live.record.epoch;expect(VDC_PRIORITY_MATCH_BINDING,true);return 0;}
    if(!strcmp(name,"exact_retire")) {next();exact_result=TDMA_EVENT_EXACT_RETIRED;expect(VDC_PRIORITY_MATCH_SOURCE,true);return 0;}
    mapping_fresh(!strcmp(name,"long_gap")?UINT64_C(500000001):125u);
    if(!strcmp(name,"projection_unavailable_retry")) {
        project_ok=false;expect(VDC_PRIORITY_MATCH_PROJECTION,false);
        assert(s_priority_match_work.status.matched==1u && !s_priority_match_work.fresh);
        project_ok=true;
    }
    if(!strcmp(name,"final_model_reject_retry") || !strcmp(name,"final_field_reject_retry")) {
        action=!strcmp(name,"final_model_reject_retry")?1:2;
        expect(VDC_PRIORITY_MATCH_CHANGED,false);action=0;
        assert(s_priority_match_work.status.matched==1u && !s_priority_match_work.fresh);
    }
    if(!strcmp(name,"final_arithmetic_reject_retry")) {
        const uint64_t saved=rx.typed_record.event_time_lower;rx.typed_record.event_time_lower=UINT64_MAX-1;
        expect(VDC_PRIORITY_MATCH_OVERFLOW,false);rx.typed_record.event_time_lower=saved;
        assert(s_priority_match_work.status.matched==1u);
    }
    if(!strcmp(name,"model_new_event")) {++model.token;++model.dco.period_adjust_ppb;}
    if(!strcmp(name,"new_generation")) {assert(request(102));rx.typed_record.binding_generation=102;}
    if(!strcmp(name,"future_event")) {
        mapping_bridge.raw_after=first.raw_hi-1;expect(VDC_PRIORITY_MATCH_PROJECTION,false);return 0;
    }
    if(!strcmp(name,"final_binding_retire")) {action=6;expect(VDC_PRIORITY_MATCH_BINDING,true);return 0;}
    if(!strcmp(name,"timer0_independent")) {
        mapping_bridge.local_ns=UINT64_MAX;mapping_bridge.raw_before=0;
    }
    out=sample();assert(out.active && !out.retired);assert_interval_sound(&out);
    assert(out.matched==(!strcmp(name,"new_generation")?1u:2u));
    if(!strcmp(name,"many_events"))for(unsigned i=0;i<70;++i) {
        mapping_fresh(125);out=sample();assert(out.matched==i+3);assert_interval_sound(&out);
    }
    return 0;
}
'''
