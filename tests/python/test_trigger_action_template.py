"""Static action template expansion into independent position instances."""
import subprocess
import pytest
from test_trigger_grid_preview import ROOT, compile_grid

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_action_template.h"
int main(int argc,char **argv){assert(argc==2);trigger_position_descriptor_t d={
 /* Reference the versioned schema symbol instead of a literal version. */
 .schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,.run_id=1,.sync_epoch=2,.position_id=3,.count_epoch=4,
 .target_count=5,.prediction_version=6,.target_ms_index=10,
 .target_fine_slice_index=2,.grid_generation=7,.deployment_generation=8,
 .deployment_crc=9,.valid_from_sync=1,.expiry_sync=2};
 trigger_action_template_t t={.template_id=11,.generation=12,.role=1,
  .kind=TRIGGER_ACTION_TEMPLATE_SWITCH,.argument=3,.span_slices=2,.delta_q=-2};
 trigger_action_template_instance_t x;
 if(!strcmp(argv[1],"valid")){assert(trigger_action_template_expand(&t,&d,4,&x)&&x.position.action_q==40&&x.role==1);return 0;}
 if(!strcmp(argv[1],"bad")){t.kind=99;assert(!trigger_action_template_expand(&t,&d,4,&x));return 0;}
 if(!strcmp(argv[1],"underflow")){t.delta_q=-43;assert(!trigger_action_template_expand(&t,&d,4,&x));return 0;}
 return 2;}
'''

@pytest.fixture(scope="module")
def template(tmp_path_factory):
    d=tmp_path_factory.mktemp("action-template")
    return compile_grid(d,"template",HARNESS)

@pytest.mark.parametrize("case",["valid","bad","underflow"])
def test_template(template,case):
    r=subprocess.run([str(template),case],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr
