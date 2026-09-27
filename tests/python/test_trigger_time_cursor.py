"""Host coverage for the VDC sample sequence to local time cursor bridge."""
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

static uint32_t session=7;
static uint32_t model_reads;
static vdc_dpll_manager_committed_model_t model;
static vdc_dpll_manager_vdc_status_t vdc_status;
static vdc_dpll_manager_sync_event_t event;
static uint64_t model_offset;
uint32_t vdc_dpll_manager_feedback_session(void){return session;}
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out){
    ++model_reads;*out=model;return true;
}
void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *out){*out=vdc_status;}
bool vdc_dpll_manager_get_sync_event(vdc_dpll_manager_sync_event_t *out){*out=event;return true;}
bool vdc_domain_dco_local_to_output_ns(const vdc_dco_control_t *d,uint64_t local,uint64_t *out)
{(void)d;if(UINT64_MAX-local<model_offset)return false;*out=local+model_offset;return true;}

static void setup(void){memset(&event,0,sizeof(event));memset(&model,0,sizeof(model));
    model.session=session;model.token=11;model.dco.valid=1;model.dco.dco_update_seq=3;
    model.dco.tdma_schedule_crc32=0xabc;model.clock_epoch_id=3;model.clock_run_id=4;
    model.local_slot=1;model.valid_from_raw=0;
    vdc_status.ready=false;vdc_status.lock_state=VDC_DOMAIN_LOCK_OFF;vdc_status.sync_seq=0;}
static trigger_time_cursor_snapshot_t snap(void){trigger_time_cursor_snapshot_t s;assert(trigger_time_cursor_snapshot(&s));return s;}
static void observed(uint32_t local_seq,uint32_t carrier,uint64_t local_ns){
    vdc_status.sync_seq=local_seq;
    event=(vdc_dpll_manager_sync_event_t){.valid=1,.sync_seq=local_seq,
        .ring_config_seq=5,.carrier_sequence=carrier,.identity_crc32=77,
        .schedule_crc32=0xabc,.timestamp_flags=TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED,
        .clock_epoch_id=3,.clock_run_id=4,.local_slot=1,.local_rx_timestamp_ns=local_ns};}
int main(int argc,char **argv){assert(argc==2);setup();
    const trigger_time_grid_t grid={.anchor_vdc_ns=10000000,.epoch=2,
        .generation=4,.period_ns=1000000,.subdivisions=1000};
    assert(trigger_time_cursor_configure(&grid));
    if(!strcmp(argv[1],"no_sync")){trigger_time_cursor_service();assert(snap().valid==0&&snap().last_error==TRIGGER_TIME_CURSOR_NO_SYNC);return 0;}
    if(!strcmp(argv[1],"no_lock")){vdc_status.ready=true;vdc_status.lock_state=VDC_DOMAIN_LOCK_CHECKING;observed(10,41,10000000);trigger_time_cursor_service();assert(snap().valid==0&&snap().last_error==TRIGGER_TIME_CURSOR_NO_LOCK&&model_reads==0);return 0;}
    vdc_status.ready=true;vdc_status.lock_state=VDC_DOMAIN_LOCK_LOCKED;
    observed(!strcmp(argv[1],"different_local_seq")?91u:10u,41u,10000000u);
    if(!strcmp(argv[1],"model_late"))model.valid_from_raw=2500001u;
    trigger_time_cursor_service();
    if(!strcmp(argv[1],"model_late")){assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_MODEL_TIME);return 0;}
    {trigger_time_cursor_snapshot_t s=snap();assert(s.valid&&s.session==7&&s.pulse_index==1&&s.action_cursor==0&&s.carrier_sequence==41&&s.identity_crc32==77);}
    if(!strcmp(argv[1],"same_seq_changed")){event.carrier_sequence=99;trigger_time_cursor_service();assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_IDENTITY);return 0;}
    if(!strcmp(argv[1],"different_local_seq"))return 0;
    if(!strcmp(argv[1],"step")){observed(11,42,11000100);trigger_time_cursor_service();
        trigger_time_cursor_snapshot_t s=snap();assert(s.valid&&s.pulse_index==2&&s.action_cursor==1000&&s.sample_seq==11);return 0;}
    if(!strcmp(argv[1],"phase")){observed(11,42,11100000);trigger_time_cursor_service();assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_GRID_PHASE);return 0;}
    if(!strcmp(argv[1],"identity")){observed(11,42,11000000);event.identity_crc32=0;trigger_time_cursor_service();assert(!snap().valid&&snap().last_error==TRIGGER_TIME_CURSOR_IDENTITY);return 0;}
    if(!strcmp(argv[1],"previous_model")){observed(11,42,11000000);model.token=12;model.valid_from_raw=3000000;trigger_time_cursor_service();assert(snap().valid&&snap().model_token==11);return 0;}
    if(!strcmp(argv[1],"gap")){observed(13,44,13000000);trigger_time_cursor_service();
        trigger_time_cursor_snapshot_t s=snap();assert(!s.valid&&s.missed_sync_count==2&&s.last_error==TRIGGER_TIME_CURSOR_SAMPLE_GAP);
        observed(14,45,14000000);trigger_time_cursor_service();s=snap();
        assert(!s.valid&&s.pulse_index==1&&s.last_error==TRIGGER_TIME_CURSOR_SAMPLE_GAP);
        session=8;model.session=8;observed(1,46,10000000);trigger_time_cursor_service();
        s=snap();assert(s.valid&&s.session==8&&s.pulse_index==1);return 0;}
    if(!strcmp(argv[1],"session")){session=8;model.session=8;observed(1,46,10000000);trigger_time_cursor_service();
        trigger_time_cursor_snapshot_t s=snap();assert(s.valid&&s.session==8&&s.pulse_index==1&&s.sample_seq==1);return 0;}
    return 2;}
'''


@pytest.fixture(scope="module")
def cursor(tmp_path_factory):
    d = tmp_path_factory.mktemp("time-cursor")
    (d / "board_config.h").write_text(
        "#define BOARD_SYS_CLOCK_HZ 250000000u\n", encoding="utf-8")
    return compile_grid(d, "cursor", HARNESS,
        [ROOT / "components/sync_trigger/src/trigger_time_cursor.c"])


@pytest.mark.parametrize("case", ["no_sync", "no_lock", "step", "phase", "identity",
                                  "previous_model", "model_late", "same_seq_changed",
                                  "different_local_seq", "gap", "session"])
def test_cursor(cursor, case):
    result = subprocess.run([str(cursor), case], capture_output=True,
        text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
