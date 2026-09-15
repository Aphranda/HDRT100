"""Recovery diagnostic facade preserves the full retained failure and ARM identity."""
import os
from pathlib import Path
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_recovery_readback_and_unavailable_paths(tmp_path):
    header = (ROOT / 'components/tdma/inc/tdma_pio_spi_phys.h').read_text(encoding='utf-8')
    end = header.index('} tdma_pio_spi_event_recovery_snapshot_t;') + len('} tdma_pio_spi_event_recovery_snapshot_t;')
    record = header[header.rfind('typedef struct {', 0, end):end]
    runtime = (ROOT / 'components/tdma/src/tdma_runtime_owner.c').read_text(encoding='utf-8')
    scpi = (ROOT / 'middleware/scpi_port/src/scpi_tdma_commands.c').read_text(encoding='utf-8')
    funcs = '\n'.join(signature + '{' + c_definition_body(source, name) + '}'
        for source, name, signature in (
            (runtime, 'tdma_runtime_owner_get_event_recovery',
             'bool tdma_runtime_owner_get_event_recovery(tdma_pio_spi_event_recovery_snapshot_t *snapshot)'),
            (scpi, 'scpi_cmd_tdma_event_recovery_q',
             'scpi_result_t scpi_cmd_tdma_event_recovery_q(scpi_t *context)')))
    source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
''' + record + r'''
static int s_tdma_pio_spi_phys;
static bool s_tdma_runtime_owner_initialized, available=true;
static tdma_pio_spi_event_recovery_snapshot_t retained;
static uint32_t results[18];
static unsigned calls, count;
static bool tdma_pio_spi_phys_event_recovery_get(const int *phys,
    tdma_pio_spi_event_recovery_snapshot_t *out)
{
    assert(phys == &s_tdma_pio_spi_phys); ++calls;
    if (!available || !out) return false;
    *out=retained; return true;
}
static void SCPI_ResultUInt32(scpi_t *ctx, uint32_t value)
{ (void)ctx; assert(count<18); results[count++]=value; }
''' + funcs + r'''
int main(void)
{
    scpi_t context=0;
    retained=(tdma_pio_spi_event_recovery_snapshot_t){
        .pending=0, .failure_count=27, .attempt_count=30, .enable_count=28,
        .deferral_count=4, .cancel_count=2, .cancel_reason=1, .service_max_us=150,
        .last_failure_epoch=42, .last_failure_reason=9, .last_failure_fault_bits=0,
        .last_accepted_sequence=0x12345678u, .last_accepted_ordinal=17,
        .last_batch_sequence_first=0x78563412u, .last_batch_sequence_valid=1,
        .binding_tap_generation=6, .binding_arm_epoch_lo=0xfedcba98u,
        .binding_arm_epoch_hi=0x87654321u};
    const uint32_t expected[]={0,27,30,28,4,2,1,150,42,9,0,0x12345678u,17,
        0x78563412u,1,6,0xfedcba98u,0x87654321u};
    assert(scpi_cmd_tdma_event_recovery_q(&context)==SCPI_RES_ERR);
    assert(!calls && !count);
    s_tdma_runtime_owner_initialized=true; available=false;
    assert(scpi_cmd_tdma_event_recovery_q(&context)==SCPI_RES_ERR);
    assert(calls==1 && !count);
    available=true;
    assert(!tdma_runtime_owner_get_event_recovery(NULL));
    assert(scpi_cmd_tdma_event_recovery_q(&context)==SCPI_RES_OK);
    assert(count==18 && !memcmp(results,expected,sizeof(expected)));
    return 0;
}
'''
    harness = tmp_path / 'recovery_scpi.c'
    harness.write_text(source, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler, 'Host compiler required'
    exe = tmp_path / ('recovery_scpi.exe' if os.name == 'nt' else 'recovery_scpi')
    result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
        str(harness), '-o', str(exe)], capture_output=True, text=True, timeout=30)
    (tmp_path / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    (tmp_path / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
