"""Execute raw origin arithmetic without upgrading it to physical-edge evidence."""
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def origin_reference_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("origin-reference")
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_plan.h"
static tdma_origin_live_snapshot_t valid(void)
{
    return (tdma_origin_live_snapshot_t){.retained=1,.active=1,.sample={
        .epoch=7,.published_version=8,.record={.epoch=7,
        .format=TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME,.flags=TDMA_ORIGIN_RECORD_TRANSPORT_CHECKED,
        .sequence_end=42,.observation={.sequence=42,.identity=0xabcdef01,.local_generation=3},
        .raw_time={.arm_before={0x1234,0xfffffff0,0x1234},.arm_after={0x1235,0x10,0x1235},
            .latch_remaining=UINT32_MAX-31,.latch_fstat=0,.arm_padout=1u<<26,.tick_hz=250000000}}}};
}
int main(int argc,char **argv)
{
    assert(argc==2); const char *mode=argv[1];
    tdma_origin_live_snapshot_t live=valid();
    tdma_origin_raw_time_t *raw=&live.sample.record.raw_time;
    tdma_origin_raw_reference_t out,sentinel; memset(&sentinel,0x5a,sizeof(sentinel)); out=sentinel;
    uint32_t sm=2,pin=26; bool accepted=false;
    if(!strcmp(mode,"interval")) accepted=true;
    else if(!strcmp(mode,"other_fifo_empty")) {raw->latch_fstat=1u<<8;accepted=true;}
    else if(!strcmp(mode,"boundary_indices")) {sm=3;pin=31;raw->arm_padout=1u<<31;accepted=true;}
    else if(!strcmp(mode,"zero_elapsed")) {raw->latch_remaining=UINT32_MAX;accepted=true;}
    else if(!strcmp(mode,"max_elapsed")) {raw->latch_remaining=0;accepted=true;}
    else if(!strcmp(mode,"zero_width")) {memcpy(raw->arm_after,raw->arm_before,sizeof(raw->arm_after));accepted=true;}
    else if(!strcmp(mode,"other_rate")) {raw->tick_hz=125000000;accepted=true;}
    else if(!strcmp(mode,"torn_before")) raw->arm_before[2]++;
    else if(!strcmp(mode,"torn_after")) raw->arm_after[2]++;
    else if(!strcmp(mode,"reverse")) {raw->arm_after[0]=raw->arm_after[2]=0x1233;}
    else if(!strcmp(mode,"overflow")) {
        raw->arm_before[0]=raw->arm_before[2]=UINT32_MAX;raw->arm_before[1]=UINT32_MAX-10;
        memcpy(raw->arm_after,raw->arm_before,sizeof(raw->arm_after));
    }
    else if(!strcmp(mode,"empty_fifo")) raw->latch_fstat=1u<<(8+sm);
    else if(!strcmp(mode,"pad_low")) raw->arm_padout=0;
    else if(!strcmp(mode,"no_rate")) raw->tick_hz=0;
    else if(!strcmp(mode,"wrong_epoch")) live.sample.record.epoch++;
    else if(!strcmp(mode,"zero_epoch")) live.sample.epoch=0;
    else if(!strcmp(mode,"wrong_format")) live.sample.record.format=0;
    else if(!strcmp(mode,"tail_mismatch")) live.sample.record.sequence_end++;
    else if(!strcmp(mode,"unchecked_transport")) live.sample.record.flags=0;
    else if(!strcmp(mode,"output_pending")) live.sample.record.observation.output_remaining=1;
    else if(!strcmp(mode,"no_generation")) live.sample.record.observation.local_generation=0;
    else if(!strcmp(mode,"fault")) live.sample.fault=1;
    else if(!strcmp(mode,"odd_version")) live.sample.published_version=9;
    else if(!strcmp(mode,"zero_version")) live.sample.published_version=0;
    else if(!strcmp(mode,"not_retained")) live.retained=0;
    else if(!strcmp(mode,"inactive")) live.active=0;
    else if(!strcmp(mode,"bad_sm")) sm=4;
    else if(!strcmp(mode,"bad_pin")) pin=32;
    else if(!strcmp(mode,"null")) {
        assert(!tdma_origin_raw_reference(NULL,sm,pin,&out));
        assert(!tdma_origin_raw_reference(&live,sm,pin,NULL));
        assert(!memcmp(&out,&sentinel,sizeof(out)));return 0;
    } else assert(!"unknown test");
    const tdma_origin_live_snapshot_t original=live;
    assert(tdma_origin_raw_reference(&live,sm,pin,&out)==accepted);
    assert(!memcmp(&live,&original,sizeof(live)));
    if(!accepted) assert(!memcmp(&out,&sentinel,sizeof(out)));
    else {
        const uint64_t before=((uint64_t)raw->arm_before[0]<<32)|raw->arm_before[1];
        const uint64_t after=((uint64_t)raw->arm_after[0]<<32)|raw->arm_after[1];
        const uint64_t elapsed=2ull*(UINT32_MAX-raw->latch_remaining);
        assert(out.timer_lower==before+elapsed && out.timer_upper==after+elapsed);
        assert(out.timer_upper-out.timer_lower==after-before);
        assert(out.epoch==7 && out.sequence==42 && out.identity==0xabcdef01 && out.published_version==8);
        assert(out.tick_hz==raw->tick_hz);
        /* Known endpoint values include low-word rollover and exactly two
         * clk_sys ticks per decrement. No midpoint, nanoseconds or GPIO bias. */
        if(!strcmp(mode,"interval")) assert(out.timer_lower==0x12350000002eull && out.timer_upper==0x12350000004eull);
    }
    printf("raw origin arithmetic %s passed\n",mode);return 0;
}
'''
    return compile_executable(directory, "origin_reference", source,
                              [ROOT / "components/tdma/src/tdma_origin_reference.c"])


@pytest.mark.parametrize("case", [
    "interval", "other_fifo_empty", "boundary_indices", "zero_elapsed", "max_elapsed", "zero_width", "other_rate",
    "torn_before", "torn_after", "reverse", "overflow", "empty_fifo", "pad_low", "no_rate", "wrong_epoch",
    "zero_epoch", "wrong_format", "tail_mismatch", "unchecked_transport", "output_pending", "no_generation",
    "fault", "odd_version", "zero_version", "not_retained", "inactive", "bad_sm", "bad_pin", "null",
])
def test_origin_reference(origin_reference_executable, case):
    result = subprocess.run([str(origin_reference_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
