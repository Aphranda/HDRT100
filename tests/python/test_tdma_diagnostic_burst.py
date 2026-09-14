"""Exercise the production runtime, adapter and diagnostic SCPI boundaries."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def compile_run(tmp_path, name, sources, includes=()):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    exe = tmp_path / (name + (".exe" if os.name == "nt" else ""))
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "components/tdma/inc"),
                    "-I" + str(ROOT / "components/vdc_domain/inc"),
                    *includes, *map(str, sources), "-o", str(exe)],
                   check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=10)


@pytest.mark.parametrize("name", ["tdma_ring_runtime", "tdma_pio_spi_ring_adapter"])
def test_real_c_lifecycle_and_launch_bounds(tmp_path, name):
    names = [name] if name == "tdma_ring_runtime" else [
        name, "tdma_adapter_comm_fsm", "tdma_flight_fifo", "tdma_flight_engine",
        "tdma_flight_overlay", "tdma_overlay_prepare", "tdma_rx_prepare",
        "tdma_receive_health", "tdma_process_image_map", "tdma_ring_runtime",
        "tdma_transport_frame", "tdma_profile"]
    compile_run(tmp_path, name, [ROOT / f"tests/unit/test_{name}.c",
                *[ROOT / f"components/tdma/src/{n}.c" for n in names]])


def test_actual_scpi_callbacks_reject_active_or_changed_sessions(tmp_path):
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    functions = "\n".join(
        f"static int {name}(scpi_t *context) {{\n{c_definition_body(source, name)}\n}}"
        for name in ("scpi_cmd_system_tdma_ring_burst", "scpi_cmd_system_tdma_ring_burst_q",
                     "scpi_cmd_system_tdma_ring_burst_status_q"))
    unit = tmp_path / "scpi.c"
    unit.write_text(r'''
#include <assert.h>
#include "tdma_pio_spi_ring_adapter.h"
typedef int scpi_t;
#define SCPI_RES_ERR 0
#define SCPI_RES_OK 1
typedef tdma_ring_runtime_config_t tdma_service_ring_runtime_config_t;
typedef struct { uint32_t tx_count, tx_timeout_count, last_error; } tdma_pio_spi_phys_snapshot_t;
static uint32_t parameter, staged_flags, setter_calls, errors, result[8], fields, reads;
static bool parse_ok=true, setter_ok=true, staged_ok=true, change_generation=false;
static tdma_ring_runtime_snapshot_t ring;
static tdma_pio_spi_ring_adapter_snapshot_t adapter;
static tdma_pio_spi_phys_snapshot_t phys;
static bool scpi_port_read_u32(scpi_t *c, uint32_t *v) { (void)c; *v=parameter; return parse_ok; }
static void scpi_port_push_exec_error(scpi_t *c, const char *s) { (void)c; (void)s; errors++; }
static void SCPI_ResultUInt32(scpi_t *c, uint32_t v) { (void)c; assert(fields<8); result[fields++]=v; }
static bool tdma_runtime_owner_set_ring_diagnostic_burst(uint32_t v) {
    setter_calls++; assert(v<=2); return setter_ok;
}
static bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *c) {
    c->flags=staged_flags; return staged_ok;
}
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *s) {
    *s=ring; if (++reads==2 && change_generation) s->config_seq++; return true;
}
static tdma_pio_spi_ring_adapter_t *tdma_runtime_owner_get_ring_adapter(void) { return NULL; }
bool tdma_pio_spi_ring_adapter_get_snapshot(const tdma_pio_spi_ring_adapter_t *a,
                                           tdma_pio_spi_ring_adapter_snapshot_t *s) {
    (void)a; *s=adapter; return true;
}
static bool tdma_runtime_owner_get_phys_snapshot(tdma_pio_spi_phys_snapshot_t *s) { *s=phys; return true; }
''' + functions + r'''
int main(void) {
    for (parameter=0; parameter<=2; parameter++) {
        fields=0; assert(scpi_cmd_system_tdma_ring_burst(NULL)); assert(result[0]==parameter);
    }
    parameter=3; assert(!scpi_cmd_system_tdma_ring_burst(NULL));
    parameter=UINT32_MAX; assert(!scpi_cmd_system_tdma_ring_burst(NULL)); assert(setter_calls==3);
    parameter=1; parse_ok=false; assert(!scpi_cmd_system_tdma_ring_burst(NULL)); assert(setter_calls==3);
    parse_ok=true; setter_ok=false; assert(!scpi_cmd_system_tdma_ring_burst(NULL));
    staged_flags=2u<<TDMA_RING_FLAG_DIAGNOSTIC_BURST_SHIFT;
    fields=0; assert(scpi_cmd_system_tdma_ring_burst_q(NULL)); assert(result[0]==2);
    staged_ok=false; assert(!scpi_cmd_system_tdma_ring_burst_q(NULL));
    ring.config_seq=ring.applied_config_seq=9;
    adapter.diagnostic_burst_valid=1;
    adapter.diagnostic_burst_limit=2; adapter.diagnostic_burst_launched=2;
    phys.tx_count=1; phys.tx_timeout_count=1; phys.last_error=5; adapter.last_error=7;
    fields=reads=0; assert(scpi_cmd_system_tdma_ring_burst_status_q(NULL));
    assert(fields==8 && result[0]==1 && result[1]==2 && result[2]==2 && result[3]==1);
    assert(result[4]==1 && result[5]==5 && result[6]==7 && result[7]==9);
    /* Exhaustion must not synthesize completion or erase failures. */
    ring.enabled=1; fields=reads=0; assert(!scpi_cmd_system_tdma_ring_burst_status_q(NULL)); assert(!fields);
    ring.enabled=0; ring.adapter_started=1; assert(!scpi_cmd_system_tdma_ring_burst_status_q(NULL));
    ring.adapter_started=0; ring.applied_config_seq=8; assert(!scpi_cmd_system_tdma_ring_burst_status_q(NULL));
    ring.applied_config_seq=9; change_generation=true; fields=reads=0;
    assert(!scpi_cmd_system_tdma_ring_burst_status_q(NULL)); assert(!fields);
    change_generation=false; adapter.diagnostic_burst_valid=0; fields=reads=0;
    assert(!scpi_cmd_system_tdma_ring_burst_status_q(NULL)); assert(!fields);
    assert(errors==10); return 0;
}
''', encoding="utf-8")
    compile_run(tmp_path, "scpi", [unit])
