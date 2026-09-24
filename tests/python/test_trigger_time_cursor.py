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
static vdc_dpll_manager_sync_io_observer_status_t observer;
static vdc_dpll_manager_committed_model_t model;
static vdc_dpll_manager_vdc_status_t vdc_status;
uint32_t vdc_dpll_manager_feedback_session(void){return session;}
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out){
    *out=model;return true;
}
void vdc_dpll_manager_get_sync_io_observer_status(
    vdc_dpll_manager_sync_io_observer_status_t *out){*out=observer;}
void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *out){*out=vdc_status;}

static void setup(void){memset(&observer,0,sizeof(observer));memset(&model,0,sizeof(model));
    model.session=session;model.token=11;model.dco.valid=1;model.dco.dco_update_seq=3;
    vdc_status.ready=false;vdc_status.sync_seq=0;}
static trigger_time_cursor_snapshot_t snap(void){trigger_time_cursor_snapshot_t s;assert(trigger_time_cursor_snapshot(&s));return s;}
int main(int argc,char **argv){assert(argc==2);setup();assert(trigger_time_cursor_configure(2,4,1000));
    if(!strcmp(argv[1],"no_sync")){trigger_time_cursor_service();assert(snap().valid==0&&snap().last_error==TRIGGER_TIME_CURSOR_NO_SYNC);return 0;}
    vdc_status.ready=true;vdc_status.sync_seq=10;observer.enabled=true;observer.last_sample_seq=10;observer.last_event_id=41;trigger_time_cursor_service();
    {trigger_time_cursor_snapshot_t s=snap();assert(s.valid&&s.session==7&&s.pulse_index==1&&s.action_cursor==0&&s.sample_seq==10);}
    if(!strcmp(argv[1],"step")){vdc_status.sync_seq=11;observer.last_event_id=42;trigger_time_cursor_service();
        trigger_time_cursor_snapshot_t s=snap();assert(s.valid&&s.pulse_index==2&&s.action_cursor==1000&&s.sample_seq==11);return 0;}
    if(!strcmp(argv[1],"gap")){vdc_status.sync_seq=13;trigger_time_cursor_service();
        trigger_time_cursor_snapshot_t s=snap();assert(!s.valid&&s.missed_sync_count==2&&s.last_error==TRIGGER_TIME_CURSOR_SAMPLE_GAP);return 0;}
    if(!strcmp(argv[1],"session")){session=8;model.session=8;vdc_status.sync_seq=1;trigger_time_cursor_service();
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


@pytest.mark.parametrize("case", ["no_sync", "step", "gap", "session"])
def test_cursor(cursor, case):
    result = subprocess.run([str(cursor), case], capture_output=True,
        text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
