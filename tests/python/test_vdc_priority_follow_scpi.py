"""Execute the production FOLLOW callbacks, including STOP and busy rejection."""
import json
import os
import shutil
import subprocess

from test_tdma_priority_rx_scpi import ROOT, callback


def test_priority_follow_commands(tmp_path):
    source = (ROOT / 'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    header = (ROOT / 'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    names = ('scpi_cmd_vdc_priority_follow', 'scpi_cmd_vdc_priority_follow_q',
             'scpi_cmd_vdc_priority_follow_status_q')
    for pattern, name in zip(('FOLLow', 'FOLLow?', 'FOLLow:STATus?'), names):
        assert header.count(f'.pattern = "SYSTem:VDC:PRIORity:{pattern}", .callback = {name}') == 1
    unit = tmp_path / 'commands.c'
    unit.write_text(HARNESS + '\n'.join(callback(source, name) for name in names) + MAIN, encoding='utf-8')
    executable = tmp_path / ('commands.exe' if os.name == 'nt' else 'commands')
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               *['-I' + str(ROOT / p) for p in ('components/tdma/inc', 'components/vdc_dpll_manager/inc')],
               str(unit), '-o', str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (tmp_path / 'compile.json').write_text(json.dumps(dict(command=command,
        returncode=built.returncode, stdout=built.stdout, stderr=built.stderr), indent=2), encoding='utf-8')
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "tdma_ring_runtime.h"
#include "vdc_priority_follow.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool parameter_ok=true, setter_ok=true, query_ok=true, snapshot_ok=true, ring_ok=true;
static bool enabled;
static uint32_t parameter=1, results, errors, sets, gets, fields[64];
static tdma_ring_clock_snapshot_t ring;
static bool SCPI_ParamUInt32(scpi_t *c,uint32_t *v,int required)
{ (void)c;assert(required);*v=parameter;return parameter_ok; }
bool vdc_dpll_manager_set_priority_follow(bool v)
{ ++sets;if(!setter_ok)return false;enabled=v;return true; }
bool vdc_dpll_manager_try_priority_follow_enabled(bool *v)
{ if(!query_ok)return false;*v=enabled;return true; }
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *s)
{ *s=ring;return ring_ok; }
bool vdc_dpll_manager_get_priority_follow(vdc_priority_follow_snapshot_t *s)
{ ++gets;memset(s,0,sizeof(*s));s->schema=1;s->active=1;s->applied=7;
  s->before_dco_seq=4;s->after_dco_seq=5;s->before_ppb=-500;s->after_ppb=-1000;
  s->selected_delta_ppb=-500;s->arm_epoch=UINT64_C(0x123456789abcdef0);
  s->local_interval_lo=123456789;s->local_interval_hi=123456790;
  s->error_lo_ppb=INT64_MIN;s->error_hi_ppb=-1;
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
    assert(scpi_cmd_vdc_priority_follow(&c)==SCPI_RES_ERR && sets==0 && errors==1 && results==0);
    parameter_ok=true;parameter=2;
    assert(scpi_cmd_vdc_priority_follow(&c)==SCPI_RES_ERR && sets==0 && results==0);
    parameter=1;setter_ok=false;
    assert(scpi_cmd_vdc_priority_follow(&c)==SCPI_RES_ERR && sets==1 && !enabled);
    setter_ok=true;
    assert(scpi_cmd_vdc_priority_follow(&c)==SCPI_RES_OK && enabled && fields[0]==1 && results==1);
    results=0;query_ok=false;
    assert(scpi_cmd_vdc_priority_follow_q(&c)==SCPI_RES_ERR && results==0);
    query_ok=true;
    assert(scpi_cmd_vdc_priority_follow_q(&c)==SCPI_RES_OK && fields[0]==1 && results==1);
    results=0;ring_ok=false;
    assert(scpi_cmd_vdc_priority_follow_status_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring_ok=true;ring.enabled=1;
    assert(scpi_cmd_vdc_priority_follow_status_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.enabled=0;ring.adapter_started=1;
    assert(scpi_cmd_vdc_priority_follow_status_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.adapter_started=0;snapshot_ok=false;
    assert(scpi_cmd_vdc_priority_follow_status_q(&c)==SCPI_RES_ERR && results==0 && gets==1);
    snapshot_ok=true;
    assert(scpi_cmd_vdc_priority_follow_status_q(&c)==SCPI_RES_OK && results==54 && gets==2);
    assert(fields[0]==1 && fields[1]==1 && fields[13]==7);
    assert(fields[25]==4 && fields[26]==5);
    assert((int32_t)fields[30]==-500 && (int32_t)fields[31]==-1000 && (int32_t)fields[32]==-500);
    assert(fields[34]==0x9abcdef0 && fields[35]==0x12345678);
    assert(fields[46]==123456789 && fields[47]==0 && fields[48]==123456790 && fields[49]==0);
    assert(fields[50]==0 && fields[51]==0x80000000 && fields[52]==UINT32_MAX && fields[53]==UINT32_MAX);
    results=0;parameter=0;
    assert(scpi_cmd_vdc_priority_follow(&c)==SCPI_RES_OK && fields[0]==0 && !enabled);
    return 0;
}
'''
