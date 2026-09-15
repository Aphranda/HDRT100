"""Exercise the real STOP/ARM service lifecycle around diagnostic tap publication."""
import os
from pathlib import Path
import shutil
import subprocess

import test_tdma_geometry_config as lifecycle
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body


def test_event_tap_publication_serializes_with_arm_and_stop(tmp_path):
    source = lifecycle.HARNESS.replace('int main(int argc, char **argv)',
                                       'int old_geometry_main(int argc, char **argv)')
    source += r'''
static unsigned publications;
static bool publish_result = true;
static bool publish_tap(void *context)
{
    assert(context == &publications);
    assert(owner.ring_control_guard == 1u);
    /* A competing Core0 ARM/configure cannot interleave with this write. */
    assert(!tdma_service_ring_arm(&owner));
    assert(!tdma_service_set_ring_geometry_generation(&owner, 7u));
    ++publications;
    return publish_result;
}
static bool update(void)
{
    return tdma_service_update_event_tap(&owner, publish_tap, &publications);
}
int main(void)
{
    setup();
    assert(!tdma_service_update_event_tap(NULL, publish_tap, &publications));
    assert(!tdma_service_update_event_tap(&owner, NULL, &publications));
    assert(update() && publications == 1u);
    publish_result = false;
    assert(!update() && publications == 2u && owner.ring_control_guard == 0u);
    publish_result = true;
    owner.ring_control_guard = 1u;
    assert(!update() && owner.ring_control_guard == 1u);
    owner.ring_control_guard = 0u;
    assert(tdma_service_set_ring_geometry_generation(&owner, 7u));
    assert(!update());
    assert(tdma_service_set_ring_geometry_generation(&owner, 0u));
    owner.stopped_update = TDMA_STOPPED_UPDATE_REQUESTED | 1u;
    assert(!update());
    owner.stopped_update = TDMA_STOPPED_UPDATE_APPLYING | 1u;
    assert(!update());
    owner.stopped_update = 0u;
    owner.ring_runtime.result_guard = 1u;
    assert(!update());
    owner.ring_runtime.result_guard = 0u;
    owner.ring_runtime.config_guard = 1u;
    assert(!update());
    owner.ring_runtime.config_guard = 0u;
    assert(publications == 2u);
    assert(tdma_service_ring_arm(&owner));
    /* ARM is accepted while the physical adapter has not started yet. */
    assert(!owner.ring_runtime.adapter_started);
    assert(!update());
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(owner.ring_runtime.adapter_started && !update());
    tdma_service_core0_lifecycle_service(&owner);
    assert(!update());
    reject_stop = true;
    assert(tdma_service_ring_stop(&owner));
    assert(!update());
    for (unsigned i = 0; i < 3; ++i) {
        tdma_ring_runtime_service(&owner.ring_runtime);
        assert(owner.ring_runtime.adapter_stop_pending);
        assert(!update());
    }
    assert(publications == 2u);
    reject_stop = false;
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(update() && publications == 3u);
    assert(owner.ring_control_pending == TDMA_RING_CONTROL_NONE);
    reject_start = true;
    assert(tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(!owner.ring_runtime.adapter_started && !update());
    finish_stop();
    assert(update() && publications == 4u);
    assert(owner.ring_control_guard == 0u);
    return 0;
}
'''
    harness = tmp_path / 'event_tap_control.c'
    harness.write_text(source, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler, 'A host C compiler is required'
    root = lifecycle.ROOT
    sources = [root / f'components/tdma/src/{name}.c' for name in (
        'tdma_profile', 'tdma_operating_profile', 'tdma_payload_registry',
        'tdma_flight_fifo', 'tdma_flight_engine', 'tdma_process_image_map',
        'tdma_traffic_scheduler')]
    exe = tmp_path / ('event_tap_control.exe' if os.name == 'nt' else 'event_tap_control')
    command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(root), '-I' + str(root / 'components/tdma/inc'),
        '-I' + str(root / 'components/vdc_domain/inc'),
        str(harness), *map(str, sources), '-o', str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (tmp_path / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    (tmp_path / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr


def test_event_tap_runtime_and_scpi(tmp_path):
    root = lifecycle.ROOT
    header = (root / 'components/tdma/inc/tdma_pio_spi_phys.h').read_text(encoding='utf-8')
    end = header.index('} tdma_pio_spi_event_tap_snapshot_t;') + len('} tdma_pio_spi_event_tap_snapshot_t;')
    types = header[header.index('#define TDMA_PIO_SPI_EVENT_TAP_MAX_DELAY_CYCLES'):end]
    runtime = (root / 'components/tdma/src/tdma_runtime_owner.c').read_text(encoding='utf-8')
    scpi = (root / 'middleware/scpi_port/src/scpi_tdma_commands.c').read_text(encoding='utf-8')
    end = runtime.index('} tdma_runtime_event_tap_request_t;') + len('} tdma_runtime_event_tap_request_t;')
    request = runtime[runtime.rfind('typedef struct {', 0, end):end]
    functions = []
    for source, name, signature in (
        (runtime, 'tdma_runtime_owner_publish_event_tap', 'static bool tdma_runtime_owner_publish_event_tap(void *context)'),
        (runtime, 'tdma_runtime_owner_set_event_tap', 'bool tdma_runtime_owner_set_event_tap(uint32_t enabled, uint32_t prefix_bits, uint32_t sample_delay_cycles)'),
        (runtime, 'tdma_runtime_owner_get_event_tap', 'bool tdma_runtime_owner_get_event_tap(tdma_pio_spi_event_tap_snapshot_t *snapshot)'),
        (scpi, 'scpi_cmd_tdma_event_tap', 'scpi_result_t scpi_cmd_tdma_event_tap(scpi_t *context)'),
        (scpi, 'scpi_cmd_tdma_event_tap_q', 'scpi_result_t scpi_cmd_tdma_event_tap_q(scpi_t *context)'),
    ):
        functions.append(signature + '{' + c_definition_body(source, name) + '}')
    source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
''' + types + request + r'''
static bool s_tdma_runtime_owner_initialized, stopped=true, physical_ok=true, get_ok=true;
static int s_tdma_runtime_owner, s_tdma_pio_spi_phys;
static uint32_t args[3], writes[3], results[12];
static unsigned read_index, available=3, count, errors, ok_text, publish_calls;
static tdma_pio_spi_event_tap_snapshot_t retained;
static bool tdma_service_update_event_tap(int *owner, bool (*callback)(void*), void *context)
{ assert(owner == &s_tdma_runtime_owner); return stopped && callback(context); }
static bool tdma_pio_spi_phys_event_tap_set(int *phys, uint32_t e, uint32_t p, uint32_t d)
{ assert(phys == &s_tdma_pio_spi_phys); ++publish_calls; writes[0]=e; writes[1]=p; writes[2]=d; return physical_ok; }
static bool tdma_pio_spi_phys_event_tap_get(const int *phys, tdma_pio_spi_event_tap_snapshot_t *out)
{ assert(phys == &s_tdma_pio_spi_phys); if (!get_ok || !out) return false; *out=retained; return true; }
static bool scpi_port_read_u32(scpi_t *ctx, uint32_t *out)
{ (void)ctx; if(read_index>=available)return false; *out=args[read_index++]; return true; }
static void scpi_port_push_exec_error(scpi_t *ctx, const char *message)
{ (void)ctx; assert(message && *message); ++errors; }
static void SCPI_ResultText(scpi_t *ctx, const char *message)
{ (void)ctx; assert(!strcmp(message,"OK")); ++ok_text; }
static void SCPI_ResultUInt32(scpi_t *ctx, uint32_t value)
{ (void)ctx; assert(count<12); results[count++]=value; }
''' + '\n'.join(functions) + r'''
int main(void)
{
    scpi_t context=0;
    args[0]=1; args[1]=109; args[2]=15;
    assert(scpi_cmd_tdma_event_tap(&context)==SCPI_RES_ERR && publish_calls==0);
    s_tdma_runtime_owner_initialized=true;
    for (available=0; available<3; ++available) {
        read_index=0;
        assert(scpi_cmd_tdma_event_tap(&context)==SCPI_RES_ERR && publish_calls==0);
    }
    stopped=false; read_index=0;
    assert(scpi_cmd_tdma_event_tap(&context)==SCPI_RES_ERR && publish_calls==0);
    stopped=true; physical_ok=false; read_index=0;
    assert(scpi_cmd_tdma_event_tap(&context)==SCPI_RES_ERR && publish_calls==1);
    physical_ok=true; read_index=0;
    assert(scpi_cmd_tdma_event_tap(&context)==SCPI_RES_OK && publish_calls==2);
    assert(!memcmp(writes,args,sizeof(args)) && ok_text==1 && errors==6);
    retained=(tdma_pio_spi_event_tap_snapshot_t){
        .requested={1,109,15,3}, .applied={1,98,14,2},
        .applied_valid=1, .actual_valid=1, .actual_prefix_bits=98,
        .actual_sample_delay_cycles=14};
    const uint32_t expected[]={1,109,15,3,1,98,14,2,1,1,98,14};
    assert(scpi_cmd_tdma_event_tap_q(&context)==SCPI_RES_OK);
    assert(count==12 && !memcmp(results,expected,sizeof(expected)));
    count=0; get_ok=false;
    assert(scpi_cmd_tdma_event_tap_q(&context)==SCPI_RES_ERR && count==0);
    return 0;
}
'''
    harness = tmp_path / 'event_tap_scpi.c'
    harness.write_text(source, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path / ('event_tap_scpi.exe' if os.name == 'nt' else 'event_tap_scpi')
    result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
        str(harness), '-o', str(exe)], capture_output=True, text=True, timeout=30)
    (tmp_path / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    (tmp_path / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
