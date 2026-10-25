"""Execute fixed-output owner admission, model races and cancellation paths."""
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable, function_body


@pytest.fixture(scope="module")
def fixed_owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-fixed-output")
    owner = (ROOT / 'components/vdc_dpll_manager/archive/vdc_inc_split_v1/vdc_fixed_output.inc').read_text(encoding='utf-8')
    # Only substitute the architecture's read-only IRQ register boundary.
    # Admission, retry, deadline, reason and cleanup logic remain production C.
    owner = owner.replace(function_body(owner, 'fixed_output_irq_ready'), '\n    return irq_ready;\n', 1)
    harness = r'''
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_dpll_manager.h"
''' + '#include "' + (ROOT / 'components/sync_io/inc/sync_io.h').as_posix() + '"\n' + r'''
#define BOARD_SYS_CLOCK_HZ 250000000u
static unsigned core, arm_calls, abort_calls, race;
static bool stopped=true, clock_ok=true, model_available=true, resource_ok=true;
static bool ring_available=true;
static bool irq_ready=true;
static unsigned phase, enables, leases, ring_reads[4], model_reads[4], clock_reads[4];
static unsigned ring_missing[4], model_missing[4], change_phase=99u;
static uint32_t now_us=1000u, clock_cost;
static unsigned time_reads[4];
static const char *probe_mode;
static uint32_t session=100;
static vdc_dpll_manager_committed_model_t current;
static sync_io_fixed_rate_runtime_t hardware;
static struct { bool active; } s_observation_self_test;
static unsigned get_core_num(void) { return core; }
static uint32_t time_us_32(void) {
  ++time_reads[phase];
  if(probe_mode && phase==change_phase) {
    if(!strcmp(probe_mode,"wrap") && time_reads[phase]==1)now_us=UINT_MAX-5;
    if(!strcmp(probe_mode,"next_read_expiry") && time_reads[phase]==2)
      now_us+=VDC_FIXED_OUTPUT_SNAPSHOT_BUDGET_US;
    if(!strcmp(probe_mode,"next_read_last_us") && time_reads[phase]==2)
      now_us+=VDC_FIXED_OUTPUT_SNAPSHOT_BUDGET_US-1;
    if(!strcmp(probe_mode,"session_between") && time_reads[phase]==2)
      {++session;current.session=session;}
  }
  return now_us;
}
bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{
  ++ring_reads[phase];
  if(!ring_available || ring_reads[phase]<=ring_missing[phase])return false;
  memset(out,0,sizeof(*out));out->enabled=!stopped;
  if(probe_mode && phase==change_phase) {
    if(!strcmp(probe_mode,"ring_bad"))out->enabled=true;
    if(!strcmp(probe_mode,"config_bad"))out->config_seq=1;
  }
  return true;
}
bool vdc_timestamp_clock_configuration_supported(uint32_t hz)
{
  assert(hz==BOARD_SYS_CLOCK_HZ);++clock_reads[phase];now_us+=clock_cost;
  if(probe_mode && phase==change_phase) {
    if(!strcmp(probe_mode,"clock_bad"))clock_ok=false;
    if(!strcmp(probe_mode,"selftest_bad"))s_observation_self_test.active=true;
    if(!strcmp(probe_mode,"session_after"))++session;
    if(!strcmp(probe_mode,"irq_after"))irq_ready=false;
    if(!strcmp(probe_mode,"first_valid_long_irq") ||
       (!strcmp(probe_mode,"first_unknown_long_irq") && clock_reads[phase]==1) ||
       ((!strcmp(probe_mode,"valid_retry_long_irq") ||
         !strcmp(probe_mode,"unknown_retry_long_irq")) && clock_reads[phase]==2))
      now_us+=10*VDC_FIXED_OUTPUT_SNAPSHOT_BUDGET_US;
    if(!strcmp(probe_mode,"late_invalid")){
      now_us+=VDC_FIXED_OUTPUT_SNAPSHOT_BUDGET_US;clock_ok=false;
    }
  }
  return clock_ok;
}
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{
  ++model_reads[phase];
  if(probe_mode && phase==change_phase) {
    if(!strcmp(probe_mode,"session_during"))++session;
    if(model_reads[phase]==2 && !strcmp(probe_mode,"miss_then_changed"))++current.token;
    if(model_reads[phase]==2 && !strcmp(probe_mode,"miss_then_invalid"))current.dco.valid=0;
    if(!strcmp(probe_mode,"model_bad"))current.dco.valid=0;
  }
  if(!model_available || model_reads[phase]<=model_missing[phase])return false;
  *out=current;return true;
}
bool sync_io_sma_observer_fixed_rate_arm(const sync_io_rate_schedule_request_t *req,
    sync_io_fixed_rate_runtime_t *out,bool(*validate)(void*),void *context)
{
    ++arm_calls;
    if(!resource_ok)return false;
    phase=1;leases=1;
    if(race==1)++current.token;
    if(!validate(context)){leases=0;return false;}
    memset(&hardware,0,sizeof(hardware));
    hardware.configured=true;hardware.request=*req;
    hardware.pulse.running=true;hardware.pulse.pio_enabled=true;
    hardware.pulse.dma_busy=true;++enables;
    hardware.pulse.total_pulses=req->pulse_count;
    hardware.pulse.transfer_count=req->pulse_count*2;
    hardware.system_clock_hz=BOARD_SYS_CLOCK_HZ;
    hardware.pio_divider256=256;
    *out=hardware;
    if(race==2)++current.dco.dco_update_seq;
    phase=2;
    return true;
}
static uint32_t admitted_marker;
bool sync_io_sma_observer_subdivision_arm(const sync_io_rate_schedule_request_t *req,
    uint32_t marker_high_ns, sync_io_fixed_rate_runtime_t *out,
    bool(*validate)(void*), void *context)
{
    if(!sync_io_rate_schedule_marked_valid(req,marker_high_ns))return false;
    admitted_marker=marker_high_ns;
    return sync_io_sma_observer_fixed_rate_arm(req,out,validate,context);
}
bool sync_io_sma_observer_subdivision_window_arm(const sync_io_rate_schedule_request_t *req,
    uint32_t marker_high_ns, sync_io_fixed_rate_runtime_t *out,
    bool(*validate)(void*), void *context)
{
    return req->pulse_count>=3 && sync_io_sma_observer_subdivision_arm(
        req,marker_high_ns,out,validate,context);
}
void sync_io_sma_observer_fixed_rate_disarm(void)
{ ++abort_calls;leases=0;hardware.pulse.running=false;hardware.pulse.pio_enabled=false;
  hardware.pulse.dma_busy=false; }
void sync_io_sma_observer_fixed_rate_get_runtime(sync_io_fixed_rate_runtime_t *out)
{ *out=hardware;phase=3; }
''' + owner + r'''
static void probe(unsigned target,const char *mode)
{
    assert(target<3);change_phase=target;probe_mode=mode;
    current=(vdc_dpll_manager_committed_model_t){.token=2,.session=100,
        .dco={.valid=1,.dco_update_seq=3,.period_adjust_ppb=1563}};
    bool accepted_expected=false;
    uint32_t reason=VDC_FIXED_OUTPUT_INITIAL_RING_UNAVAILABLE+3*target;
    if(!strcmp(mode,"ring_transient")){ring_missing[target]=2;accepted_expected=true;}
    else if(!strcmp(mode,"model_transient")){model_missing[target]=2;accepted_expected=true;}
    else if(!strcmp(mode,"both_transient")){ring_missing[target]=1;model_missing[target]=2;accepted_expected=true;}
    else if(!strcmp(mode,"last_attempt_success")){model_missing[target]=VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS-1;accepted_expected=true;}
    else if(!strcmp(mode,"ring_persistent"))ring_missing[target]=UINT_MAX;
    else if(!strcmp(mode,"model_persistent")){model_missing[target]=UINT_MAX;++reason;}
    else if(!strcmp(mode,"ring_bad") || !strcmp(mode,"config_bad"))reason=VDC_FIXED_OUTPUT_NOT_STOPPED;
    else if(!strcmp(mode,"model_bad")){ring_missing[target]=UINT_MAX;reason=target?VDC_FIXED_OUTPUT_CHANGED:VDC_FIXED_OUTPUT_MODEL;}
    else if(!strcmp(mode,"miss_then_invalid")){model_missing[target]=1;reason=target?VDC_FIXED_OUTPUT_CHANGED:VDC_FIXED_OUTPUT_MODEL;}
    else if(!strcmp(mode,"miss_then_changed")){assert(target);model_missing[target]=1;reason=VDC_FIXED_OUTPUT_CHANGED;}
    else if(!strcmp(mode,"clock_bad")){ring_missing[target]=UINT_MAX;reason=VDC_FIXED_OUTPUT_CLOCK;}
    else if(!strcmp(mode,"selftest_bad")){model_missing[target]=UINT_MAX;reason=VDC_FIXED_OUTPUT_SELFTEST;}
    else if(!strcmp(mode,"session_during") || !strcmp(mode,"session_after")) {
        ring_missing[target]=UINT_MAX;model_missing[target]=UINT_MAX;
        reason=target?VDC_FIXED_OUTPUT_CHANGED:VDC_FIXED_OUTPUT_MODEL;
    }
    else if(!strcmp(mode,"irq_after"))reason=VDC_FIXED_OUTPUT_IRQ_CONTEXT;
    else if(!strcmp(mode,"next_read_expiry")){model_missing[target]=1;reason+=2;}
    else if(!strcmp(mode,"session_between")){model_missing[target]=1;reason=target?VDC_FIXED_OUTPUT_CHANGED:VDC_FIXED_OUTPUT_MODEL;}
    else if(!strcmp(mode,"late_invalid")){model_missing[target]=UINT_MAX;reason=VDC_FIXED_OUTPUT_CLOCK;}
    else if(!strcmp(mode,"first_valid_long_irq"))accepted_expected=true;
    else if(!strcmp(mode,"first_unknown_long_irq") || !strcmp(mode,"valid_retry_long_irq") ||
            !strcmp(mode,"next_read_last_us")){model_missing[target]=1;accepted_expected=true;}
    else if(!strcmp(mode,"unknown_retry_long_irq")){model_missing[target]=UINT_MAX;reason+=2;}
    else if(!strcmp(mode,"deadline_persistent")){clock_cost=10;model_missing[target]=UINT_MAX;reason+=2;}
    else if(!strcmp(mode,"wrap")){clock_cost=4;model_missing[target]=2;accepted_expected=true;}
    else if(!strcmp(mode,"irq_before")){assert(!target);irq_ready=false;reason=VDC_FIXED_OUTPUT_IRQ_CONTEXT;}
    else assert(false);
    uint32_t request=0xabcdef;
    const bool accepted=vdc_dpll_manager_fixed_output_start(1000000,2000,2048,4,&request);
    assert(accepted==accepted_expected);
    assert(ring_reads[target]<=VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS);
    assert(model_reads[target]<=VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS);
    if(accepted) {
        assert(request==1 && arm_calls==1 && enables==1 && !abort_calls && leases==1);
        assert(s_fixed_output.last_reason==VDC_FIXED_OUTPUT_OK);
        assert(s_fixed_output.model.token==2 && s_fixed_output.model.dco.dco_update_seq==3);
        assert(vdc_dpll_manager_fixed_output_stop());assert(abort_calls==1 && leases==0);
    } else {
        assert(request==0xabcdef && s_fixed_output.last_reason==reason);
        assert(arm_calls==(target?1u:0u));
        assert(enables==(target==2?1u:0u) && abort_calls==(target==2?1u:0u));
        assert(!leases && !hardware.pulse.running && !hardware.pulse.pio_enabled && !hardware.pulse.dma_busy);
        if(target==2)assert(hardware.pulse.transfer_count==4096);
        const unsigned abort_before=abort_calls;
        assert(vdc_dpll_manager_fixed_output_stop() && abort_calls==abort_before);
    }
    if(!strcmp(mode,"ring_persistent"))assert(ring_reads[target]==VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS);
    if(!strcmp(mode,"model_persistent"))assert(model_reads[target]==VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS);
    if(!strcmp(mode,"model_bad") || !strcmp(mode,"clock_bad") || !strcmp(mode,"selftest_bad") ||
       !strcmp(mode,"session_during") || !strcmp(mode,"session_after"))assert(ring_reads[target]==1);
    if(!strcmp(mode,"miss_then_changed") || !strcmp(mode,"miss_then_invalid"))assert(model_reads[target]==2);
    if(!strcmp(mode,"next_read_expiry"))assert(ring_reads[target]==1 && model_reads[target]==1);
    if(!strcmp(mode,"first_valid_long_irq"))
        assert(ring_reads[target]==1 && model_reads[target]==1 && time_reads[target]==0);
    if(!strcmp(mode,"first_unknown_long_irq") || !strcmp(mode,"valid_retry_long_irq") ||
       !strcmp(mode,"next_read_last_us") || !strcmp(mode,"unknown_retry_long_irq"))
        assert(ring_reads[target]==2 && model_reads[target]==2);
    if(!strcmp(mode,"last_attempt_success"))assert(model_reads[target]==VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS);
    if(!strcmp(mode,"wrap"))assert(time_reads[target]>=3 && now_us<1000u);
    if(!strcmp(mode,"irq_before"))assert(!ring_reads[target] && !model_reads[target] && !irq_ready);
}
static void subdivision_probe(const char *mode)
{
    current=(vdc_dpll_manager_committed_model_t){.token=2,.session=100,
        .dco={.valid=1,.dco_update_seq=3,.period_adjust_ppb=1563}};
    uint32_t request=0xabcdef, count=8, tick=4, marker=2001;
    if(!strcmp(mode,"zero_count"))count=0;
    if(!strcmp(mode,"overflow_count"))count=UINT_MAX;
    if(!strcmp(mode,"oversize_count"))count=2048;
    if(!strcmp(mode,"max_count"))count=2047;
    if(!strcmp(mode,"zero_tick"))tick=0;
    if(!strcmp(mode,"bad_marker"))marker=1000;
    if(!strcmp(mode,"run"))stopped=false;
    if(!strcmp(mode,"race_before"))race=1;
    if(!strcmp(mode,"race_after"))race=2;
    if(!strcmp(mode,"resource"))resource_ok=false;
    if(!strcmp(mode,"core1"))core=1;
    if(!strcmp(mode,"fixed_busy")) {
        assert(vdc_dpll_manager_fixed_output_start(1000000,1000,9,4,&request));
        assert(!vdc_dpll_manager_subdivision_output_start(10000,1000,8,2001,4,&request));
        assert(!vdc_dpll_manager_subdivision_output_stop());
        assert(!abort_calls && hardware.pulse.running && s_output_observation_mode==0);
        vdc_dpll_manager_subdivision_output_status_t s,copy;
        memset(&s,0xa5,sizeof(s));copy=s;
        assert(!vdc_dpll_manager_get_subdivision_output(&s));
        assert(!memcmp(&s,&copy,sizeof(s)));
        assert(vdc_dpll_manager_fixed_output_stop() && abort_calls==1);
        return;
    }
    const bool accepted=vdc_dpll_manager_subdivision_output_start(
        10000,1000,count,marker,tick,&request);
    if(!count || count>=2048 || core || !stopped) {
        assert(!accepted && request==0xabcdef && !arm_calls && !abort_calls);
        return;
    }
    if(race || !resource_ok || !tick || marker==1000) {
        assert(!accepted && request==0xabcdef && !leases);
        assert(abort_calls==(race==2 ? 1u:0u));
        vdc_dpll_manager_subdivision_output_status_t s;
        assert(vdc_dpll_manager_get_subdivision_output(&s));
        assert(s.batch.state==VDC_FIXED_OUTPUT_FAILED && s.subdivision_count==8);
        assert(s.marker_high_ticks==(race==2 ? 501u:0u));
        return;
    }
    assert(accepted && request==1 && hardware.request.pulse_count==count+1);
    assert(hardware.request.period_ns==10000 && hardware.request.rate_ppb==1563);
    assert(admitted_marker==2001 && s_output_observation_mode==1);
    vdc_dpll_manager_fixed_output_status_t fixed,copy;
    memset(&fixed,0xa5,sizeof(fixed));copy=fixed;
    assert(!vdc_dpll_manager_get_fixed_output(&fixed));
    assert(!memcmp(&fixed,&copy,sizeof(fixed)));
    assert(!vdc_dpll_manager_fixed_output_stop() && !abort_calls);
    vdc_dpll_manager_subdivision_output_status_t s;
    assert(vdc_dpll_manager_get_subdivision_output(&s));
    assert(s.mode==1 && s.marker_high_ns==2001 && s.marker_high_ticks==501);
    assert(s.subdivision_count==count && s.batch.pulse_count==count+1);
    assert(s.batch.state==VDC_FIXED_OUTPUT_RUNNING);
    if(!strcmp(mode,"busy")) {
        assert(!vdc_dpll_manager_fixed_output_start(1000000,1000,9,4,&request));
        assert(!vdc_dpll_manager_subdivision_output_start(10000,1000,8,2001,4,&request));
        assert(arm_calls==1 && !abort_calls && hardware.pulse.running);
    } else if(!strcmp(mode,"changed")) {
        ++current.token;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_CANCELLED && abort_calls==1);
    } else if(!strcmp(mode,"start_tdma")) {
        stopped=false;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_CANCELLED && abort_calls==1);
        assert(!vdc_dpll_manager_get_subdivision_output(&s));
    } else if(!strcmp(mode,"complete")) {
        hardware.pulse.running=false;hardware.pulse.completed_pulses=count+1;
        hardware.pulse.pio_enabled=false;hardware.pulse.dma_busy=false;leases=0;
        vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_COMPLETE);
        assert(vdc_dpll_manager_subdivision_output_stop() && !abort_calls);
        assert(vdc_dpll_manager_fixed_output_start(1000000,1000,9,4,&request));
        assert(request==2 && !vdc_dpll_manager_get_subdivision_output(&s));
        assert(!vdc_dpll_manager_subdivision_output_stop() && !abort_calls);
        assert(vdc_dpll_manager_fixed_output_stop() && abort_calls==1);
        return;
    }
    assert(vdc_dpll_manager_subdivision_output_stop());
    assert(abort_calls==1 && !leases);
    assert(vdc_dpll_manager_subdivision_output_stop() && abort_calls==1);
}
static void window_probe(const char *mode)
{
    current=(vdc_dpll_manager_committed_model_t){.token=2,.session=100,
        .dco={.valid=1,.dco_update_seq=3,.period_adjust_ppb=1563}};
    uint32_t request=0xabcdef, sync=1000000, interval=1000, high=100, marker=200, tick=4;
    if(!strcmp(mode,"min_count"))sync=3000;
    if(!strcmp(mode,"max_count"))sync=2048000;
    if(!strcmp(mode,"small_count"))sync=2000;
    if(!strcmp(mode,"oversize_count"))sync=2049000;
    if(!strcmp(mode,"overflow_count")){sync=UINT_MAX;interval=1;}
    if(!strcmp(mode,"nondivisible"))++sync;
    if(!strcmp(mode,"zero_sync"))sync=0;
    if(!strcmp(mode,"zero_interval"))interval=0;
    if(!strcmp(mode,"zero_high"))high=0;
    if(!strcmp(mode,"zero_marker"))marker=0;
    if(!strcmp(mode,"zero_tick"))tick=0;
    if(!strcmp(mode,"run"))stopped=false;
    if(!strcmp(mode,"race_before"))race=1;
    if(!strcmp(mode,"race_after"))race=2;
    if(!strcmp(mode,"resource"))resource_ok=false;
    if(!strcmp(mode,"core1"))core=1;
    if(!strcmp(mode,"fixed_busy") || !strcmp(mode,"subdiv_busy")) {
        const bool fixed=!strcmp(mode,"fixed_busy");
        assert(fixed ? vdc_dpll_manager_fixed_output_start(1000000,1000,9,4,&request)
            : vdc_dpll_manager_subdivision_output_start(10000,1000,8,2001,4,&request));
        assert(!vdc_dpll_manager_subdivision_window_start(sync,interval,high,marker,tick,&request));
        assert(!vdc_dpll_manager_subdivision_window_stop());
        vdc_dpll_manager_subdivision_output_status_t s,copy;
        memset(&s,0xa5,sizeof(s));copy=s;
        assert(!vdc_dpll_manager_get_subdivision_window(&s) && !memcmp(&s,&copy,sizeof(s)));
        assert(arm_calls==1 && !abort_calls && hardware.pulse.running);
        assert(fixed ? vdc_dpll_manager_fixed_output_stop() : vdc_dpll_manager_subdivision_output_stop());
        return;
    }
    const bool accepted=vdc_dpll_manager_subdivision_window_start(sync,interval,high,marker,tick,&request);
    const bool invalid_count=!interval || sync%interval || sync/interval<3 || sync/interval>2048;
    if(invalid_count || !stopped || core) {
        assert(!accepted && request==0xabcdef && !arm_calls && !abort_calls);return;
    }
    if(!high || !marker || !tick || race || !resource_ok) {
        assert(!accepted && request==0xabcdef && !leases);
        assert(abort_calls==(race==2 ? 1u:0u));
        vdc_dpll_manager_subdivision_output_status_t s;
        assert(vdc_dpll_manager_get_subdivision_window(&s));
        assert(s.mode==2 && s.batch.state==VDC_FIXED_OUTPUT_FAILED);
        assert(s.marker_high_ticks==(race==2 ? 50u:0u));return;
    }
    const uint32_t count=sync/interval;
    assert(accepted && request==1 && hardware.request.pulse_count==count);
    assert(hardware.request.period_ns==interval && admitted_marker==marker);
    vdc_dpll_manager_subdivision_output_status_t s,copy;
    memset(&s,0xa5,sizeof(s));copy=s;
    assert(!vdc_dpll_manager_get_subdivision_output(&s) && !memcmp(&s,&copy,sizeof(s)));
    assert(!vdc_dpll_manager_subdivision_output_stop() && !vdc_dpll_manager_fixed_output_stop());
    assert(!abort_calls && vdc_dpll_manager_get_subdivision_window(&s));
    assert(s.mode==2 && s.subdivision_count==count-2 && s.batch.pulse_count==count);
    assert(s.batch.period_ns*s.batch.pulse_count==sync && s.marker_high_ticks==50);
    if(!strcmp(mode,"busy")) {
        assert(!vdc_dpll_manager_fixed_output_start(1000000,1000,9,4,&request));
        assert(!vdc_dpll_manager_subdivision_output_start(10000,1000,8,2001,4,&request));
        assert(!vdc_dpll_manager_subdivision_window_start(sync,interval,high,marker,tick,&request));
        assert(arm_calls==1 && !abort_calls && hardware.pulse.running);
    } else if(!strcmp(mode,"changed")) {
        ++current.token;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_CANCELLED && abort_calls==1);
    } else if(!strcmp(mode,"complete")) {
        hardware.pulse.running=false;hardware.pulse.completed_pulses=count;
        hardware.pulse.pio_enabled=false;hardware.pulse.dma_busy=false;leases=0;
        vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_COMPLETE);
        assert(vdc_dpll_manager_subdivision_window_stop() && !abort_calls);
        assert(vdc_dpll_manager_subdivision_output_start(10000,1000,8,2001,4,&request));
        assert(request==2 && !vdc_dpll_manager_get_subdivision_window(&s));
        assert(!vdc_dpll_manager_subdivision_window_stop() && !abort_calls);
        assert(vdc_dpll_manager_subdivision_output_stop() && abort_calls==1);return;
    }
    assert(vdc_dpll_manager_subdivision_window_stop() && abort_calls==1 && !leases);
    assert(vdc_dpll_manager_subdivision_window_stop() && abort_calls==1);
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"window")){window_probe(argv[2]);return 0;}
    if(argc==3 && !strcmp(argv[1],"subdivision")){subdivision_probe(argv[2]);return 0;}
    if(argc==4 && !strcmp(argv[1],"probe")){probe((unsigned)atoi(argv[2]),argv[3]);return 0;}
    assert(argc==2);const char *mode=argv[1];
    current=(vdc_dpll_manager_committed_model_t){.token=2,.session=100,
        .dco={.valid=1,.dco_update_seq=3,.period_adjust_ppb=1563}};
    uint32_t request=0xabcdef;
    if(!strcmp(mode,"reason_reset")) {
        model_missing[1]=UINT_MAX;
        assert(!vdc_dpll_manager_fixed_output_start(1000000,2000,2048,4,&request));
        assert(request==0xabcdef && s_fixed_output.last_reason==VDC_FIXED_OUTPUT_PRE_MODEL_UNAVAILABLE);
        assert(s_fixed_output.request_id==1 && arm_calls==1 && !enables && !leases);
        phase=0;model_missing[1]=0;resource_ok=false;
        assert(!vdc_dpll_manager_fixed_output_start(1000000,2000,2048,4,&request));
        assert(request==0xabcdef && s_fixed_output.last_reason==VDC_FIXED_OUTPUT_RESOURCE_OR_ARGUMENT);
        assert(s_fixed_output.request_id==2 && arm_calls==2 && !enables && !leases);
        return 0;
    }
    if(!strcmp(mode,"core1"))core=1;
    else if(!strcmp(mode,"run"))stopped=false;
    else if(!strcmp(mode,"stale_session"))session=0;
    else if(!strcmp(mode,"session_mismatch"))session=101;
    else if(!strcmp(mode,"unavailable"))model_available=false;
    else if(!strcmp(mode,"empty_token"))current.token=0;
    else if(!strcmp(mode,"clock"))clock_ok=false;
    else if(!strcmp(mode,"selftest"))s_observation_self_test.active=true;
    else if(!strcmp(mode,"invalid_rate"))current.dco.period_adjust_ppb=-1000000000;
    else if(!strcmp(mode,"race_before"))race=1;
    else if(!strcmp(mode,"race_after"))race=2;
    else if(!strcmp(mode,"resource"))resource_ok=false;
    const bool short_period=!strcmp(mode,"short_period");
    const bool accepted=vdc_dpll_manager_fixed_output_start(
        short_period?999999:1000000,2000,1024,4,&request);
    if(core || !stopped || !session || session!=100 || !model_available ||
       !current.token || !clock_ok || s_observation_self_test.active ||
       current.dco.period_adjust_ppb<=-1000000000 || short_period) {
        assert(!accepted && request==0xabcdef && !arm_calls && !abort_calls);
        return 0;
    }
    if(race || !resource_ok) {
        assert(!accepted && request==0xabcdef && arm_calls==1);
        assert(abort_calls==(race==2 ? 1u:0u));
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_FAILED);
        return 0;
    }
    assert(accepted && request==1 && hardware.request.rate_ppb==1563);
    assert(s_fixed_output.state==VDC_FIXED_OUTPUT_RUNNING);
    assert(s_fixed_output.model.token==2 && s_fixed_output.model.dco.dco_update_seq==3);
    if(!strcmp(mode,"busy")) {
        assert(!vdc_dpll_manager_fixed_output_start(1,1,1,4,&request));
        assert(arm_calls==1 && !abort_calls && hardware.pulse.running);
    } else if(!strcmp(mode,"transient_model")) {
        model_available=false;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_RUNNING && !abort_calls);
        assert(s_fixed_output.model_unchanged==2);
        model_available=true;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_RUNNING && s_fixed_output.model_unchanged==1);
    } else if(!strcmp(mode,"transient_ring")) {
        ring_available=false;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_RUNNING && !abort_calls);
        ring_available=true;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_RUNNING && !abort_calls);
    } else if(!strcmp(mode,"new_failure")) {
        hardware.pulse.running=false;hardware.pulse.pio_enabled=false;
        hardware.pulse.completed_pulses=1024;hardware.pulse.transfer_count=0;
        vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_COMPLETE);
        resource_ok=false;current.token++;
        assert(!vdc_dpll_manager_fixed_output_start(1000000,2000,2048,4,&request));
        vdc_dpll_manager_fixed_output_status_t status;
        assert(vdc_dpll_manager_get_fixed_output(&status));
        assert(status.state==VDC_FIXED_OUTPUT_FAILED && status.request_id==2);
        assert(status.model.token==3 && status.completed_pulses==0);
        assert(!status.first_edge_ticks && !status.last_edge_ticks && !status.system_clock_hz);
    } else if(!strcmp(mode,"complete")) {
        hardware.pulse.running=false;hardware.pulse.pio_enabled=false;
        hardware.pulse.completed_pulses=1024;hardware.pulse.transfer_count=0;
        vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_COMPLETE);
        assert(vdc_dpll_manager_fixed_output_stop() && !abort_calls);
    } else if(!strcmp(mode,"changed")) {
        ++current.token;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_CANCELLED && abort_calls==1);
        assert(s_fixed_output.model.token==2 && !s_fixed_output.model_unchanged);
    } else if(!strcmp(mode,"start_tdma")) {
        stopped=false;vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_CANCELLED && abort_calls==1);
        vdc_dpll_manager_fixed_output_status_t untouched;memset(&untouched,0xa5,sizeof(untouched));
        const vdc_dpll_manager_fixed_output_status_t copy=untouched;
        assert(!vdc_dpll_manager_get_fixed_output(&untouched));
        assert(!memcmp(&untouched,&copy,sizeof(copy)));
    } else if(!strcmp(mode,"stop")) {
        assert(vdc_dpll_manager_fixed_output_stop());
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_CANCELLED && abort_calls==1);
        assert(vdc_dpll_manager_fixed_output_stop() && abort_calls==1);
    } else if(!strcmp(mode,"incomplete")) {
        hardware.pulse.running=false;hardware.pulse.completed_pulses=1000;
        vdc_fixed_output_service_core0();
        assert(s_fixed_output.state==VDC_FIXED_OUTPUT_FAILED);
    } else assert(!strcmp(mode,"accepted"));
    return 0;
}
'''
    return compile_executable(directory, "fixed_owner", harness)


