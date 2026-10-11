"""Local cursor to independent action event expansion."""
import subprocess
import pytest
from test_trigger_grid_preview import ROOT, compile_grid

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_action_runtime.h"
int main(int argc,char**argv){assert(argc==2);trigger_deployment_t d={.generation=2,.rows={{.board_uid=1,.tdma_slot=0,.input_mask=1},{.board_uid=2,.tdma_slot=1,.output_mask=7},{.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},{.board_uid=4,.tdma_slot=3,.output_mask=1}}};trigger_action_template_table_t t={.generation=3,.count=2,.row={{.template_id=1,.generation=3,.role=1,.kind=1,.argument=1,.span_slices=1,.delta_q=-2},{.template_id=2,.generation=3,.role=3,.kind=1,.argument=1,.span_slices=1,.delta_q=3}}};trigger_action_runtime_t r={0};assert(trigger_action_runtime_configure(&r,&t,&d,trigger_action_template_table_crc(&t))==0);trigger_position_descriptor_t x={ /* versioned schema symbol, never a literal */
 .schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,.run_id=1,.sync_epoch=2,.position_id=7,.count_epoch=8,.target_count=9,.prediction_version=10,.target_ms_index=10,.target_fine_slice_index=2,.grid_generation=11,.deployment_generation=12,.deployment_crc=13,.valid_from_sync=1,.expiry_sync=2};
 if(!strcmp(argv[1],"order")){assert(trigger_action_runtime_stage(&r,&x,4)==0);trigger_action_runtime_event_t e[2];uint32_t n;assert(trigger_action_runtime_advance(&r,40,e,2,&n)==0&&n==1&&e[0].template_id==1);assert(trigger_action_runtime_advance(&r,45,e,2,&n)==0&&n==1&&e[0].template_id==2);return 0;}
 if(!strcmp(argv[1],"repeat")){assert(trigger_action_runtime_stage(&r,&x,4)==0);trigger_action_runtime_event_t e[2];uint32_t n;assert(trigger_action_runtime_advance(&r,45,e,2,&n)==0&&n==2);assert(trigger_action_runtime_advance(&r,45,e,2,&n)==0&&n==0);return 0;}
 if(!strcmp(argv[1],"backward")){assert(trigger_action_runtime_stage(&r,&x,4)==0);trigger_action_runtime_event_t e[2];uint32_t n;assert(trigger_action_runtime_advance(&r,45,e,2,&n)==0);assert(trigger_action_runtime_advance(&r,44,e,2,&n)==TRIGGER_ACTION_RUNTIME_BACKWARD);return 0;}
 return 2;}
'''
@pytest.fixture(scope="module")
def runtime(tmp_path_factory):
    d=tmp_path_factory.mktemp("action-runtime")
    return compile_grid(d,"runtime",HARNESS)
@pytest.mark.parametrize("case",["order","repeat","backward"])
def test_runtime(runtime,case):
    r=subprocess.run([str(runtime),case],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr
