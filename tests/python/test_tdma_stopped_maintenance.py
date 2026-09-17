"""Run the real STOP lifecycle while a synchronous maintenance callback owns control."""
import os
import shutil
import subprocess

import test_tdma_geometry_config as lifecycle


def test_stopped_maintenance_excludes_control_and_always_unlocks(tmp_path):
    source = lifecycle.HARNESS.replace('int main(int argc, char **argv)',
                                       'int old_geometry_main(int argc, char **argv)')
    source += r'''
static unsigned calls;
static bool maintenance_result = true;
static bool metadata(void *context) { (void)context; assert(false); return false; }
static bool maintain(void *context)
{
    assert(context == &calls && owner.ring_control_guard == 1u);
    ++calls;
    assert(!tdma_service_ring_arm(&owner));
    assert(!tdma_service_ring_start(&owner));
    assert(!tdma_service_ring_stop(&owner));
    assert(!tdma_service_set_ring_geometry_generation(&owner, 7u));
    tdma_service_ring_runtime_config_t config = owner.ring_staged_config;
    assert(!tdma_service_configure_ring_runtime(&owner, &config));
    uint32_t generation = 0u;
    assert(!tdma_service_request_stopped_update(&owner, 1u, &generation));
    assert(!tdma_service_update_stopped_metadata(&owner, metadata, NULL));
    assert(!tdma_service_run_stopped_maintenance(&owner, metadata, NULL));
    /* Core1 service remains independent of the Core0 control guard. */
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(owner.ring_control_guard == 1u);
    return maintenance_result;
}
static bool run(void)
{ return tdma_service_run_stopped_maintenance(&owner, maintain, &calls); }
int main(void)
{
    setup();
    assert(!tdma_service_run_stopped_maintenance(NULL, maintain, &calls));
    assert(!tdma_service_run_stopped_maintenance(&owner, NULL, &calls));
    assert(run() && calls == 1u && owner.ring_control_guard == 0u);
    maintenance_result = false;
    assert(!run() && calls == 2u && owner.ring_control_guard == 0u);
    maintenance_result = true;
    owner.ring_control_guard = 1u;
    assert(!run() && owner.ring_control_guard == 1u && calls == 2u);
    owner.ring_control_guard = 0u;
    assert(tdma_service_set_ring_geometry_generation(&owner, 7u));
    assert(!run() && owner.ring_control_guard == 0u);
    assert(tdma_service_set_ring_geometry_generation(&owner, 0u));
    owner.stopped_update = TDMA_STOPPED_UPDATE_REQUESTED | 1u;
    assert(!run() && owner.ring_control_guard == 0u);
    owner.stopped_update = TDMA_STOPPED_UPDATE_APPLYING | 1u;
    assert(!run() && owner.ring_control_guard == 0u);
    owner.stopped_update = 0u;
    owner.ring_runtime.result_guard = 1u;
    assert(!run() && owner.ring_control_guard == 0u);
    owner.ring_runtime.result_guard = 0u;
    owner.ring_runtime.config_guard = 1u;
    assert(!run() && owner.ring_control_guard == 0u);
    owner.ring_runtime.config_guard = 0u;
    assert(calls == 2u);
    assert(tdma_service_ring_arm(&owner));
    assert(!run());
    tdma_ring_runtime_service(&owner.ring_runtime);
    tdma_service_core0_lifecycle_service(&owner);
    assert(!run() && calls == 2u);
    reject_stop = true;
    assert(tdma_service_ring_stop(&owner));
    for (unsigned i = 0; i < 3; ++i) {
        tdma_ring_runtime_service(&owner.ring_runtime);
        assert(owner.ring_runtime.adapter_stop_pending);
        assert(!run() && owner.ring_control_guard == 0u);
    }
    reject_stop = false;
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(run() && calls == 3u);
    assert(owner.ring_control_pending == TDMA_RING_CONTROL_NONE);
    reject_start = true;
    assert(tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(!owner.ring_runtime.adapter_started && !run());
    finish_stop();
    assert(run() && calls == 4u && owner.ring_control_guard == 0u);
    return 0;
}
'''
    harness = tmp_path / 'maintenance.c'
    harness.write_text(source, encoding='utf-8')
    cc = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert cc
    root = lifecycle.ROOT
    sources = [root / f'components/tdma/src/{name}.c' for name in (
        'tdma_profile', 'tdma_operating_profile', 'tdma_payload_registry',
        'tdma_flight_fifo', 'tdma_flight_engine', 'tdma_process_image_map',
        'tdma_traffic_scheduler')]
    exe = tmp_path / ('maintenance.exe' if os.name == 'nt' else 'maintenance')
    result = subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(root), '-I' + str(root / 'components/tdma/inc'),
        '-I' + str(root / 'components/vdc_domain/inc'),
        str(harness), *map(str, sources), '-o', str(exe)],
        capture_output=True, text=True, timeout=60)
    (tmp_path / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    (tmp_path / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
