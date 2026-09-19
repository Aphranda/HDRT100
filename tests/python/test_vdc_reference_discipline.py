"""Execute slow discipline ownership with controlled hardware-window inputs.

Domain arithmetic is exercised independently by test_vdc_reference_baseline.
Here the actuator stub records every admission and asserts absolute targets.
"""
import subprocess
import pytest
from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def discipline_host(tmp_path_factory):
    fragment = (ROOT / "components/vdc_dpll_manager/src/vdc_reference_discipline.inc").read_text(encoding="utf-8")
    text = HARNESS.replace("// DISCIPLINE", fragment)
    return compile_executable(tmp_path_factory.mktemp("reference-discipline"), "discipline", text)


@pytest.mark.parametrize("case", ["track", "negative", "stop", "origin_epoch", "role", "session",
    "epoch", "run", "config", "profile", "servo", "timeout", "stale", "generation",
    "range", "cancel", "duplicate", "old_window", "reject", "disabled", "rearm", "no_origin", "stale_origin",
    "slow", "freeze", "limits", "bypass", "filter_extreme"])
def test_reference_discipline_owner(discipline_host, case):
    result = subprocess.run([str(discipline_host), case], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "vdc_reference.h"
#include "tdma_origin_plan.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
static vdc_domain_context_t s_vdc_domain;
static vdc_domain_snapshot_t s_published_snapshot;
static uint32_t s_published_snapshot_guard;
static bool s_published_snapshot_valid=true;
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static tdma_ring_clock_snapshot_t ring;
static tdma_origin_raw_reference_t origin;
static sync_io_reference_snapshot_t monitor;
static uint32_t s_reference_discipline_request=3, s_reference_discipline_capture_generation=9;
static uint32_t s_reference_discipline_armed[5]={1,1234,100,4,10000};
static uint32_t session=7, calls;
static uint64_t raw=10000000000ull;
static bool allow=true, origin_available=true, origin_stale=false;
bool sync_io_reference_get_snapshot(sync_io_reference_snapshot_t *out){*out=monitor;return true;}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out){assert(hz==BOARD_SYS_CLOCK_HZ);*out=raw;return true;}
static uint64_t vdc_dpll_manager_now_ns(void){return raw*4u;}
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out){*out=ring;return true;}
static bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out){*out=origin;return origin_available;}
bool vdc_domain_apply_reference_baseline(vdc_domain_context_t *c,int32_t target,uint64_t now){
    assert(c==&s_vdc_domain && now==raw*4u && (s_reference_discipline_request&1u));
    ++calls;if(!allow)return false;
    if(c->reference_baseline_ppb!=target){c->reference_baseline_ppb=target;++c->dco.dco_update_seq;}
    return true;
}
// DISCIPLINE
static void tick(void){
    raw+=260000000ull;monitor.sample_seq++;monitor.completed_raw=raw-100;
    origin.timer_lower=origin.timer_upper=raw-(origin_stale?1000000000:1000);++origin.sequence;
    monitor.end_raw32=(uint32_t)(raw-1000);monitor.elapsed_ticks=250000000;
    reference_discipline_service_core1(session);
}
static void fixture(void){
    s_vdc_domain.ready=1;s_vdc_domain.control.profile.valid=1;
    s_vdc_domain.control.profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;
    s_vdc_domain.control.profile.generation=2;s_vdc_domain.clock.epoch_id=3;
    s_vdc_domain.clock.run_id=4;s_vdc_domain.schedule.schedule_crc32=123;
    s_vdc_domain.servo.servo_profile_crc32=456;
    ring.enabled=ring.adapter_started=1;ring.config_seq=ring.applied_config_seq=8;ring.schedule_crc32=123;
    origin.epoch=5;origin.published_version=2;origin.tick_hz=BOARD_SYS_CLOCK_HZ;
    monitor=(sync_io_reference_snapshot_t){.state=SYNC_IO_REFERENCE_RUNNING,.generation=9,.valid=1,
        .tick_hz=BOARD_SYS_CLOCK_HZ,.frequency_error_ppb=4000,.pio_bias_ticks=1,
        .config={.window_ms=1000,.timeout_ms=2500}};
    tick();assert(!calls && s_reference_discipline.session==7);
    tick();assert(calls==1 && s_reference_discipline.baseline_ppb==100);
    assert(s_reference_discipline.config_generation==1 && s_reference_discipline.config_crc32==1234);
}
int main(int argc,char **argv){
    assert(argc==2);fixture();const char *mode=argv[1];
    if(!strcmp(mode,"slow") || !strcmp(mode,"freeze")){
        s_reference_discipline_request=7;
        s_reference_discipline_armed[2]=!strcmp(mode,"slow")?50:0;
        tick();const int32_t before=s_vdc_domain.reference_baseline_ppb;
        tick();assert(s_vdc_domain.reference_baseline_ppb==before+(int32_t)s_reference_discipline_armed[2]);
        /* The applied profile is immutable until a new arm is published. */
        s_reference_discipline_armed[2]=999;
        tick();assert(s_vdc_domain.reference_baseline_ppb==before+2*(int32_t)s_reference_discipline.config.slew_ppb_per_s);
        return 0;
    }
    if(!strcmp(mode,"limits") || !strcmp(mode,"bypass") || !strcmp(mode,"filter_extreme")){
        s_reference_discipline_request=7;
        s_reference_discipline_armed[2]=UINT32_MAX;
        s_reference_discipline_armed[3]=!strcmp(mode,"filter_extreme")?UINT32_MAX:0;
        s_reference_discipline_armed[4]=UINT32_MAX;
        tick();monitor.frequency_error_ppb=INT32_MIN;tick();
        assert(s_reference_discipline.filtered_ppb==INT32_MIN);
        monitor.frequency_error_ppb=INT32_MAX;tick();
        if(!strcmp(mode,"filter_extreme"))assert(s_reference_discipline.filtered_ppb==INT32_MIN+1);
        else assert(s_reference_discipline.filtered_ppb==INT32_MAX);
        if(!strcmp(mode,"bypass")){
            monitor.frequency_error_ppb=4000;tick();
            assert(s_reference_discipline.filtered_ppb==4000);
        }
        assert(s_reference_discipline.rejected==0);return 0;
    }
    if(!strcmp(mode,"track") || !strcmp(mode,"negative")){
        const int sign=!strcmp(mode,"negative")?-1:1;
        monitor.frequency_error_ppb=4000*sign;
        for(unsigned i=0;i<100;++i)tick();
        assert(s_vdc_domain.reference_baseline_ppb==4000*sign);
        const unsigned accepted=s_reference_discipline.accepted;
        for(unsigned i=0;i<20;++i)reference_discipline_service_core1(session);
        assert(s_reference_discipline.accepted==accepted);
        vdc_reference_discipline_status_t out;assert(vdc_dpll_manager_get_reference_discipline(&out));
        assert(out.baseline_ppb==4000*sign && out.enabled && out.origin_epoch==5);
        s_reference_discipline_guard|=1;assert(!vdc_dpll_manager_get_reference_discipline(&out));return 0;
    }
    const unsigned previous=calls;
    if(!strcmp(mode,"stop"))ring.enabled=0;
    else if(!strcmp(mode,"origin_epoch"))++origin.epoch;
    else if(!strcmp(mode,"no_origin"))origin_available=false;
    else if(!strcmp(mode,"stale_origin"))origin_stale=true;
    else if(!strcmp(mode,"role"))++s_vdc_domain.control.profile.generation;
    else if(!strcmp(mode,"session"))++session;
    else if(!strcmp(mode,"epoch"))++s_vdc_domain.clock.epoch_id;
    else if(!strcmp(mode,"run"))++s_vdc_domain.clock.run_id;
    else if(!strcmp(mode,"config"))ring.config_seq=++ring.applied_config_seq;
    else if(!strcmp(mode,"profile"))++owner.ring_runtime.ring_profile_crc32;
    else if(!strcmp(mode,"servo"))++s_vdc_domain.servo.servo_profile_crc32;
    else if(!strcmp(mode,"timeout"))monitor.state=SYNC_IO_REFERENCE_RETIRED;
    else if(!strcmp(mode,"generation"))++monitor.generation;
    else if(!strcmp(mode,"range"))monitor.frequency_error_ppb=10001;
    else if(!strcmp(mode,"reject"))allow=false;
    else if(!strcmp(mode,"cancel") || !strcmp(mode,"disabled"))s_reference_discipline_request=4;
    else if(!strcmp(mode,"rearm")){
        s_reference_discipline_request=7;
        tick();assert(calls==previous && s_reference_discipline.accepted==0);
        tick();assert(calls==previous+1 && s_vdc_domain.reference_baseline_ppb==200);return 0;
    }
    else if(!strcmp(mode,"duplicate")){
        reference_discipline_service_core1(session);assert(calls==previous);return 0;
    }
    else if(!strcmp(mode,"stale")){
        raw+=1000000000;reference_discipline_service_core1(session);
        assert(calls==previous && s_reference_discipline.state==VDC_REFERENCE_DISCIPLINE_HOLD);return 0;
    }
    else if(!strcmp(mode,"old_window")){
        ++monitor.sample_seq;monitor.completed_raw=s_reference_bound_raw+1000;
        monitor.end_raw32=(uint32_t)monitor.completed_raw;
        reference_discipline_service_core1(session);assert(calls==previous);return 0;
    }else assert(false);
    tick();assert(calls==previous+(!allow?1u:0u));
    assert(s_vdc_domain.reference_baseline_ppb==100);
    assert(s_reference_discipline.state!=VDC_REFERENCE_DISCIPLINE_TRACK);
    if(s_reference_discipline.state==VDC_REFERENCE_DISCIPLINE_RETIRED){
        ring.enabled=1;for(unsigned i=0;i<3;++i)tick();assert(calls==previous);
    }
    return 0;
}
'''
