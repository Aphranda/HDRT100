"""Production STOP-only RAM page reader and SCPI callback, no serial hardware."""
import re
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_owner import ROOT, compile_executable, function_body
from test_vdc_local_follow_capture import local_capture_exe  # noqa: F401
from test_vdc_auto_capture import run_capture
from tools.dpll_observation_decode.dpll_observation_decode import HEADER, MAGIC, decode


@pytest.fixture(scope='module')
def ram_reader(tmp_path_factory):
    manager = (ROOT/'components/vdc_dpll_manager/src/vdc_dpll_manager.c').read_text(encoding='utf-8')
    scpi = (ROOT/'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    header = re.search(r'typedef struct __attribute__\(\(packed\)\) \{[^}]+\} vdc_dpll_manager_dpll_capture_header_t;', manager).group()
    body = function_body(manager, 'vdc_dpll_manager_dpll_capture_read')
    callback = function_body(scpi.replace('scpi_result_t', 'bool'), 'scpi_cmd_sync_vdc_dpll_trace_read_q')
    return compile_executable(tmp_path_factory.mktemp('capture-ram'), 'read', PREAMBLE+header+
        '\nbool vdc_dpll_manager_dpll_capture_read(uint32_t offset,uint8_t *data,uint32_t size,uint32_t *total_bytes,uint32_t *file_crc32){'+body+'}\n'+
        'static int scpi_cmd_sync_vdc_dpll_trace_read_q(scpi_t *context){'+callback+'}\n'+CASES)


def run(ram_reader, case, payload=b''):
    result = subprocess.run([str(ram_reader),case], input=payload, capture_output=True, timeout=5)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
    return result.stdout


@pytest.mark.parametrize('case', ['armed','incomplete','running','adapter','pending','changed',
    'missing','late_missing','count','offset','overflow','size','null','scpi_range','scpi_missing',
    'scpi_running','scpi_valid'])
def test_reader_rejects_invalid_lifecycle_or_range_and_preserves_outputs(ram_reader, case):
    run(ram_reader, case)


@pytest.mark.parametrize('case,count', [('empty',0),('full',76)])
def test_exact_native_header_and_cross_boundary_pages(ram_reader, case, count):
    data = run(ram_reader, case)
    magic,schema,size,n,dropped,start,end,crc = HEADER.unpack(data[:28])
    assert (magic,schema,size,n,dropped,start,end)==(MAGIC,6,100,count,0,100,1100)
    assert len(data)==28+count*100 and zlib.crc32(data[28:])==crc


def test_real_local_records_roundtrip_through_production_ram_export(ram_reader, local_capture_exe, tmp_path):
    records=run_capture(local_capture_exe,'flow')
    raw=run(ram_reader,'local',records)
    assert raw[28:]==records
    path=tmp_path/'local.ram.bin';path.write_bytes(raw)
    samples=decode(path,'NO2')['samples']['NO2']
    assert samples[1]['local_control']['applied']
    assert samples[1]['local_control']['after_dco_seq']==18


PREAMBLE=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_MAGIC 0x4c504444u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA 6u
static tdma_ring_runtime_snapshot_t snapshot={.config_seq=7,.applied_config_seq=7};
static unsigned reads, fail_read, change_read;
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *s){
    ++reads;if(reads==fail_read)return false;
    *s=snapshot;if(reads==change_read)++s->config_seq;return true;
}
static bool s_dpll_capture_armed,s_dpll_capture_complete=true;
static uint32_t s_dpll_capture_count=2,s_dpll_capture_dropped;
static uint32_t s_dpll_capture_start_ms=100,s_dpll_capture_end_ms=1100;
static vdc_dpll_manager_dpll_capture_record_t s_dpll_capture_records[VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES];
static uint32_t ota_crc32_update(uint32_t crc,const uint8_t *p,size_t n){
    crc=~crc;while(n--){crc^=*p++;for(unsigned b=0;b<8;++b)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}return ~crc;
}
static uint32_t ota_crc32_compute(const uint8_t *p,size_t n){return ota_crc32_update(0,p,n);}
typedef struct {uint32_t args[2],out[4];unsigned read,written,error;bool missing;char hex[257];} scpi_t;
#define SCPI_RES_ERR -1
#define SCPI_RES_OK 1
static bool scpi_port_read_u32(scpi_t *c,uint32_t *v){if(c->missing)return false;*v=c->args[c->read++];return true;}
static void scpi_port_push_exec_error(scpi_t *c,const char *v){assert(v);++c->error;}
static void SCPI_ResultUInt32(scpi_t *c,uint32_t v){assert(c->written<4);c->out[c->written++]=v;}
static void SCPI_ResultText(scpi_t *c,const char *s){strcpy(c->hex,s);}
'''

CASES=r'''
int main(int argc,char **argv){
#ifdef _WIN32
 _setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stdin),_O_BINARY);
#endif
 assert(argc==2);const char *mode=argv[1];uint32_t offset=0,size=28,total=123,crc=456;
 uint8_t data[128];memset(data,0xa5,sizeof(data));memset(s_dpll_capture_records,0x31,sizeof(s_dpll_capture_records));
 if(!strcmp(mode,"local")){assert(fread(s_dpll_capture_records,1,200,stdin)==200);}
 if(!strcmp(mode,"armed"))s_dpll_capture_armed=true;
 if(!strcmp(mode,"incomplete"))s_dpll_capture_complete=false;
 if(!strcmp(mode,"running")||!strcmp(mode,"scpi_running"))snapshot.enabled=1;
 if(!strcmp(mode,"adapter"))snapshot.adapter_started=1;
 if(!strcmp(mode,"pending"))snapshot.applied_config_seq=6;
 if(!strcmp(mode,"changed"))change_read=2;
 if(!strcmp(mode,"missing"))fail_read=1;
 if(!strcmp(mode,"late_missing"))fail_read=2;
 if(!strcmp(mode,"count"))s_dpll_capture_count=77;
 if(!strcmp(mode,"offset"))offset=229;
 if(!strcmp(mode,"overflow"))offset=0xffffffffu;
 if(!strcmp(mode,"size"))size=129;
 if(!strncmp(mode,"scpi_",5)){
    scpi_t c={.args={0,28}};if(!strcmp(mode,"scpi_range"))c.args[1]=129;
    if(!strcmp(mode,"scpi_missing"))c.missing=true;
    int result=scpi_cmd_sync_vdc_dpll_trace_read_q(&c);
    if(!strcmp(mode,"scpi_valid")){assert(result==1&&c.written==4&&!c.error);assert(c.out[0]==0&&c.out[1]==28&&c.out[2]==228);assert(strlen(c.hex)==56);}
    else {assert(result==-1&&c.error==1&&c.written==0&&c.hex[0]==0);}return 0;
 }
 if(!strcmp(mode,"empty"))s_dpll_capture_count=0;
 if(!strcmp(mode,"full"))s_dpll_capture_count=76;
 if(!strcmp(mode,"empty")||!strcmp(mode,"full")||!strcmp(mode,"local")){
    const uint32_t expected=28+s_dpll_capture_count*100;uint32_t combined=0,identity=0;
    while(offset<expected){size=expected-offset;if(size>128)size=128;
      assert(vdc_dpll_manager_dpll_capture_read(offset,data,size,&total,&crc));
      if(!offset)identity=crc;
      assert(total==expected&&identity==crc);
      combined=ota_crc32_update(combined,data,size);assert(fwrite(data,1,size,stdout)==size);offset+=size;
    }assert(combined==identity);return 0;
 }
 assert(!vdc_dpll_manager_dpll_capture_read(offset,!strcmp(mode,"null")?NULL:data,size,&total,&crc));
 assert(total==123&&crc==456);for(unsigned i=0;i<128;++i)assert(data[i]==0xa5);return 0;
}
'''
