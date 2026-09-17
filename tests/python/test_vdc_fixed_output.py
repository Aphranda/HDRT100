"""Execute fixed-output owner admission, model races and cancellation paths."""
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def fixed_owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-fixed-output")
    harness = r'''
#include <assert.h>
#include <limits.h>
#include <string.h>
#include "vdc_dpll_manager.h"
''' + '#include "' + (ROOT / 'components/sync_io/inc/sync_io.h').as_posix() + '"\n' + r'''
#define BOARD_SYS_CLOCK_HZ 250000000u
static unsigned core, arm_calls, abort_calls, race;
static bool stopped=true, clock_ok=true, model_available=true, resource_ok=true;
static bool ring_available=true;
static uint32_t session=100;
static vdc_dpll_manager_committed_model_t current;
static sync_io_fixed_rate_runtime_t hardware;
static struct { bool active; } s_observation_self_test;
static unsigned get_core_num(void) { return core; }
bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{ if(!ring_available)return false;memset(out,0,sizeof(*out));out->enabled=!stopped;return true; }
bool vdc_timestamp_clock_read_bridge_diagnostic(uint32_t hz,
    vdc_timestamp_clock_bridge_diagnostic_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);memset(out,0,sizeof(*out));
  out->configuration_supported=clock_ok;out->clock_ready=clock_ok;return true; }
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ if(!model_available)return false;*out=current;return true; }
bool sync_io_sma_observer_fixed_rate_arm(const sync_io_rate_schedule_request_t *req,
    sync_io_fixed_rate_runtime_t *out,bool(*validate)(void*),void *context)
{
    ++arm_calls;
    if(!resource_ok)return false;
    if(race==1)++current.token;
    if(!validate(context))return false;
    memset(&hardware,0,sizeof(hardware));
    hardware.configured=true;hardware.request=*req;
    hardware.pulse.running=true;hardware.pulse.pio_enabled=true;
    hardware.pulse.total_pulses=req->pulse_count;
    hardware.pulse.transfer_count=req->pulse_count*2;
    hardware.system_clock_hz=BOARD_SYS_CLOCK_HZ;
    hardware.pio_divider256=256;
    *out=hardware;
    if(race==2)++current.dco.dco_update_seq;
    return true;
}
void sync_io_sma_observer_fixed_rate_disarm(void)
{ ++abort_calls;hardware.pulse.running=false;hardware.pulse.pio_enabled=false;
  hardware.pulse.dma_busy=false;hardware.pulse.transfer_count=0; }
void sync_io_sma_observer_fixed_rate_get_runtime(sync_io_fixed_rate_runtime_t *out)
{ *out=hardware; }
''' + (ROOT / 'components/vdc_dpll_manager/src/vdc_fixed_output.inc').read_text(encoding='utf-8') + r'''
int main(int argc,char **argv)
{
    assert(argc==2);const char *mode=argv[1];
    current=(vdc_dpll_manager_committed_model_t){.token=2,.session=100,
        .dco={.valid=1,.dco_update_seq=3,.period_adjust_ppb=1563}};
    uint32_t request=0xabcdef;
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
])
def test_fixed_owner(fixed_owner, case):
    run = subprocess.run([str(fixed_owner), case], capture_output=True, text=True)
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
