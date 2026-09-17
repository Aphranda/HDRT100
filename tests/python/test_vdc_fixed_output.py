"""Execute fixed-output owner admission, model races and cancellation paths."""
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable, function_body


@pytest.fixture(scope="module")
def fixed_owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-fixed-output")
    owner = (ROOT / 'components/vdc_dpll_manager/src/vdc_fixed_output.inc').read_text(encoding='utf-8')
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
int main(int argc,char **argv)
{
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
        'scpi_result_t scpi_cmd_vdc_fixed_output_q(', 1)[0]
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
