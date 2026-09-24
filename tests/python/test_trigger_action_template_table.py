"""Static template table identity, role and CRC checks."""
import subprocess
import pytest
from test_trigger_grid_preview import ROOT, compile_grid

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_action_template_table.h"
static trigger_deployment_t dep(void){trigger_deployment_t d={.generation=2,.rows={{.board_uid=1,.tdma_slot=0,.input_mask=1},{.board_uid=2,.tdma_slot=1,.output_mask=7},{.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},{.board_uid=4,.tdma_slot=3,.output_mask=1}}};return d;}
static trigger_action_template_table_t tab(void){return (trigger_action_template_table_t){.generation=3,.count=1,.row={{.template_id=4,.generation=3,.role=1,.kind=TRIGGER_ACTION_TEMPLATE_SWITCH,.argument=3,.span_slices=2,.delta_q=-2}}};}
int main(int argc,char **argv){assert(argc==2);trigger_deployment_t d=dep();trigger_action_template_table_t t=tab();uint32_t crc=trigger_action_template_table_crc(&t);
 if(!strcmp(argv[1],"valid")){assert(trigger_action_template_table_validate(&t,&d,crc)==0);return 0;}
 if(!strcmp(argv[1],"crc")){assert(trigger_action_template_table_validate(&t,&d,crc+1)==TRIGGER_ACTION_TEMPLATE_TABLE_CRC);return 0;}
 if(!strcmp(argv[1],"id")){t.row[0].template_id=0;assert(trigger_action_template_table_validate(&t,&d,trigger_action_template_table_crc(&t))==TRIGGER_ACTION_TEMPLATE_TABLE_ARGUMENT);return 0;}
 if(!strcmp(argv[1],"role")){t.row[0].role=2;assert(trigger_action_template_table_validate(&t,&d,trigger_action_template_table_crc(&t))==TRIGGER_ACTION_TEMPLATE_TABLE_ROLE);return 0;}
 return 2;}
'''

@pytest.fixture(scope="module")
def table(tmp_path_factory):
    d=tmp_path_factory.mktemp("template-table")
    return compile_grid(d,"table",HARNESS)

@pytest.mark.parametrize("case",["valid","crc","id","role"])
def test_table(table,case):
    r=subprocess.run([str(table),case],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr
