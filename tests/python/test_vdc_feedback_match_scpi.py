"""Execute diagnostic matcher SCPI serialization, including signed 64-bit bounds."""
import subprocess

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable


def test_feedback_match_scpi(tmp_path):
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    harness = r'''
#include <assert.h>
#include <string.h>
#include "vdc_dpll_manager.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool parameter_ok=true,available=true;
static uint32_t parameter,calls,count;
static uint64_t values[41];
static char kinds[41];
static vdc_dpll_manager_feedback_match_status_t snapshot;
static bool scpi_port_read_u32(scpi_t *c,uint32_t *out)
{ (void)c;*out=parameter;return parameter_ok; }
bool vdc_dpll_manager_get_feedback_match(uint32_t slot,vdc_dpll_manager_feedback_match_status_t *out)
{ ++calls;if(!available||slot>=PROJECT_NODE_CAPACITY)return false;*out=snapshot;return true; }
static void SCPI_ResultUInt32(scpi_t *c,uint32_t value)
{ (void)c;assert(count<41);kinds[count]='u';values[count++]=value; }
static void SCPI_ResultUInt64(scpi_t *c,uint64_t value)
{ (void)c;assert(count<41);kinds[count]='U';values[count++]=value; }
static void SCPI_ResultInt64(scpi_t *c,int64_t value)
{ (void)c;assert(count<41);kinds[count]='I';values[count++]=(uint64_t)value; }
''' + ingress_definition(source, "scpi_cmd_vdc_feedback_match_q") + r'''
int main(void)
{
    scpi_t context=0;
    parameter_ok=false;assert(scpi_cmd_vdc_feedback_match_q(&context)==SCPI_RES_ERR && !calls);
    parameter_ok=true;parameter=PROJECT_NODE_CAPACITY;assert(scpi_cmd_vdc_feedback_match_q(&context)==SCPI_RES_ERR);
    parameter=UINT32_MAX;assert(scpi_cmd_vdc_feedback_match_q(&context)==SCPI_RES_ERR);
    parameter=PROJECT_NODE_CAPACITY-1;available=false;assert(scpi_cmd_vdc_feedback_match_q(&context)==SCPI_RES_ERR && !count);
    available=true;
    const uint32_t header[]={1,1,5,0,7,9,0xabc,3,4,100,2,98,7,8,9,6,101,2,233,17,999};
    memcpy(&snapshot,header,sizeof(header));
    snapshot.result=(vdc_feedback_match_snapshot_t){.has_pair=1,.reference_epoch=19,.reference_generation=20,
        .source={.source_arm_epoch=0xabcdef0198765432ull,.source_clock_epoch_id=21,
          .source_clock_run_id=22,.observer_epoch=23,.tick_hz=250000000},
        .raw_ppb_lo=-0x123456789ll,.raw_ppb_hi=0x123456789ll,
        .pairs={{.measurement_sequence=0,.reference_identity_crc32=0xfedcba98,
            .rx_elapsed_cycles=0x87654321abcdef01ull,.reference_tx_lo=0x1234567800000001ull,.reference_tx_hi=0x1234567900000001ull},
          {.measurement_sequence=UINT32_MAX,.reference_identity_crc32=0x89abcdef,
            .rx_elapsed_cycles=UINT64_MAX,.reference_tx_lo=0x1234568000000001ull,.reference_tx_hi=0x1234568100000001ull}}};
    assert(scpi_cmd_vdc_feedback_match_q(&context)==SCPI_RES_OK && count==41);
    for(unsigned i=0;i<21;i++)assert(kinds[i]=='u'&&values[i]==header[i]);
    const uint64_t tail[]={1,19,20,0xabcdef0198765432ull,21,22,23,250000000,
      (uint64_t)-0x123456789ll,0x123456789ll,0,0xfedcba98,0x87654321abcdef01ull,0x1234567800000001ull,0x1234567900000001ull,
      UINT32_MAX,0x89abcdef,UINT64_MAX,0x1234568000000001ull,0x1234568100000001ull};
    assert(!memcmp(values+21,tail,sizeof(tail)));
    assert(!memcmp(kinds+21,"uuuUuuuuIIuuUUUuuUUU",20));
    return 0;
}
'''
    exe = compile_executable(tmp_path, "match_scpi", harness)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
