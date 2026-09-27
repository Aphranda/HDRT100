"""The T2 cursor uses local committed DCO time, independently of RX polling."""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "trigger_time_cursor.h"
#include "vdc_dpll_manager.h"
#include "tdma_runtime_owner.h"

static uint32_t session=7;
static uint64_t now_raw=2500000;
static bool model_ok=true, ring_ok=true, clock_ok=true, session_race;
static vdc_dpll_manager_committed_model_t model;
static tdma_ring_clock_snapshot_t ring;
/* VDC observer state can remain absent, or change sparsely. The cursor must
 * not read this legacy observation sequence or PI state at all. */
static vdc_dpll_manager_vdc_status_t vdc_status;
uint32_t vdc_dpll_manager_feedback_session(void){return session;}
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out){
    *out=model;return model_ok;
}
void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *out)
{(void)out;assert(!"cursor must not consume ring observation status");}
bool vdc_dpll_manager_get_sync_event(vdc_dpll_manager_sync_event_t *out)
{(void)out;assert(!"cursor must not consume ring observation events");return false;}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{*out=ring;return ring_ok;}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{assert(hz==250000000u);*out=now_raw;if(session_race)++session;return clock_ok;}
/* Cursor control-flow fixture: only zero-rate affine phase is modeled here.
 * The production fast projection is independently checked against Domain. */
bool trigger_time_projection_fast(const vdc_dco_control_t *d,uint64_t local,uint64_t *out)
{
    if(d->phase_offset_ns<0){
        uint64_t offset=(uint64_t)(-(int64_t)d->phase_offset_ns);
        if(local<offset)return false;
        *out=local-offset;
    }else{
        if(UINT64_MAX-local<(uint32_t)d->phase_offset_ns)return false;
        *out=local+(uint32_t)d->phase_offset_ns;
    }
    return true;
}
static const trigger_time_grid_t grid={.anchor_vdc_ns=10000000,.epoch=2,
    .generation=4,.period_ns=1000000,.subdivisions=2000};
