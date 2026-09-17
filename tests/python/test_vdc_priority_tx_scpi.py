"""Run actual priority TX control/readback callbacks against STOP boundaries."""
import os
import shutil
import subprocess

from test_tdma_priority_rx_scpi import ROOT, callback


def test_priority_tx_control_and_stopped_evidence(tmp_path):
    source = (ROOT / 'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    names = ('scpi_cmd_vdc_priority_sync', 'scpi_cmd_vdc_priority_sync_q',
             'scpi_cmd_vdc_priority_tx_q', 'scpi_cmd_vdc_priority_rx_q')
    header = (ROOT / 'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    for pattern, name in zip(('SYNC', 'SYNC?', 'TX?', 'RX?'), names):
        assert header.count(f'.pattern = "SYSTem:VDC:PRIORity:{pattern}", .callback = {name}') == 1
    unit = tmp_path / 'commands.c'
    unit.write_text(HARNESS + '\n'.join(callback(source, name) for name in names) + MAIN,
                    encoding='utf-8')
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path / ('commands.exe' if os.name == 'nt' else 'commands')
    result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        *['-I' + str(ROOT / 'components' / name / 'inc') for name in
          ('tdma', 'vdc_dpll_manager', 'vdc_domain', 'calibration_manager', 'distributed_refmem')],
        str(unit), '-o', str(exe)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "vdc_priority_tx.h"
#include "vdc_priority_rx.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool parameter_ok=true, setter_ok=true, snapshot_ok=true;
static uint32_t generation, parameter=42, results, errors, sets, gets;
static uint32_t fields[32];
static tdma_ring_clock_snapshot_t ring;
static uint32_t rx_active;
static bool SCPI_ParamUInt32(scpi_t *c,uint32_t *v,int required)
{ (void)c;assert(required);*v=parameter;return parameter_ok; }
bool vdc_dpll_manager_set_priority_sync(uint32_t v)
{ ++sets;if(!setter_ok)return false;generation=v;return true; }
uint32_t vdc_dpll_manager_priority_sync_generation(void) { return generation; }
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *s)
{ *s=ring;return true; }
bool vdc_dpll_manager_get_priority_tx(vdc_priority_tx_snapshot_t *s)
{ ++gets;memset(s,0,sizeof(*s));s->schema=1;s->generation=42;
  s->event_time_lower=UINT64_C(0x123456789abcdef0);memset(s->mailbox,0xab,32);return snapshot_ok; }
bool vdc_dpll_manager_get_priority_rx(vdc_priority_rx_snapshot_t *s)
{ ++gets;memset(s,0,sizeof(*s));s->schema=1;s->active=rx_active;s->carrier_count=321;
  s->typed_record.binding_generation=42;s->typed_record.event_sequence=65537;
  s->typed_record.event_time_lower=UINT64_C(0x123456789abcdef0);return snapshot_ok; }
static void SCPI_ResultUInt32(scpi_t *c,uint32_t v) { (void)c;fields[results++]=v; }
static void scpi_sync_result_u64_parts(scpi_t *c,uint64_t v)
{ SCPI_ResultUInt32(c,(uint32_t)v);SCPI_ResultUInt32(c,(uint32_t)(v>>32)); }
static void SCPI_ResultText(scpi_t *c,const char *s)
{ (void)c;assert(strlen(s)==64);for(unsigned i=0;i<64;i++)assert(s[i]==(i%2?'b':'a'));++results; }
static void scpi_port_push_exec_error(scpi_t *c,const char *s) { (void)c;(void)s;++errors; }
'''

MAIN = r'''
int main(void)
{
    scpi_t c=0;parameter_ok=false;
    assert(scpi_cmd_vdc_priority_sync(&c)==SCPI_RES_ERR && sets==0 && errors==1 && results==0);
    parameter_ok=true;setter_ok=false;
    assert(scpi_cmd_vdc_priority_sync(&c)==SCPI_RES_ERR && sets==1 && generation==0);
    setter_ok=true;
    assert(scpi_cmd_vdc_priority_sync(&c)==SCPI_RES_OK && fields[0]==42 && results==1);
    results=0;
    assert(scpi_cmd_vdc_priority_sync_q(&c)==SCPI_RES_OK && fields[0]==42 && results==1);
    results=0;ring.enabled=1;
    assert(scpi_cmd_vdc_priority_tx_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.enabled=0;ring.adapter_started=1;
    assert(scpi_cmd_vdc_priority_tx_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.adapter_started=0;snapshot_ok=false;
    assert(scpi_cmd_vdc_priority_tx_q(&c)==SCPI_RES_ERR && results==0 && gets==1);
    snapshot_ok=true;
    assert(scpi_cmd_vdc_priority_tx_q(&c)==SCPI_RES_OK && results==27 && gets==2);
    assert(fields[0]==1 && fields[1]==42 && fields[24]==0x9abcdef0 && fields[25]==0x12345678);
    results=0;gets=0;ring.enabled=1;
    assert(scpi_cmd_vdc_priority_rx_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.enabled=0;ring.adapter_started=1;
    assert(scpi_cmd_vdc_priority_rx_q(&c)==SCPI_RES_ERR && results==0 && gets==0);
    ring.adapter_started=0;rx_active=1;
    assert(scpi_cmd_vdc_priority_rx_q(&c)==SCPI_RES_ERR && results==0 && gets==1);
    rx_active=0;snapshot_ok=false;
    assert(scpi_cmd_vdc_priority_rx_q(&c)==SCPI_RES_ERR && results==0 && gets==2);
    snapshot_ok=true;
    assert(scpi_cmd_vdc_priority_rx_q(&c)==SCPI_RES_OK && results==22 && gets==3);
    assert(fields[4]==321 && fields[16]==42 && fields[17]==65537 &&
        fields[18]==0x9abcdef0 && fields[19]==0x12345678);
    return 0;
}
'''
