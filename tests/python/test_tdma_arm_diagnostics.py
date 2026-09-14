"""Exercise the real ARM frontend's rejection routing without peripheral access."""
import os
from pathlib import Path
import re
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_arm_reports_the_rejecting_boundary(tmp_path):
    implementation = (ROOT / 'components/distributed_refmem/src/distributed_refmem.c').read_text(encoding='utf-8')
    header = (ROOT / 'components/distributed_refmem/inc/distributed_refmem.h').read_text(encoding='utf-8')
    enum = re.search(r'typedef enum \{\s*DISTRIBUTED_REFMEM_TDMA_ARM_NOT_ATTEMPTED.*?\} distributed_refmem_tdma_arm_result_t;', header, re.S)
    assert enum
    body = c_definition_body(implementation, 'distributed_refmem_tdma_ring_arm')
    source = r'''
#include "tdma_service.h"
#include <assert.h>
#include <string.h>
ENUM_DEFINITION
static tdma_service_service_t owner;
static bool s_initialized, owner_available, snapshot_available, runtime_active;
static bool calibration_valid, runtime_accepts;
static uint32_t board_no, map_calls, arm_calls, s_tdma_ring_arm_last_result;
static tdma_service_flight_map_result_t rejection;
static tdma_service_service_t *tdma_runtime_owner_get(void) { return owner_available ? &owner : NULL; }
static uint32_t board_identity_get_no(void) { return board_no; }
static tdma_process_image_map_t distributed_refmem_default_flight_map(uint32_t node_count) {
    assert(node_count == owner.ring_staged_config.node_count);
    tdma_process_image_map_t map = {0}; return map;
}
bool tdma_ring_runtime_get_snapshot(const tdma_ring_runtime_t *runtime, tdma_ring_runtime_snapshot_t *snapshot) {
    assert(runtime == &owner.ring_runtime);
    memset(snapshot, 0, sizeof(*snapshot)); snapshot->adapter_started = runtime_active;
    return snapshot_available;
}
tdma_service_flight_map_result_t tdma_service_configure_flight_map_checked(
    tdma_service_service_t *service, const tdma_process_image_map_t *map) {
    assert(service == &owner && map != NULL); ++map_calls; return rejection;
}
bool tdma_ring_runtime_validate_calibration_stage(const tdma_ring_calibration_stage_t *stage,
    uint32_t nodes, tdma_ring_runtime_reason_t *reason) {
    (void)reason; assert(stage == &owner.calibration_stage && nodes == owner.ring_staged_config.node_count);
    return calibration_valid;
}
bool tdma_service_ring_arm(tdma_service_service_t *service) {
    assert(service == &owner); ++arm_calls; return runtime_accepts;
}
bool distributed_refmem_tdma_ring_arm(void) { ARM_BODY }
static void reset(void) {
    memset(&owner, 0, sizeof(owner));
    s_initialized = owner_available = snapshot_available = calibration_valid = runtime_accepts = true;
    runtime_active = false; board_no = 1; map_calls = arm_calls = 0;
    owner.ring_staged_config.enabled = 1; owner.ring_staged_config.node_count = 4;
    rejection = TDMA_SERVICE_FLIGHT_MAP_OK;
    s_tdma_ring_arm_last_result = UINT32_MAX;
}
static void result(uint32_t expected, uint32_t expected_map_calls, uint32_t expected_arm_calls) {
    assert(distributed_refmem_tdma_ring_arm() == (expected == DISTRIBUTED_REFMEM_TDMA_ARM_OK));
    assert(s_tdma_ring_arm_last_result == expected);
    assert(map_calls == expected_map_calls && arm_calls == expected_arm_calls);
}
int main(void) {
    static const struct { tdma_service_flight_map_result_t rejection; uint32_t result; } cases[] = {
        {TDMA_SERVICE_FLIGHT_MAP_INVALID, DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_REJECTED},
        {TDMA_SERVICE_FLIGHT_MAP_SNAPSHOT_UNAVAILABLE, DISTRIBUTED_REFMEM_TDMA_ARM_SNAPSHOT_UNAVAILABLE},
        {TDMA_SERVICE_FLIGHT_MAP_RUNTIME_ACTIVE, DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_ACTIVE},
        {TDMA_SERVICE_FLIGHT_MAP_BUSY, DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_BUSY},
        {TDMA_SERVICE_FLIGHT_MAP_ENGINE_ACTIVE, DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_ENGINE_ACTIVE},
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        reset(); rejection = cases[i].rejection; result(cases[i].result, 1, 0);
    }
    reset(); board_no = 5; result(DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED, 0, 0);
    reset(); s_initialized = false; result(DISTRIBUTED_REFMEM_TDMA_ARM_OWNER_UNAVAILABLE, 0, 0);
    reset(); owner_available = false; result(DISTRIBUTED_REFMEM_TDMA_ARM_OWNER_UNAVAILABLE, 0, 0);
    reset(); snapshot_available = false; result(DISTRIBUTED_REFMEM_TDMA_ARM_SNAPSHOT_UNAVAILABLE, 0, 0);
    reset(); runtime_active = true; result(DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_ACTIVE, 0, 0);
    reset(); owner.ring_staged_config.enabled = 0; result(DISTRIBUTED_REFMEM_TDMA_ARM_STAGED_CONFIG_MISSING, 0, 0);
    reset(); owner.calibration_gate_required = 1; calibration_valid = false;
    result(DISTRIBUTED_REFMEM_TDMA_ARM_CALIBRATION_GATE_REJECTED, 1, 0);
    reset(); runtime_accepts = false; result(DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED, 1, 1);
    reset(); result(DISTRIBUTED_REFMEM_TDMA_ARM_OK, 1, 1);
    return 0;
}
'''.replace('ENUM_DEFINITION', enum[0]).replace('ARM_BODY', body)
    unit = tmp_path / 'arm_diagnostics.c'
    unit.write_text(source, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path / ('arm_diagnostics.exe' if os.name == 'nt' else 'arm_diagnostics')
    subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(ROOT / 'components/tdma/inc'), str(unit), '-o', str(exe)], check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=3)
