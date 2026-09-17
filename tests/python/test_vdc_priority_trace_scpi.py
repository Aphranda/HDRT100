"""Execute production trace callbacks; request acceptance is separate from ACK."""
import subprocess

from test_tdma_priority_rx_scpi import ROOT, callback
from test_vdc_command_owner import compile_executable


def test_real_priority_trace_callbacks(tmp_path):
    source = (ROOT / 'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    table = (ROOT / 'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    pairs = [('ARM', 'arm'), ('ORIGin', 'origin_arm'), ('STOP', 'stop'), ('RELease', 'release'), ('STATus?', 'status_q'), ('READ?', 'read_q')]
    names = ['scpi_cmd_vdc_priority_trace_' + suffix for _, suffix in pairs]
    for (pattern, _), name in zip(pairs, names):
        assert table.count(f'.pattern = "SYSTem:VDC:PRIORity:TRACe:{pattern}", .callback = {name}') == 1
    unit = PRELUDE + '\n'.join(callback(source, name) for name in names) + MAIN
    exe = compile_executable(tmp_path, 'trace_scpi', unit)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


PRELUDE = r'''
#include <assert.h>
#include <string.h>
#include "tdma_ring_runtime.h"
#include "vdc_priority_trace.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static uint32_t args[3], argc, cursor, fields[64], count, calls, errors;
static bool accepted=true, ring_ok=true, status_ok=true;
static char text[257];
static tdma_ring_clock_snapshot_t ring;
static bool SCPI_ParamUInt32(scpi_t *c,uint32_t *v,int required)
{ (void)c;assert(required);if(cursor>=argc)return false;*v=args[cursor++];return true; }
static bool scpi_port_read_u32(scpi_t *c,uint32_t *v){return SCPI_ParamUInt32(c,v,TRUE);}
static void SCPI_ResultUInt32(scpi_t *c,uint32_t v){(void)c;assert(count<64);fields[count++]=v;}
static void SCPI_ResultText(scpi_t *c,const char *s){(void)c;assert(strlen(s)<sizeof(text));strcpy(text,s);}
static void scpi_port_push_exec_error(scpi_t *c,const char *s){(void)c;(void)s;++errors;}
bool vdc_dpll_manager_priority_trace_arm(uint32_t id){assert(id==123);++calls;return accepted;}
bool vdc_dpll_manager_priority_trace_origin_arm(uint32_t id){assert(id==123);++calls;return accepted;}
bool vdc_dpll_manager_priority_trace_stop(void){++calls;return accepted;}
bool vdc_dpll_manager_priority_trace_release(void){++calls;return accepted;}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out){*out=ring;return ring_ok;}
bool vdc_dpll_manager_get_priority_trace(vdc_priority_trace_status_t *out)
{ ++calls;for(unsigned i=0;i<37;i++){uint32_t v=i+10;memcpy((uint8_t*)out+4*i,&v,4);}return status_ok; }
bool vdc_dpll_manager_priority_trace_read(uint32_t id,uint32_t offset,uint8_t *data,uint32_t size,uint32_t *total,uint32_t *crc)
{ assert(id==123 && offset==168 && size==128);++calls;if(!accepted)return false;
  for(uint32_t i=0;i<size;i++){data[i]=(uint8_t)i;}
  *total=768;*crc=0x87654321;return true; }
static void reset(void){cursor=0;count=0;calls=0;errors=0;text[0]=0;}
'''

MAIN = r'''
int main(void)
{
 scpi_t c=0;argc=0;
 assert(scpi_cmd_vdc_priority_trace_arm(&c)==SCPI_RES_ERR && calls==0 && count==0);
 reset();argc=1;args[0]=0;
 assert(scpi_cmd_vdc_priority_trace_arm(&c)==SCPI_RES_ERR && calls==0);
 reset();args[0]=123;accepted=false;
 assert(scpi_cmd_vdc_priority_trace_arm(&c)==SCPI_RES_ERR && calls==1 && count==0);
 reset();accepted=true;
 assert(scpi_cmd_vdc_priority_trace_arm(&c)==SCPI_RES_OK && count==1 && fields[0]==123);
 reset();argc=0;
 assert(scpi_cmd_vdc_priority_trace_origin_arm(&c)==SCPI_RES_ERR && calls==0 && count==0);
 reset();argc=1;args[0]=0;
 assert(scpi_cmd_vdc_priority_trace_origin_arm(&c)==SCPI_RES_ERR && calls==0);
 reset();args[0]=123;accepted=false;
 assert(scpi_cmd_vdc_priority_trace_origin_arm(&c)==SCPI_RES_ERR && calls==1 && count==0);
 reset();accepted=true;
 assert(scpi_cmd_vdc_priority_trace_origin_arm(&c)==SCPI_RES_OK && count==1 && fields[0]==123);
 reset();accepted=false;
 assert(scpi_cmd_vdc_priority_trace_stop(&c)==SCPI_RES_ERR && count==0);
 assert(scpi_cmd_vdc_priority_trace_release(&c)==SCPI_RES_ERR && count==0);
 accepted=true;
 assert(scpi_cmd_vdc_priority_trace_stop(&c)==SCPI_RES_OK && fields[0]==1);
 assert(scpi_cmd_vdc_priority_trace_release(&c)==SCPI_RES_OK && fields[1]==1);
 reset();ring.enabled=1;
 assert(scpi_cmd_vdc_priority_trace_status_q(&c)==SCPI_RES_ERR && calls==0);
 ring.enabled=0;ring.adapter_started=1;
 assert(scpi_cmd_vdc_priority_trace_status_q(&c)==SCPI_RES_ERR && calls==0);
 ring.adapter_started=0;ring_ok=false;
 assert(scpi_cmd_vdc_priority_trace_status_q(&c)==SCPI_RES_ERR && calls==0);
 ring_ok=true;status_ok=false;
 assert(scpi_cmd_vdc_priority_trace_status_q(&c)==SCPI_RES_ERR && count==0);
 status_ok=true;
 assert(scpi_cmd_vdc_priority_trace_status_q(&c)==SCPI_RES_OK && count==37);
 for(unsigned i=0;i<37;i++)assert(fields[i]==i+10);
 reset();argc=3;args[0]=123;args[1]=168;args[2]=129;
 assert(scpi_cmd_vdc_priority_trace_read_q(&c)==SCPI_RES_ERR && calls==0);
 reset();args[2]=0;
 assert(scpi_cmd_vdc_priority_trace_read_q(&c)==SCPI_RES_ERR && calls==0);
 reset();args[2]=128;accepted=false;
 assert(scpi_cmd_vdc_priority_trace_read_q(&c)==SCPI_RES_ERR && calls==1 && count==0 && !text[0]);
 reset();accepted=true;
 assert(scpi_cmd_vdc_priority_trace_read_q(&c)==SCPI_RES_OK && calls==1 && count==4);
 assert(fields[0]==168 && fields[1]==128 && fields[2]==768 && fields[3]==0x87654321);
 assert(strlen(text)==256 && !strncmp(text,"00010203",8) && !strcmp(text+252,"7e7f"));
 return 0;
}
'''