@pytest.mark.parametrize('case', [
    'core1', 'run', 'stale_session', 'session_mismatch', 'unavailable',
    'empty_token', 'clock', 'selftest', 'invalid_rate', 'race_before',
    'race_after', 'resource', 'accepted', 'busy', 'complete', 'changed',
    'start_tdma', 'stop', 'incomplete', 'short_period', 'transient_model', 'transient_ring', 'new_failure',
    'reason_reset',
])
def test_fixed_owner(fixed_owner, case):
    run = subprocess.run([str(fixed_owner), case], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


@pytest.mark.parametrize('case', [
    'accepted', 'zero_count', 'overflow_count', 'oversize_count', 'max_count',
    'zero_tick', 'bad_marker', 'run', 'race_before', 'race_after', 'resource',
    'core1', 'fixed_busy', 'busy', 'changed', 'start_tdma', 'complete',
])
def test_subdivision_owner(fixed_owner, case):
    run = subprocess.run([str(fixed_owner), 'subdivision', case], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


@pytest.mark.parametrize('case', [
    'accepted', 'min_count', 'max_count', 'small_count', 'oversize_count',
    'overflow_count', 'nondivisible', 'zero_sync', 'zero_interval', 'zero_high',
    'zero_marker', 'zero_tick', 'run', 'core1', 'race_before', 'race_after',
    'resource', 'fixed_busy', 'subdiv_busy', 'busy', 'changed', 'complete',
])
def test_subdivision_window_owner(fixed_owner, case):
    run = subprocess.run([str(fixed_owner), 'window', case], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


@pytest.mark.parametrize('stage', [0, 1, 2], ids=['initial', 'pre_enable', 'post_arm'])
@pytest.mark.parametrize('case', [
    'ring_transient', 'model_transient', 'both_transient', 'ring_persistent', 'model_persistent',
    'ring_bad', 'config_bad', 'model_bad', 'miss_then_invalid', 'clock_bad', 'selftest_bad',
    'session_during', 'session_after', 'irq_after', 'first_valid_long_irq', 'valid_retry_long_irq',
    'first_unknown_long_irq', 'unknown_retry_long_irq', 'deadline_persistent', 'wrap',
    'next_read_expiry', 'next_read_last_us', 'last_attempt_success', 'session_between', 'late_invalid',
])
def test_fixed_admission_bounded_snapshot(fixed_owner, stage, case):
    run = subprocess.run([str(fixed_owner), 'probe', str(stage), case], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr


@pytest.mark.parametrize('stage,case', [(1, 'miss_then_changed'), (2, 'miss_then_changed'), (0, 'irq_before')])
def test_fixed_admission_preserves_selected_model_and_irq(fixed_owner, stage, case):
    run = subprocess.run([str(fixed_owner), 'probe', str(stage), case], capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr


def test_fixed_stop_scpi_ack(tmp_path):
    """A completed STOP must emit an ACK; busy must emit only an error."""
    source = (ROOT / 'middleware/scpi_port/src/scpi_system_snapshot_commands.c').read_text(encoding='utf-8')
    handler = source.split('scpi_result_t scpi_cmd_vdc_fixed_output_stop(', 1)[1]
    handler = 'scpi_result_t scpi_cmd_vdc_fixed_output_stop(' + handler.split(
        'static scpi_result_t scpi_vdc_output_status(', 1)[0]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
typedef int scpi_result_t;
typedef struct { unsigned ack, error; } scpi_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool ready;
static bool vdc_dpll_manager_fixed_output_stop(void) { return ready; }
static void scpi_port_push_exec_error(scpi_t *c, const char *e)
{ assert(!strcmp(e,"VDC_FIXED_OUTPUT_BUSY")); ++c->error; }
scpi_result_t scpi_port_result_ok(scpi_t *c) { ++c->ack; return SCPI_RES_OK; }
''' + handler + r'''
int main(void) {
    scpi_t c={0};
    assert(scpi_cmd_vdc_fixed_output_stop(&c)==SCPI_RES_ERR);
    assert(c.error==1 && c.ack==0);
    ready=true;
    assert(scpi_cmd_vdc_fixed_output_stop(&c)==SCPI_RES_OK);
    assert(c.error==1 && c.ack==1);
    assert(scpi_cmd_vdc_fixed_output_stop(&c)==SCPI_RES_OK);
    assert(c.error==1 && c.ack==2);
    return 0;
}
'''
    exe = compile_executable(tmp_path, 'fixed_stop_ack', harness)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_subdivision_scpi_wire_format_and_errors(tmp_path):
    """Exercise production handlers: exact schema, parameter order and ACKs."""
    source = (ROOT / 'middleware/scpi_port/src/scpi_system_snapshot_commands.c').read_text(encoding='utf-8')
    handlers = 'scpi_result_t scpi_cmd_vdc_fixed_output(' + source.split(
        'scpi_result_t scpi_cmd_vdc_fixed_output(', 1)[1].split(
        'scpi_result_t scpi_cmd_vdc_feedback_match_q(', 1)[0]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "vdc_dpll_manager.h"
typedef int scpi_result_t;
typedef struct {
    uint32_t args[5], arg_count, cursor, count, errors, acks;
    int64_t values[40];
} scpi_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool ready;
static unsigned starts;
static bool scpi_port_read_u32(scpi_t *c,uint32_t *v) {
    if(c->cursor==c->arg_count)return false;
    *v=c->args[c->cursor++];return true;
}
static void scpi_port_push_exec_error(scpi_t *c,const char *e) {
    assert(e && *e);++c->errors;
}
static scpi_result_t scpi_port_result_ok(scpi_t *c) { ++c->acks;return SCPI_RES_OK; }
static void SCPI_ResultUInt32(scpi_t *c,uint32_t v) { c->values[c->count++]=v; }
static void SCPI_ResultInt32(scpi_t *c,int32_t v) { c->values[c->count++]=v; }
static void SCPI_ResultUInt64(scpi_t *c,uint64_t v) { c->values[c->count++]=(int64_t)v; }
bool vdc_dpll_manager_fixed_output_start(uint32_t a,uint32_t b,uint32_t d,uint32_t e,uint32_t *id)
{(void)a;(void)b;(void)d;(void)e;(void)id;return false;}
bool vdc_dpll_manager_subdivision_output_start(uint32_t a,uint32_t b,uint32_t d,
    uint32_t e,uint32_t f,uint32_t *id) {
    ++starts;assert(a==10000 && b==1000 && d==8 && e==2001 && f==4);
    *id=77;return ready;
}
bool vdc_dpll_manager_subdivision_window_start(uint32_t a,uint32_t b,uint32_t d,
    uint32_t e,uint32_t f,uint32_t *id) {
    ++starts;assert(a==1000000 && b==1000 && d==100 && e==200 && f==4);
    *id=78;return ready;
}
bool vdc_dpll_manager_fixed_output_stop(void) { return ready; }
bool vdc_dpll_manager_subdivision_output_stop(void) { return ready; }
bool vdc_dpll_manager_subdivision_window_stop(void) { return ready; }
bool vdc_dpll_manager_get_fixed_output(vdc_dpll_manager_fixed_output_status_t *s) {
    if(!ready)return false;
    memset(s,0,sizeof(*s));s->schema=1;s->request_id=77;s->state=2;
    s->model.dco.period_adjust_ppb=-1000;s->period_ns=10000;s->high_ns=1000;
    s->pulse_count=9;s->tick_period_ns=4;s->completed_pulses=9;
    s->first_edge_ticks=2501;s->last_edge_ticks=22523;s->total_ticks=22773;
    s->model_unchanged=1;return true;
}
bool vdc_dpll_manager_get_subdivision_output(vdc_dpll_manager_subdivision_output_status_t *s) {
    if(!vdc_dpll_manager_get_fixed_output(&s->batch))return false;
    s->mode=1;s->marker_high_ns=2001;s->marker_high_ticks=501;s->subdivision_count=8;
    return true;
}
bool vdc_dpll_manager_get_subdivision_window(vdc_dpll_manager_subdivision_output_status_t *s) {
    if(!vdc_dpll_manager_get_fixed_output(&s->batch))return false;
    s->mode=2;s->marker_high_ns=200;s->marker_high_ticks=50;s->subdivision_count=998;
    s->batch.request_id=78;s->batch.period_ns=1000;s->batch.high_ns=100;
    s->batch.pulse_count=1000;s->batch.completed_pulses=1000;return true;
}
''' + handlers + r'''
int main(void) {
    for(unsigned n=0;n<5;++n) {
        scpi_t c={.args={10000,1000,8,2001,4},.arg_count=n};
        assert(scpi_cmd_vdc_subdivision_output(&c)==SCPI_RES_ERR);
        assert(c.errors==1 && !c.count && !starts);
    }
    scpi_t c={.args={10000,1000,8,2001,4},.arg_count=5};
    assert(scpi_cmd_vdc_subdivision_output(&c)==SCPI_RES_ERR);
    assert(c.errors==1 && !c.count && starts==1);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_output_q(&c)==SCPI_RES_ERR);
    assert(c.errors==1 && !c.count);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_output_stop(&c)==SCPI_RES_ERR);
    assert(c.errors==1 && !c.acks);
    ready=true;
    c=(scpi_t){.args={10000,1000,8,2001,4},.arg_count=5};
    assert(scpi_cmd_vdc_subdivision_output(&c)==SCPI_RES_OK);
    assert(!c.errors && c.count==1 && c.values[0]==77 && starts==2);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_output_stop(&c)==SCPI_RES_OK);
    assert(!c.errors && c.acks==1);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_output_q(&c)==SCPI_RES_OK);
    assert(c.count==37 && !c.errors);
    assert(c.values[0]==1 && c.values[1]==1 && c.values[2]==2001 &&
        c.values[3]==501 && c.values[4]==8);
    scpi_t fixed={0};assert(scpi_cmd_vdc_fixed_output_q(&fixed)==SCPI_RES_OK);
    assert(fixed.count==32 && !fixed.errors);
    assert(!memcmp(c.values+5,fixed.values,32*sizeof(int64_t)));
    assert(fixed.values[12]==-1000 && fixed.values[15]==10000 &&
        fixed.values[16]==1000 && fixed.values[17]==9 && fixed.values[18]==4);
    assert(fixed.values[24]==2501 && fixed.values[25]==22523 &&
        fixed.values[26]==22773 && fixed.values[27]==9 && fixed.values[31]==1);
    for(unsigned n=0;n<5;++n) {
        c=(scpi_t){.args={1000000,1000,100,200,4},.arg_count=n};
        assert(scpi_cmd_vdc_subdivision_window(&c)==SCPI_RES_ERR);
        assert(c.errors==1 && !c.count && starts==2);
    }
    ready=false;c=(scpi_t){.args={1000000,1000,100,200,4},.arg_count=5};
    assert(scpi_cmd_vdc_subdivision_window(&c)==SCPI_RES_ERR);
    assert(c.errors==1 && !c.count && starts==3);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_window_q(&c)==SCPI_RES_ERR);
    assert(c.errors==1 && !c.count);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_window_stop(&c)==SCPI_RES_ERR);
    assert(c.errors==1 && !c.acks);
    ready=true;c=(scpi_t){.args={1000000,1000,100,200,4},.arg_count=5};
    assert(scpi_cmd_vdc_subdivision_window(&c)==SCPI_RES_OK);
    assert(c.count==1 && c.values[0]==78 && starts==4);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_window_q(&c)==SCPI_RES_OK);
    assert(c.count==37 && !c.errors);
    assert(c.values[0]==1 && c.values[1]==2 && c.values[2]==200 &&
        c.values[3]==50 && c.values[4]==998);
    assert(c.values[5]==1 && c.values[6]==78 && c.values[20]==1000 &&
        c.values[21]==100 && c.values[22]==1000 && c.values[23]==4);
    c=(scpi_t){0};assert(scpi_cmd_vdc_subdivision_window_stop(&c)==SCPI_RES_OK);
    assert(c.acks==1 && !c.errors);
    return 0;
}
'''
    exe = compile_executable(tmp_path, 'subdivision_scpi', harness)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
