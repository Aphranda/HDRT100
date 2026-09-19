"""Execute committed-model publication and event association through its real wrapper."""
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def owner_executable(tmp_path_factory):
    source = (ROOT / "components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(encoding="utf-8")
    wrapper = ingress_definition(source.replace("void __attribute__((noinline)) sync_dpll_fb_service",
                                                "void sync_dpll_fb_service"), "sync_dpll_fb_service")
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "vdc_model_projection.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
static unsigned get_core_num(void) { return 1u; }
static vdc_domain_context_t s_vdc_domain;
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static bool s_vdc_ready=true, stopped=true, clock_ok=true, bridge_ok=true;
static uint64_t raw_now=1000000;
static unsigned action;
static unsigned match_calls, ingress_calls, step_calls;
static bool auto_mode;
static bool mode_available=true;
bool vdc_dpll_manager_try_boundary_auto_enabled(bool *out)
{ if(!mode_available)return false;*out=auto_mode;return true; }
bool vdc_dpll_manager_boundary_auto_enabled(void) { return auto_mode; }
static vdc_timestamp_clock_bridge_t bridge={.raw_before=1001000,.raw_after=1001004,
    .local_ns=10000000,.tick_hz=250000000};
bool tdma_service_update_stopped_metadata(tdma_service_service_t *p,bool(*publish)(void*),void *c)
{ assert(p==&owner);return stopped && publish(c); }
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{ assert(hz==250000000);if(!clock_ok)return false;*out=raw_now;return true; }
bool vdc_timestamp_clock_try_read_bridge(uint32_t hz,vdc_timestamp_clock_bridge_t *out)
{ assert(hz==250000000);if(!bridge_ok)return false;*out=bridge;return true; }
/* Projection arithmetic is separately tested against the real Domain mapper. */
bool vdc_domain_dco_local_to_output_ns(const vdc_dco_control_t *d,uint64_t t,uint64_t *out)
{ assert(d->valid);*out=t+d->phase_offset_ns;return true; }
void vdc_domain_set_ready(vdc_domain_context_t *c,bool ready) { c->ready=ready; }
''' + (ROOT / "components/vdc_dpll_manager/src/vdc_model_feedback.inc").read_text(encoding="utf-8") + r'''
/* Real recorder behavior is covered by test_vdc_priority_trace. This model
 * owner fixture checks its placement outside the committed-model guard. */
static void priority_trace_service_core1(void)
{
    assert(!(s_committed_model_guard & 1u));
    assert(match_calls == ingress_calls && ingress_calls == step_calls);
}
static void priority_trace_match_core1(void)
{
    assert(!(s_committed_model_guard & 1u));
    assert(match_calls == ingress_calls + 1u && ingress_calls == step_calls);
}
static void vdc_priority_match_core1(void)
{
    /* Matching projects against the committed model before ingress and before
     * the writer opens its guard, including session-disabled service beats. */
    assert(!(s_committed_model_guard & 1u));
    assert(match_calls == ingress_calls && ingress_calls == step_calls);
    ++match_calls;
}
static void vdc_priority_ingress_core1(void)
{
    /* Real wrapper must run ingress before opening the committed-model guard,
     * even with no feedback session or with a later role/step early return. */
    assert(!(s_committed_model_guard & 1u));
    assert(match_calls == ingress_calls + 1u && ingress_calls == step_calls);
    ++ingress_calls;
}
static void sync_dpll_fb_step(void)
{
    assert(match_calls == ingress_calls && ingress_calls == step_calls + 1u); ++step_calls;
    if(vdc_dpll_manager_feedback_session()) {
        assert(s_committed_model_guard & 1u);
        vdc_dpll_manager_committed_model_t out;
        memset(&out,0xa5,sizeof(out));const vdc_dpll_manager_committed_model_t saved=out;
        assert(!vdc_dpll_manager_get_committed_model(&out));assert(!memcmp(&out,&saved,sizeof(out)));
        if(action==1)s_vdc_domain.dco.phase_offset_ns+=100;
        if(action==2)s_vdc_domain.control.last_follower_command_seq++;
    }
}
/* Boundary owner is exercised separately; this fixture isolates model publication. */
static void vdc_boundary_service_core1(void) {}
static void reference_discipline_service_core1(uint32_t session)
{ assert(!session || (s_committed_model_guard & 1u)); }
static void priority_summary_service(bool active)
{ assert(active && !(s_committed_model_guard & 1u)); }
static void priority_guard_service_core1(void)
{ assert(!(s_committed_model_guard & 1u)); }
static void priority_follow_prepare_core1(void)
{
    assert(!(s_committed_model_guard & 1u));
    assert(match_calls == ingress_calls + 1u && ingress_calls == step_calls);
}
static void priority_follow_apply_core1(void)
{
    assert(match_calls == step_calls && ingress_calls == step_calls);
    assert((s_committed_model_guard & 1u) == (vdc_dpll_manager_feedback_session() ? 1u : 0u));
}
''' + wrapper + r'''
static bool project(uint64_t lo,vdc_dpll_manager_projected_event_t *out)
{ raw_now=1001004;return vdc_dpll_manager_project_feedback_event(123,9,3,4,2,0xabc,250000000,lo,1000500,out); }
int main(int argc,char **argv)
{
    assert(argc==2);const char *mode=argv[1];
    s_vdc_domain.dco=(vdc_dco_control_t){.valid=1,.nominal_period_ns=1500000,.tdma_schedule_crc32=0xabc};
    s_vdc_domain.clock.epoch_id=3;s_vdc_domain.clock.run_id=4;
    s_vdc_domain.control.profile.generation=9;s_vdc_domain.schedule.local_slot_id=2;
    assert(!vdc_dpll_manager_feedback_session());sync_dpll_fb_service();
    assert(match_calls==1u && ingress_calls==1u && step_calls==1u);
    assert(vdc_dpll_manager_set_feedback_session(123));sync_dpll_fb_service();
    assert(match_calls==2u && ingress_calls==2u && step_calls==2u);
    vdc_dpll_manager_committed_model_t model;assert(vdc_dpll_manager_get_committed_model(&model));
    assert(model.token==1 && model.session==123 && model.valid_from_raw==raw_now);
    raw_now=1001004;
    vdc_dpll_manager_projected_event_t event,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));event=sentinel;
    if(!strcmp(mode,"rate_coordinates")) {
        auto_mode=true;
        vdc_dpll_manager_rate_event_t rate;
        assert(vdc_dpll_manager_project_rate_feedback_event(123,9,3,4,2,0xabc,250000000,
            1000400,1000500,400,&rate));
        assert(rate.coordinate_ns==1600 && rate.model_token==1);
        assert(project(1000400,&event) && rate.absolute_output_ns_lo==event.output_ns_lo);
        assert(vdc_dpll_manager_project_rate_reference(123,9,3,4,2,0xabc,250000000,
            1000400,1000500,&event));
        assert(event.output_ns_lo==4001600 && event.output_ns_hi==4002000);
        const vdc_dpll_manager_rate_event_t saved=rate;
        mode_available=false;
        assert(!vdc_dpll_manager_project_rate_feedback_event(123,9,3,4,2,0xabc,250000000,
            1000400,1000500,400,&rate));
        assert(!memcmp(&rate,&saved,sizeof(rate)));mode_available=true;
        bridge.raw_after+=500;bridge.local_ns+=777;
        assert(vdc_dpll_manager_project_rate_feedback_event(123,9,3,4,2,0xabc,250000000,
            1000400,1000500,400,&rate));
        assert(rate.coordinate_ns==saved.coordinate_ns && rate.absolute_output_ns_lo==saved.absolute_output_ns_lo);
        auto_mode=false;
        assert(!vdc_dpll_manager_project_rate_reference(123,9,3,4,2,0xabc,250000000,
            1000400,1000500,&event));
    } else if(!strcmp(mode,"actual_commit")) {
        assert(project(1000400,&event));const uint64_t old=event.output_ns_lo;
        raw_now=1000300;action=1;sync_dpll_fb_service();assert(project(1000400,&event));
        assert(event.output_ns_lo==old+100 && event.model_token==2);
        assert(!project(1000200,&event));
    } else if(!strcmp(mode,"stable_model")) {
        raw_now=1000900;sync_dpll_fb_service();assert(project(1000400,&event));
        assert(event.model_token==1);
        action=2;sync_dpll_fb_service();assert(project(1000400,&event));
        assert(event.applied_command_seq==1);
    } else if(!strcmp(mode,"session")) {
        stopped=false;assert(!vdc_dpll_manager_set_feedback_session(124));
        stopped=true;assert(vdc_dpll_manager_set_feedback_session(0));assert(!project(1000400,&event));
        assert(!vdc_dpll_manager_set_feedback_session(123));assert(vdc_dpll_manager_set_feedback_session(124));
        sync_dpll_fb_service();assert(vdc_dpll_manager_get_committed_model(&model));
        assert(model.session==124 && model.token==2);
    } else if(!strcmp(mode,"epoch")) {
        s_vdc_domain.clock.run_id++;sync_dpll_fb_service();assert(!project(1000400,&event));
    } else if(!strcmp(mode,"role")) {
        s_vdc_domain.control.profile.generation++;sync_dpll_fb_service();assert(!project(1000400,&event));
    } else if(!strcmp(mode,"clock_failure")) {
        action=1;clock_ok=false;sync_dpll_fb_service();assert(!project(1000400,&event));
        clock_ok=true;action=0;raw_now=1000300;sync_dpll_fb_service();assert(project(1000400,&event));
    } else if(!strcmp(mode,"exhaustion")) {
        s_committed_model_serial=UINT32_MAX;action=1;sync_dpll_fb_service();assert(!project(1000400,&event));
        action=0;sync_dpll_fb_service();assert(!project(1000400,&event));
    } else if(!strcmp(mode,"bridge_failure")) {
        bridge_ok=false;assert(project(1000400,&event));
        assert(event.output_ns_lo==4001600 && event.output_ns_hi==4002000);
    } else if(!strcmp(mode,"old_event")) {
        assert(!project(999999,&event));assert(!memcmp(&event,&sentinel,sizeof(event)));
    } else assert(!"unknown scenario");
    puts("committed model owner passed");return 0;
}
'''
    return compile_executable(tmp_path_factory.mktemp("model-owner"), "model_owner", harness)


@pytest.mark.parametrize("case", ["actual_commit", "stable_model", "session", "epoch", "role",
                                 "clock_failure", "exhaustion", "bridge_failure", "old_event", "rate_coordinates"])
def test_model_owner(owner_executable, case):
    result = subprocess.run([str(owner_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
