"""Exercise actual MATCH SCPI callbacks and lossless signed residual encoding."""
import os
import shutil
import subprocess
from test_tdma_priority_rx_scpi import ROOT, callback

def test_priority_match_commands(tmp_path):
    source = (ROOT / 'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    names = ('scpi_cmd_vdc_priority_match', 'scpi_cmd_vdc_priority_match_q',
             'scpi_cmd_vdc_priority_match_status_q')
    header = (ROOT / 'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    for pattern, name in zip(('MATCH', 'MATCH?', 'MATCH:STATus?'), names):
        assert header.count(f'.pattern = "SYSTem:VDC:PRIORity:{pattern}", .callback = {name}') == 1
    unit = tmp_path / 'commands.c'
    unit.write_text(HARNESS + '\n'.join(callback(source, name) for name in names) + MAIN, encoding='utf-8')
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path / ('commands.exe' if os.name == 'nt' else 'commands')
    result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        *['-I' + str(ROOT / p) for p in ('components/tdma/inc', 'components/vdc_dpll_manager/inc')],
        str(unit), '-o', str(exe)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "tdma_ring_runtime.h"
#include "vdc_priority_match.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool parameter_ok=true, setter_ok=true, snapshot_ok=true, ring_ok=true;
static uint32_t generation, parameter=42, results, errors, sets, gets, fields[64];
static tdma_ring_clock_snapshot_t ring;
static bool SCPI_ParamUInt32(scpi_t *c,uint32_t *v,int required)
{ (void)c;assert(required);*v=parameter;return parameter_ok; }
bool vdc_dpll_manager_set_priority_match(uint32_t v)
{ ++sets;if(!setter_ok)return false;generation=v;return true; }
uint32_t vdc_dpll_manager_priority_match_generation(void) { return generation; }
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *s)
{ *s=ring;return ring_ok; }
bool vdc_dpll_manager_get_priority_match(vdc_priority_match_snapshot_t *s)
{ ++gets;memset(s,0,sizeof(*s));s->schema=1;s->generation=42;s->active=1;
  s->arm_epoch=UINT64_C(0x123456789abcdef0);s->residual_lo=INT64_MIN;s->residual_hi=-1;
  return snapshot_ok; }
static void SCPI_ResultUInt32(scpi_t *c,uint32_t v) { (void)c;assert(results<64);fields[results++]=v; }
static void scpi_sync_result_u64_parts(scpi_t *c,uint64_t v)
{ SCPI_ResultUInt32(c,(uint32_t)v);SCPI_ResultUInt32(c,(uint32_t)(v>>32)); }
static void scpi_port_push_exec_error(scpi_t *c,const char *s) { (void)c;(void)s;++errors; }
'''
MAIN = r'''
int main(void)
{
    scpi_t c=0;parameter_ok=false;
    assert(scpi_cmd_vdc_priority_match(&c)==SCPI_RES_ERR && sets==0 && errors==1 && results==0);
    parameter_ok=true;setter_ok=false;
    assert(scpi_cmd_vdc_priority_match(&c)==SCPI_RES_ERR && sets==1 && generation==0);
    setter_ok=true;
    assert(scpi_cmd_vdc_priority_match(&c)==SCPI_RES_OK && fields[0]==42 && results==1);
    results=0;
    assert(scpi_cmd_vdc_priority_match_q(&c)==SCPI_RES_OK && fields[0]==42 && results==1);
    results=0;ring_ok=false;
    assert(scpi_cmd_vdc_priority_match_status_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring_ok=true;ring.enabled=1;
    assert(scpi_cmd_vdc_priority_match_status_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.enabled=0;ring.adapter_started=1;
    assert(scpi_cmd_vdc_priority_match_status_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.adapter_started=0;snapshot_ok=false;
    assert(scpi_cmd_vdc_priority_match_status_q(&c)==SCPI_RES_ERR && results==0 && gets==1);
    snapshot_ok=true;
    assert(scpi_cmd_vdc_priority_match_status_q(&c)==SCPI_RES_OK && results==62 && gets==2);
    assert(fields[0]==1 && fields[1]==42 && fields[2]==1);
    assert(fields[38]==0x9abcdef0 && fields[39]==0x12345678);
    assert(fields[58]==0 && fields[59]==0x80000000 && fields[60]==UINT32_MAX && fields[61]==UINT32_MAX);
    results=0;parameter=0;
    assert(scpi_cmd_vdc_priority_match(&c)==SCPI_RES_OK && fields[0]==0 && generation==0);
    return 0;
}
'''
