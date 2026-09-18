"""Execute summary ARM callbacks: parsing, mode and owner rejection stay explicit."""
import subprocess

from test_tdma_priority_rx_scpi import ROOT, callback
from test_vdc_command_owner import compile_executable


def test_summary_arm_callbacks(tmp_path):
    source = (ROOT/'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    table = (ROOT/'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    functions = []
    for pattern, mode in [('PHASe','phase'), ('ORIGin','origin')]:
        name = f'scpi_cmd_vdc_priority_trace_summary_{mode}_arm'
        assert table.count(f'.pattern = "SYSTem:VDC:PRIORity:TRACe:SUMMary:{pattern}", .callback = {name}') == 1
        functions.append(callback(source,name))
    unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool parsed, accepted, last_origin;
static uint32_t argument, received, calls, results, errors;
static bool SCPI_ParamUInt32(scpi_t *c,uint32_t *out,int required)
{(void)c;assert(required);if(!parsed)return false;*out=argument;return true;}
static void SCPI_ResultUInt32(scpi_t *c,uint32_t value)
{(void)c;assert(value==argument);++results;}
static void scpi_port_push_exec_error(scpi_t *c,const char *msg)
{(void)c;assert(msg);++errors;}
bool vdc_dpll_manager_priority_trace_summary_arm(uint32_t id,bool origin)
{received=id;last_origin=origin;++calls;return accepted;}
''' + '\n'.join(functions) + r'''
int main(void) {
    scpi_result_t (*callbacks[])(scpi_t *) = {
        scpi_cmd_vdc_priority_trace_summary_phase_arm,
        scpi_cmd_vdc_priority_trace_summary_origin_arm};
    for(unsigned i=0;i<2;++i) {
        parsed=false;argument=42;calls=results=errors=0;
        assert(callbacks[i](0)==SCPI_RES_ERR && !calls && !results && errors==1);
        parsed=true;argument=0;errors=0;
        assert(callbacks[i](0)==SCPI_RES_ERR && !calls && !results && errors==1);
        argument=UINT32_MAX;accepted=false;errors=0;
        assert(callbacks[i](0)==SCPI_RES_ERR && calls==1 && !results && errors==1);
        assert(received==argument && last_origin==(i!=0));
        argument=42;accepted=true;calls=errors=0;
        assert(callbacks[i](0)==SCPI_RES_OK && calls==1 && results==1 && !errors);
        assert(received==argument && last_origin==(i!=0));
    }
    return 0;
}
'''
    exe = compile_executable(tmp_path,'summary_scpi',unit)
    result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