static void setup(void){
    model=(vdc_dpll_manager_committed_model_t){.session=session,.token=11,
        .role_generation=2,.clock_epoch_id=3,.clock_run_id=4,.local_slot=1,
        .dco={.valid=1,.dco_update_seq=3,.nominal_period_ns=1000000,
            .tdma_schedule_crc32=0xabc,.lock_state=VDC_DOMAIN_LOCK_CHECKING}};
    ring=(tdma_ring_clock_snapshot_t){.enabled=true,.adapter_started=true,.data_enabled=true,
        .config_seq=5,.applied_config_seq=5,.local_slot_id=1,.schedule_crc32=0xabc};
    assert(trigger_time_cursor_configure(&grid));
}
static trigger_time_cursor_snapshot_t snap(void){
    trigger_time_cursor_snapshot_t s;assert(trigger_time_cursor_snapshot(&s));return s;
}
static void at(uint64_t local_ns){assert(local_ns%4==0);now_raw=local_ns/4;trigger_time_cursor_service();}
static void fresh_session(void){
    ++session;model.session=session;++model.token;now_raw=2500000;model.dco.phase_offset_ns=0;
}
int main(int argc,char **argv){assert(argc==2);setup();const char *c=argv[1];
    if(!strcmp(c,"unconfigured")){trigger_time_cursor_clear();trigger_time_cursor_service();assert(!snap().configured);return 0;}
    if(!strcmp(c,"no_session")){session=0;trigger_time_cursor_service();assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_NO_SYNC);return 0;}
    if(!strcmp(c,"no_model")){model_ok=false;trigger_time_cursor_service();assert(!snap().valid);model_ok=true;}
    if(!strcmp(c,"missing_ring")){ring_ok=false;trigger_time_cursor_service();assert(!snap().valid);ring_ok=true;}
    if(!strcmp(c,"pending_ring")){ring.applied_config_seq=4;trigger_time_cursor_service();assert(!snap().valid);ring.applied_config_seq=5;}
    if(!strcmp(c,"model_late")){
        model.valid_from_raw=now_raw+1;trigger_time_cursor_service();
        assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_MODEL_TIME);
        ++now_raw;trigger_time_cursor_service();assert(snap().valid);return 0;
    }
    if(!strcmp(c,"before_anchor")){
        at(9999996);assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_GRID_PHASE);
        at(10000000);assert(snap().valid);return 0;
    }
    if(!strcmp(c,"session_race")){
        session_race=true;trigger_time_cursor_service();assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_SESSION);return 0;
    }
    trigger_time_cursor_service();
    assert(snap().valid&&snap().session==7&&snap().pulse_index==1&&snap().action_cursor==0);
    assert(snap().sample_seq==1&&snap().last_event_id==0&&snap().carrier_sequence==0&&snap().identity_crc32==0);
    if(!strcmp(c,"same_token_command")) {
        ++model.applied_command_seq;
        at(11000000);assert(snap().valid&&snap().model_token==11&&snap().action_cursor==2000);
        return 0;
    }
    if(!strcmp(c,"bound_model_unavailable")) {
        model_ok=false;at(11000000);
        assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_NO_SYNC);
        model_ok=true;at(12000000);
        assert(snap().valid&&snap().model_token==11&&snap().sample_seq==3);return 0;
    }
    if(!strcmp(c,"zero_token") || !strcmp(c,"model_seq_missing") ||
       !strcmp(c,"model_period_missing") || !strcmp(c,"rate_invalid") ||
       !strcmp(c,"state_invalid")) {
        ++model.token;
        if(!strcmp(c,"zero_token"))model.token=0;
        if(!strcmp(c,"model_seq_missing"))model.dco.dco_update_seq=0;
        if(!strcmp(c,"model_period_missing"))model.dco.nominal_period_ns=0;
        if(!strcmp(c,"rate_invalid"))model.dco.period_adjust_ppb=-1000000000;
        if(!strcmp(c,"state_invalid"))model.dco.lock_state=VDC_DOMAIN_LOCK_FAULT+1;
        at(11000000);assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_NO_SYNC);
        assert(snap().model_token==11); // a rejected candidate never becomes a cache hit
        at(12000000);assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_NO_SYNC);
        model.token=13;model.dco.dco_update_seq=4;model.dco.nominal_period_ns=1000000;
        model.dco.period_adjust_ppb=0;model.dco.lock_state=VDC_DOMAIN_LOCK_CHECKING;
        at(13000000);assert(snap().valid&&snap().model_token==13);return 0;
    }
    if(!strcmp(c,"no_rx")||!strcmp(c,"sparse_rx")){
        at(11500000);assert(snap().valid&&snap().action_cursor==3000&&snap().sample_seq==2);
        if(!strcmp(c,"sparse_rx"))vdc_status.sync_seq=123;
        at(13000000);assert(snap().valid&&snap().action_cursor==6000&&snap().sample_seq==4&&snap().missed_sync_count==1);
        at(14500000);assert(snap().valid&&snap().action_cursor==9000&&snap().sample_seq==5);return 0;
    }
    if(!strcmp(c,"fine_floor")){
        at(10000496);assert(snap().valid&&snap().fine_slice_index==0&&snap().sample_seq==1);
        at(10000500);assert(snap().fine_slice_index==1&&snap().action_cursor==1&&snap().sample_seq==1);
        at(10999996);assert(snap().fine_slice_index==1999&&snap().action_cursor==1999);
        at(11000000);assert(snap().fine_slice_index==0&&snap().action_cursor==2000&&snap().sample_seq==2);return 0;
    }
    if(!strcmp(c,"uneven_grid")){
        trigger_time_grid_t uneven=grid;uneven.subdivisions=3;assert(trigger_time_cursor_configure(&uneven));
        at(10333332);assert(snap().fine_slice_index==0);
        at(10333336);assert(snap().fine_slice_index==1);return 0;
    }
    if(!strcmp(c,"model_update")){
        ++model.token;++model.dco.dco_update_seq;model.dco.phase_offset_ns=500;model.valid_from_raw=2750000;
        at(11000000);assert(snap().valid&&snap().model_token==12&&snap().action_cursor==2001&&snap().sample_seq==2);return 0;
    }
    if(!strcmp(c,"late_update")){
        ++model.token;model.valid_from_raw=3000000;
        at(11000000);assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_MODEL_TIME&&snap().model_token==11);
        at(12000000);assert(snap().valid&&snap().model_token==12&&snap().action_cursor==4000);return 0;
    }
    if(!strcmp(c,"clock_unavailable")){
        clock_ok=false;at(11000000);assert(!snap().valid);
        clock_ok=true;at(12000000);assert(snap().valid&&snap().sample_seq==3);return 0;
    }
    if(!strcmp(c,"session")){
        at(12000000);fresh_session();trigger_time_cursor_service();
        assert(snap().valid&&snap().session==8&&snap().sample_seq==1&&snap().action_cursor==0&&snap().missed_sync_count==0);return 0;
    }
    if(!strcmp(c,"backward")||!strcmp(c,"phase_backstep")){
        at(11000500);
        if(!strcmp(c,"phase_backstep")){++model.token;model.dco.phase_offset_ns=-1000;at(11001000);}
        else at(11000496);
        assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_BACKWARD);
        at(13000000);assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_BACKWARD);
        fresh_session();trigger_time_cursor_service();assert(snap().valid&&snap().action_cursor==0);return 0;
    }
    /* Real committed publication mints a fresh token for each of these changes. */
    if(!strcmp(c,"role")){++model.role_generation;++model.token;}
    else if(!strcmp(c,"epoch")){++model.clock_epoch_id;++model.token;}
    else if(!strcmp(c,"run")){++model.clock_run_id;++model.token;}
    else if(!strcmp(c,"slot")){++model.local_slot;++ring.local_slot_id;++model.token;}
    else if(!strcmp(c,"schedule")){++model.dco.tdma_schedule_crc32;++ring.schedule_crc32;++model.token;}
    else if(!strcmp(c,"ring_config")){++ring.config_seq;++ring.applied_config_seq;}
    else if(!strcmp(c,"ring_slot"))++ring.local_slot_id;
    else if(!strcmp(c,"ring_crc"))++ring.schedule_crc32;
    else if(!strcmp(c,"stop"))ring.enabled=false;
    else if(!strcmp(c,"period")){++model.dco.nominal_period_ns;++model.token;}
    else if(!strcmp(c,"overflow")){
        trigger_time_grid_t huge=grid;huge.period_ns=UINT32_MAX;huge.subdivisions=UINT32_MAX;
        huge.anchor_vdc_ns=0;model.dco.nominal_period_ns=UINT32_MAX;++model.token;
        assert(trigger_time_cursor_configure(&huge));at(0);at(UINT64_C(18446744073709551612));
        assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_OVERFLOW);return 0;
    }
    else return !strcmp(c,"no_model")||!strcmp(c,"missing_ring")||!strcmp(c,"pending_ring")?0:2;
    at(11000000);assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_IDENTITY);
    at(12000000);assert(!snap().valid);
    /* Reconfigure explicitly creates a new local binding, after STOP. */
    ring.enabled=true;model.dco.nominal_period_ns=grid.period_ns;++model.token;
    model.local_slot=ring.local_slot_id;model.dco.tdma_schedule_crc32=ring.schedule_crc32;
    assert(trigger_time_cursor_configure(&grid));at(13000000);assert(snap().valid);
    return 0;
}
'''


@pytest.fixture(scope="module")
def cursor(tmp_path_factory):
    d = tmp_path_factory.mktemp("time-cursor")
    (d / "board_config.h").write_text(
        "#define BOARD_SYS_CLOCK_HZ 250000000u\n", encoding="utf-8")
    (d / "tdma_runtime_owner.h").write_text(
        '#include "tdma_service.h"\n'
        'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n',
        encoding="utf-8")
    return compile_grid(d, "cursor", HARNESS,
        [ROOT / "components/sync_trigger/src/trigger_time_cursor.c"])


@pytest.mark.parametrize("case", [
    "unconfigured", "no_session", "no_model", "missing_ring", "pending_ring",
    "model_late", "before_anchor", "session_race", "no_rx", "sparse_rx",
    "fine_floor", "uneven_grid", "model_update", "late_update", "clock_unavailable",
    "session", "backward", "phase_backstep", "role", "epoch", "run", "slot",
    "schedule", "ring_config", "stop", "period", "overflow",
    "same_token_command", "bound_model_unavailable", "zero_token", "model_seq_missing",
    "model_period_missing", "rate_invalid", "state_invalid", "ring_slot", "ring_crc",
])
def test_cursor(cursor, case):
    result = subprocess.run([str(cursor), case], capture_output=True,
        text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
