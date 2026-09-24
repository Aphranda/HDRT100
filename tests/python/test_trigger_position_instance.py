"""Local position descriptor to action-q expansion."""
import subprocess
import pytest
from test_trigger_grid_preview import ROOT, compile_grid

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_position_instance.h"
int main(int argc,char **argv){assert(argc==2);trigger_position_descriptor_t d={
 .schema=1,.run_id=1,.sync_epoch=2,.position_id=3,.count_epoch=4,
 .target_count=5,.prediction_version=6,.target_ms_index=10,
 .target_fine_slice_index=2,.grid_generation=7,.deployment_generation=8,
 .deployment_crc=9,.valid_from_sync=1,.expiry_sync=2};
 trigger_position_instance_t x;
 if(!strcmp(argv[1],"plus")){assert(trigger_position_instance_expand(&d,4,3,&x)&&x.target_q==42&&x.action_q==45);return 0;}
 if(!strcmp(argv[1],"minus")){assert(trigger_position_instance_expand(&d,4,-3,&x)&&x.action_q==39);return 0;}
 if(!strcmp(argv[1],"underflow")){assert(!trigger_position_instance_expand(&d,4,-43,&x));return 0;}
 return 2;}
'''

@pytest.fixture(scope="module")
def instance(tmp_path_factory):
    d=tmp_path_factory.mktemp("position-instance")
    return compile_grid(d,"instance",HARNESS)

@pytest.mark.parametrize("case",["plus","minus","underflow"])
def test_instance(instance,case):
    r=subprocess.run([str(instance),case],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr
