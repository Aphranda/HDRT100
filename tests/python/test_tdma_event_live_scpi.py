"""Real diagnostic facade preserves raw event identity and 64-bit clock domains."""
import os
from pathlib import Path
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_live_snapshot_readback_and_unavailable_paths(tmp_path):
    header = (ROOT / 'components/tdma/inc/tdma_pio_spi_phys.h').read_text(encoding='utf-8')
    marker = '} tdma_pio_spi_event_live_snapshot_t;'
    end = header.index(marker) + len(marker)
    declaration = header[header.rfind('typedef struct {', 0, end):end]
    runtime = (ROOT / 'components/tdma/src/tdma_runtime_owner.c').read_text(encoding='utf-8')
    scpi = (ROOT / 'middleware/scpi_port/src/scpi_tdma_commands.c').read_text(encoding='utf-8')
    functions = '\n'.join(signature + '{' + c_definition_body(source, name) + '}'
        for source, name, signature in (
            (runtime, 'tdma_runtime_owner_get_event_live_snapshot',
             'bool tdma_runtime_owner_get_event_live_snapshot(tdma_pio_spi_event_live_snapshot_t *snapshot)'),
            (scpi, 'scpi_cmd_tdma_event_live_q',
             'scpi_result_t scpi_cmd_tdma_event_live_q(scpi_t *context)')))
    source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "tdma_event_observer.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
''' + declaration + r'''
static int s_tdma_pio_spi_phys;
static bool s_tdma_runtime_owner_initialized, available=true;
static tdma_pio_spi_event_live_snapshot_t retained;
static uint32_t results[27];
static unsigned calls, count;
static bool tdma_pio_spi_phys_event_get_live_snapshot(const int *phys,
    tdma_pio_spi_event_live_snapshot_t *out)
{
    assert(phys == &s_tdma_pio_spi_phys); ++calls;
    if (!available || !out) return false;
    *out=retained; return true;
}
static void SCPI_ResultUInt32(scpi_t *ctx, uint32_t value)
{ (void)ctx; assert(count<27); results[count++]=value; }
''' + functions + r'''
int main(void)
{
    scpi_t context=0;
    retained=(tdma_pio_spi_event_live_snapshot_t){
        .flags=13, .tick_hz=250000000u,
        .arm_epoch=0x01020304abcdef01ull,
        .timer1_enable_before=0x12345678fffffffeull,
        .timer1_enable_after=0x1234567900000010ull,
        .record={.epoch=4, .ordinal=123, .sequence=0,
          .raw_rx=0xfedcba98u, .raw_tx=0x76543210u,
          .rx_elapsed_cycles=0x9876543201234567ull,
          .tx_elapsed_cycles=0x9876543201234587ull,
          .start_bounds={0x1122334455667788ull,0x11223344556677ffull},
          .diagnostic_only=true, .physical_first_unproved=true,
          .identity_unproved=true, .timestamp_valid=false, .dpll_eligible=false}};
    const uint32_t expected[]={1,13,250000000u,0xabcdef01u,0x01020304u,
      0xfffffffeu,0x12345678u,0x10,0x12345679u,4,123,0,
      0xfedcba98u,0x76543210u,0x01234567u,0x98765432u,
      0x01234587u,0x98765432u,0x55667788u,0x11223344u,
      0x556677ffu,0x11223344u,1,1,1,0,0};
    tdma_pio_spi_event_live_snapshot_t unchanged;
    memset(&unchanged,0xa5,sizeof(unchanged));
    const tdma_pio_spi_event_live_snapshot_t sentinel=unchanged;
    assert(!tdma_runtime_owner_get_event_live_snapshot(&unchanged));
    assert(!memcmp(&unchanged,&sentinel,sizeof(unchanged)));
    assert(scpi_cmd_tdma_event_live_q(&context)==SCPI_RES_ERR);
    assert(!calls && !count);
    s_tdma_runtime_owner_initialized=true; available=false;
    assert(!tdma_runtime_owner_get_event_live_snapshot(NULL));
    assert(!calls);
    assert(scpi_cmd_tdma_event_live_q(&context)==SCPI_RES_ERR);
    assert(calls==1 && !count);
    available=true;
    assert(scpi_cmd_tdma_event_live_q(&context)==SCPI_RES_OK);
    assert(count==27 && !memcmp(results,expected,sizeof(expected)));
    count=0; memset(&retained,0,sizeof(retained));
    assert(scpi_cmd_tdma_event_live_q(&context)==SCPI_RES_OK);
    assert(count==27 && results[0]==1);
    for(unsigned i=1;i<count;i++) assert(results[i]==0);
    return 0;
}
'''
    harness = tmp_path / 'event_live_scpi.c'
    harness.write_text(source, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler, 'Host compiler required'
    exe = tmp_path / ('event_live_scpi.exe' if os.name == 'nt' else 'event_live_scpi')
    result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(ROOT / 'components/tdma/inc'), str(harness), '-o', str(exe)],
        capture_output=True, text=True, timeout=30)
    (tmp_path / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    (tmp_path / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
