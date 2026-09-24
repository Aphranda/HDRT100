"""Position descriptor identity and canonical CRC coverage."""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_position_descriptor.h"
int main(int argc,char **argv){
    assert(argc==2);
    trigger_position_descriptor_t d={
        .schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,.run_id=7,.sync_epoch=8,
        .position_id=9,.count_epoch=10,.target_count=UINT64_C(1000),
        .prediction_version=11,.target_ms_index=12,
        .target_fine_slice_index=3,.grid_generation=13,
        .deployment_generation=14,.deployment_crc=15,
        .valid_from_sync=16,.expiry_sync=20};
    if(!strcmp(argv[1],"valid")){assert(trigger_position_descriptor_valid(&d,4));return 0;}
    if(!strcmp(argv[1],"q")){uint64_t q=0;assert(trigger_position_descriptor_target_q(&d,4,&q)&&q==51);return 0;}
    if(!strcmp(argv[1],"wire")){uint8_t w[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE];trigger_position_descriptor_t x;assert(trigger_position_descriptor_encode(&d,w)&&trigger_position_descriptor_decode(w,&x)&&x.position_id==d.position_id);w[0]^=1u;assert(!trigger_position_descriptor_decode(w,&x));return 0;}
    if(!strcmp(argv[1],"fragments")){uint8_t w[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE],part[4];trigger_position_descriptor_t x;trigger_position_fragment_context_t c={0};assert(trigger_position_descriptor_encode(&d,w));for(uint8_t i=0;i<TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT;++i){memcpy(part,w+i*4u,4u);assert(trigger_position_fragment_push(&c,1u,2u,(uint16_t)(100u+i),i,TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT,part,&x)==(i+1u<TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT?TRIGGER_POSITION_FRAGMENT_PROGRESS:TRIGGER_POSITION_FRAGMENT_COMPLETE));}assert(x.position_id==d.position_id);return 0;}
    if(!strcmp(argv[1],"reject")){uint8_t w[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE],part[4];trigger_position_descriptor_t x;trigger_position_fragment_context_t c={0};assert(trigger_position_descriptor_encode(&d,w));memcpy(part,w,4u);assert(trigger_position_fragment_push(&c,1u,2u,100u,0u,TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT,part,&x)==TRIGGER_POSITION_FRAGMENT_PROGRESS);assert(trigger_position_fragment_push(&c,1u,2u,102u,2u,TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT,part,&x)==TRIGGER_POSITION_FRAGMENT_REJECTED);return 0;}
    if(!strcmp(argv[1],"fine")){d.target_fine_slice_index=4;assert(!trigger_position_descriptor_valid(&d,4));return 0;}
    if(!strcmp(argv[1],"window")){d.valid_from_sync=21;assert(!trigger_position_descriptor_valid(&d,4));return 0;}
    if(!strcmp(argv[1],"crc")){uint32_t a=trigger_position_descriptor_crc(&d);d.position_id++;assert(a!=trigger_position_descriptor_crc(&d));return 0;}
    if(!strcmp(argv[1],"schema")){d.schema++;assert(!trigger_position_descriptor_valid(&d,4));return 0;}
    return 2;
}
'''


@pytest.fixture(scope="module")
def descriptor(tmp_path_factory):
    d = tmp_path_factory.mktemp("position-descriptor")
    return compile_grid(d, "descriptor", HARNESS)


@pytest.mark.parametrize("case", ["valid", "q", "wire", "fragments", "reject", "fine", "window", "crc", "schema"])
def test_descriptor(descriptor, case):
    result = subprocess.run([str(descriptor), case], capture_output=True,
        text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
