"""The bridge probe must refuse RUN before touching the hardware reader."""
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    definition = ingress_definition(source, "scpi_cmd_vdc_feedback_bridge_q")
    return compile_executable(tmp_path_factory.mktemp("bridge-scpi"), "bridge_scpi", PREAMBLE + definition + CASES)


@pytest.mark.parametrize("case", ["stopped", "enabled", "adapter", "busy", "unavailable"])
def test_bridge_probe_boundary(executable, case):
    result = subprocess.run([str(executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_bridge_probe_registration():
    header = (ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    assert header.count('.pattern = "SYSTem:VDC:FEEDback:BRIDge?", .callback = scpi_cmd_vdc_feedback_bridge_q') == 1


PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_timestamp_clock.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
typedef int scpi_t;
typedef int scpi_result_t;
typedef struct { bool enabled, adapter_started; } tdma_ring_runtime_snapshot_t;
static tdma_ring_runtime_snapshot_t ring;
static bool ring_available=true, diagnostic_available=true;
static uint32_t calls, errors, count;
static uint64_t values[41];
static char kinds[41];
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{ *out=ring;return ring_available; }
bool vdc_timestamp_clock_read_bridge_diagnostic(uint32_t expected,vdc_timestamp_clock_bridge_diagnostic_t *out)
{
    assert(expected==BOARD_SYS_CLOCK_HZ);++calls;
    *out=(vdc_timestamp_clock_bridge_diagnostic_t){.schema=1,.platform_supported=1,
        .expected_hz=expected,.pll_cs=0x80000001u,.xosc_status=0x81001000u,
        .timer0_sample_us=UINT64_C(0x123456789abcdef),.timer1_sample_ticks=UINT64_MAX,
        .bridge={.raw_before=UINT64_MAX-5,.local_ns=UINT64_MAX-3,.raw_after=UINT64_MAX-1,.tick_hz=expected}};
    return diagnostic_available;
}
static void scpi_port_push_exec_error(scpi_t *ctx,const char *message)
{ (void)ctx;assert(!strcmp(message,"VDC_FEEDBACK_BRIDGE_STOP_REQUIRED_OR_BUSY"));++errors; }
static void SCPI_ResultUInt32(scpi_t *ctx,uint32_t value)
{ (void)ctx;assert(count<41);kinds[count]='u';values[count++]=value; }
static void SCPI_ResultUInt64(scpi_t *ctx,uint64_t value)
{ (void)ctx;assert(count<41);kinds[count]='U';values[count++]=value; }
'''

CASES = r'''
int main(int argc,char **argv)
{
    assert(argc==2);scpi_t ctx=0;
    if(!strcmp(argv[1],"enabled"))ring.enabled=true;
    else if(!strcmp(argv[1],"adapter"))ring.adapter_started=true;
    else if(!strcmp(argv[1],"busy"))ring_available=false;
    else if(!strcmp(argv[1],"unavailable"))diagnostic_available=false;
    else assert(!strcmp(argv[1],"stopped"));
    const int result=scpi_cmd_vdc_feedback_bridge_q(&ctx);
    if(ring.enabled || ring.adapter_started || !ring_available)
        assert(result==SCPI_RES_ERR && errors==1 && calls==0 && count==0);
    else if(!diagnostic_available)
        assert(result==SCPI_RES_ERR && errors==0 && calls==1 && count==0);
    else {
        assert(result==SCPI_RES_OK && errors==0 && calls==1 && count==41);
        assert(values[0]==1 && values[1]==1 && values[2]==0 && values[3]==0);
        assert(values[5]==BOARD_SYS_CLOCK_HZ && values[24]==0x80000001u && values[31]==0x81001000u);
        assert(values[35]==UINT64_C(0x123456789abcdef) && values[36]==UINT64_MAX);
        assert(values[37]==UINT64_MAX-5 && values[38]==UINT64_MAX-3 && values[39]==UINT64_MAX-1);
        assert(values[40]==BOARD_SYS_CLOCK_HZ && !memcmp(kinds+35,"UUUUUu",6));
    }
    puts("bridge SCPI passed");return 0;
}
'''
