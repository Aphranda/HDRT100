"""Execute the real baseline callbacks, including parameter and storage failures."""
import json
import os
import shutil
import subprocess

from test_tdma_priority_rx_scpi import ROOT, callback


def test_baseline_scpi_commands(tmp_path):
    source = (ROOT / 'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    header = (ROOT / 'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    names = ('scpi_cmd_vdc_priority_follow_baseline', 'scpi_cmd_vdc_priority_follow_baseline_q',
             'scpi_cmd_vdc_priority_follow_baseline_default', 'scpi_cmd_vdc_priority_follow_baseline_recall',
             'scpi_cmd_vdc_priority_follow_baseline_store')
    for suffix, name in zip(('', '?', ':DEFAult', ':RECall', ':STORe'), names):
        assert header.count(f'.pattern = "SYSTem:VDC:PRIORity:FOLLow:BASEline{suffix}", .callback = {name}') == 1
    unit = tmp_path / 'commands.c'
    unit.write_text(HARNESS + '\n'.join(callback(source, n) for n in names) + MAIN, encoding='utf-8')
    exe = tmp_path / ('commands.exe' if os.name == 'nt' else 'commands')
    cc = shutil.which('gcc') or shutil.which('clang')
    assert cc
    command = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I'+str(ROOT/'components/vdc_dpll_manager/inc'), str(unit), '-o', str(exe)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (tmp_path / 'compile.json').write_text(json.dumps(dict(command=command,
        returncode=built.returncode, stdout=built.stdout, stderr=built.stderr), indent=2), encoding='utf-8')
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "vdc_priority_follow.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static uint32_t parameters[2], inputs, at, outputs, fields[2], errors, sets, stores, recalls, defaults;
static bool allowed=true, readable=true;
static vdc_priority_follow_baseline_config_t current={2u,250000000u}, saved={1u,100000000u};
static bool scpi_port_read_u32(scpi_t *c,uint32_t *v)
{ (void)c;if(at>=inputs)return false;*v=parameters[at++];return true; }
static void SCPI_ResultUInt32(scpi_t *c,uint32_t v)
{ (void)c;assert(outputs<2u);fields[outputs++]=v; }
static void SCPI_ResultText(scpi_t *c,const char *v)
{ (void)c;assert(strcmp(v,"OK")==0);++outputs; }
static void scpi_port_push_exec_error(scpi_t *c,const char *v)
{ (void)c;(void)v;++errors; }
bool vdc_dpll_manager_set_priority_follow_baseline(const vdc_priority_follow_baseline_config_t *v)
{ ++sets;if(!allowed||v->max_replacements>2u||!v->window_ns||v->window_ns>250000000u)return false;
  current=*v;return true; }
bool vdc_dpll_manager_get_priority_follow_baseline(vdc_priority_follow_baseline_config_t *v)
{ if(!readable)return false;*v=current;return true; }
bool vdc_dpll_manager_default_priority_follow_baseline(void)
{ ++defaults;if(!allowed)return false;current=(vdc_priority_follow_baseline_config_t){2u,250000000u};return true; }
bool vdc_dpll_manager_recall_priority_follow_baseline(void)
{ ++recalls;if(!allowed)return false;current=saved;return true; }
bool vdc_dpll_manager_store_priority_follow_baseline(void)
{ ++stores;if(!allowed)return false;saved=current;return true; }
static void reset(uint32_t n,uint32_t a,uint32_t b)
{ inputs=n;at=outputs=errors=0u;parameters[0]=a;parameters[1]=b; }
'''

MAIN = r'''
int main(void)
{
    scpi_t c=0;
    reset(0,0,0);assert(scpi_cmd_vdc_priority_follow_baseline(&c)==SCPI_RES_ERR&&sets==0u&&outputs==0u);
    reset(1,1,0);assert(scpi_cmd_vdc_priority_follow_baseline(&c)==SCPI_RES_ERR&&sets==0u&&outputs==0u);
    const uint32_t bad[][2]={{3,100},{1,0},{1,250000001},{UINT32_MAX,UINT32_MAX}};
    for(unsigned i=0;i<4;++i){reset(2,bad[i][0],bad[i][1]);
        assert(scpi_cmd_vdc_priority_follow_baseline(&c)==SCPI_RES_ERR&&outputs==0u&&errors==1u);
        assert(current.max_replacements==2u&&current.window_ns==250000000u);}
    allowed=false;reset(2,1,100000000u);
    assert(scpi_cmd_vdc_priority_follow_baseline(&c)==SCPI_RES_ERR&&outputs==0u);
    assert(current.max_replacements==2u);
    allowed=true;reset(2,0,1u);
    assert(scpi_cmd_vdc_priority_follow_baseline(&c)==SCPI_RES_OK&&outputs==2u&&fields[0]==0u&&fields[1]==1u);
    assert(stores==0u);
    reset(0,0,0);assert(scpi_cmd_vdc_priority_follow_baseline_q(&c)==SCPI_RES_OK&&fields[0]==0u&&fields[1]==1u);
    readable=false;reset(0,0,0);
    assert(scpi_cmd_vdc_priority_follow_baseline_q(&c)==SCPI_RES_ERR&&outputs==0u);
    readable=true;allowed=false;
    reset(0,0,0);assert(scpi_cmd_vdc_priority_follow_baseline_store(&c)==SCPI_RES_ERR&&outputs==0u);
    assert(saved.max_replacements==1u);
    reset(0,0,0);assert(scpi_cmd_vdc_priority_follow_baseline_recall(&c)==SCPI_RES_ERR&&outputs==0u);
    reset(0,0,0);assert(scpi_cmd_vdc_priority_follow_baseline_default(&c)==SCPI_RES_ERR&&outputs==0u);
    assert(current.max_replacements==0u);
    allowed=true;reset(0,0,0);
    assert(scpi_cmd_vdc_priority_follow_baseline_recall(&c)==SCPI_RES_OK&&outputs==1u);
    assert(current.max_replacements==1u&&current.window_ns==100000000u);
    reset(0,0,0);assert(scpi_cmd_vdc_priority_follow_baseline_default(&c)==SCPI_RES_OK&&outputs==1u);
    assert(current.max_replacements==2u&&current.window_ns==250000000u);
    assert(saved.max_replacements==1u);
    reset(0,0,0);assert(scpi_cmd_vdc_priority_follow_baseline_store(&c)==SCPI_RES_OK&&outputs==1u);
    assert(saved.max_replacements==2u&&saved.window_ns==250000000u);
    assert(stores==2u&&recalls==2u&&defaults==2u);
    return 0;
}
'''
