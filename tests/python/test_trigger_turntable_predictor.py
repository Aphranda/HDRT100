"""Deterministic turntable boundary and threshold prediction model."""
import subprocess
import pytest
from test_trigger_grid_preview import ROOT, compile_grid

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_turntable_predictor.h"
int main(int argc,char **argv){assert(argc==2);
 trigger_turntable_predictor_config_t c={1000u,0,0,10000,5000u,5000u,950u};
 trigger_turntable_predictor_snapshot_t s={0};assert(trigger_turntable_predictor_configure(&s,&c));
 assert(trigger_turntable_predictor_begin(&s,&c,7u,0u,0));
 if(!strcmp(argv[1],"targets")){assert(s.target_count[0]==5000&&s.target_count[1]==5000&&s.target_count[2]==15000);assert(s.threshold_count[0]==4750&&s.threshold_count[2]==14250);return 0;}
 if(!strcmp(argv[1],"threshold")){uint32_t m=0;assert(!trigger_turntable_predictor_observe(&s,&c,4749,&m)&&!m);assert(!trigger_turntable_predictor_observe(&s,&c,4750,&m)&&(m&1u));return 0;}
 if(!strcmp(argv[1],"descriptor")){uint32_t m=0;trigger_position_descriptor_t d;assert(!trigger_turntable_predictor_observe(&s,&c,4750,&m));assert(trigger_turntable_predictor_build_descriptor(&s,2,3,1,10,4,5,6,7,8,9,&d));assert(d.position_id==1&&d.target_count==5000&&d.count_epoch==7);return 0;}
 if(!strcmp(argv[1],"terminal")){uint32_t m=0;assert(!trigger_turntable_predictor_observe(&s,&c,14250,&m)&&(m&4u));assert(!s.terminal_verified);assert(!trigger_turntable_predictor_observe(&s,&c,20000,&m)&&s.terminal_verified);return 0;}
 if(!strcmp(argv[1],"backward")){uint32_t m;assert(!trigger_turntable_predictor_observe(&s,&c,100,&m));assert(trigger_turntable_predictor_observe(&s,&c,99,&m)==TRIGGER_TURNTABLE_BACKWARD);return 0;}
 return 2;}
'''

@pytest.fixture(scope="module")
def predictor(tmp_path_factory):
    d=tmp_path_factory.mktemp("turntable-predictor")
    return compile_grid(d,"predictor",HARNESS)

@pytest.mark.parametrize("case",["targets","threshold","descriptor","terminal","backward"])
def test_predictor(predictor,case):
    r=subprocess.run([str(predictor),case],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr
