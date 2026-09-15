"""First RX window: real prelaunch/service, FIFO feed, counter and copy leaf.

No parser, SD, wire identity or DPLL eligibility is simulated. Register reads,
DMA writes during the copy, and concurrent guarded readers are explicit seams.
"""
import hashlib
import json
from pathlib import Path
import re

import pytest

from test_tdma_observer_prelaunch import prelaunch_source
from test_tdma_rx_event_candidate import run
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def lifecycle_source(source):
    programs = (ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c").read_text(encoding="utf-8")
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    declarations, routines = [], []
    signatures = [
        ("bool", "tdma_pio_spi_programs_is_flight_persona", "tdma_pio_spi_program_persona_t persona"),
        ("void", "tdma_pio_spi_programs_publish_lifecycle", "const tdma_pio_spi_program_manager_t *manager, tdma_pio_spi_phys_t *phys"),
        ("bool", "tdma_pio_spi_programs_transition", "tdma_pio_spi_program_manager_t *manager, tdma_pio_spi_phys_t *phys, tdma_pio_spi_persona_event_t event, tdma_pio_spi_program_persona_t persona"),
        ("void", "tdma_pio_spi_phys_unload_programs", "tdma_pio_spi_program_manager_t *manager"),
        ("void", "tdma_pio_spi_programs_rollback", "tdma_pio_spi_program_manager_t *manager, tdma_pio_spi_phys_t *phys, tdma_pio_spi_program_persona_t previous, tdma_pio_spi_program_persona_t failed_target, bool previous_resources_held"),
        ("bool", "tdma_pio_spi_programs_select", "tdma_pio_spi_program_manager_t *manager, tdma_pio_spi_phys_t *phys, tdma_pio_spi_program_persona_t persona"),
    ]
    for result, name, args in signatures:
        routines.append(f"static {result} {name if not name.endswith('programs_select') else 'first_real_select'}({args}) {{" + c_definition_body(programs, name) + "}\n")
    bodies = "\n".join(routines)
    fields = {field for field in re.findall(r"phys->snapshot\.(\w+)", bodies)
              if not re.search(rf"uint(?:32|64)_t {field};", source)}
    source = source.replace("uint32_t rx_observation_drop_count;", "uint32_t rx_observation_drop_count; " +
                            " ".join(f"uint32_t {field};" for field in sorted(fields)))
    source = source.replace("bool armed, rx_capture_active,", "bool flight_resource_claimed; bool armed, rx_capture_active,")
    for name, value in re.findall(r"(TDMA_PIO_SPI_PROGRAM_PERSONA_\w+)\s*=\s*(\d+u)", header):
        declarations.append(f"#define {name} {value}")
    declarations.append("#define TDMA_PIO_SPI_PROGRAM_PERSONA_MAX TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN")
    for name in sorted(set(re.findall(r"TDMA_PIO_SPI_PHYS_ERROR_\w+", bodies))):
        value = re.search(rf"{name}\s*=\s*(\d+u)", header).group(1)
        declarations.append(f"#ifndef {name}\n#define {name} {value}\n#endif")
    for name in sorted(set(re.findall(r"&([a-z_0-9]+_program)\b", bodies))):
        declarations.append(f"static const int {name};")
    # The full unload switch executes its true prefix and retirement, with
    # SDK instruction removal and addresses represented by observable seams.
    offsets = sorted(set(re.findall(r"\bs_tdma_[a-z_0-9]+_offset\b", bodies)))
    declarations.extend(f"#define {name} 0u" for name in offsets)
    appendix = "\n".join(declarations) + "\n" + PROGRAM_SEAMS
    appendix += (ROOT / "components/tdma/src/tdma_pio_spi_persona_fsm.c").read_text(encoding="utf-8")
    appendix += "\n#define s_tdma_pio_spi_program_persona (*manager->program_persona)\n" + bodies
    appendix += "\n#undef s_tdma_pio_spi_program_persona\n" + "\n".join(f"#undef {name}" for name in offsets)
    geometry = (ROOT / "components/tdma/src/tdma_pio_spi_phys_geometry.inc").read_text(encoding="utf-8")
    appendix += GEOMETRY_SEAMS
    for result, name, args in (
        ("void", "tdma_geometry_retire", "uint32_t reason"),
        ("void", "tdma_geometry_arm_failed", "void"),
        ("bool", "tdma_pio_spi_phys_geometry_arm_requested", "void *context, const tdma_ring_runtime_config_t *config"),
    ):
        appendix += f"static {result} {name}({args}) {{" + c_definition_body(geometry, name) + "}\n"
    physical = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    disarm = c_definition_body(physical, "tdma_pio_spi_phys_disarm")
    tail = disarm[disarm.rindex("    if (worker_retired && scanner_retired) {"):]
    appendix += "\nstatic bool first_physical_stop_tail(bool worker_retired, bool scanner_retired) {\n" + tail + "}\n"
    return source + appendix


PROGRAM_SEAMS = r'''
#include "tdma_pio_spi_persona_fsm.h"
#define PROJECT_TDMA_EVENT_OBSERVER 1
#define BOARD_TDMA_SPI_PIO (&bank)
#define BOARD_TDMA_TX_PIO (&bank)
#define BOARD_TDMA_RX_PIO (&rx_bank)
typedef struct { bool tx; const int *program; uint32_t offset; } tdma_origin_catalog_entry_t;
static const tdma_origin_catalog_entry_t s_origin_catalog[] = {{true,NULL,0u}};
typedef struct {
    tdma_pio_spi_persona_fsm_t lifecycle;
    tdma_pio_spi_program_persona_t *program_persona;
    bool *sms_claimed, *flight_sms_claimed, *maintenance_resources_claimed;
    int *tx_dma_channel, *rx_dma_channel, *command_dma_channel;
    uint32_t *event_sequence_offset, *event_counter_offset;
} tdma_pio_spi_program_manager_t;
static bool first_resources=true, first_quiesced=true, first_load=true;
static unsigned first_unloads, first_loads, first_releases;
static void pio_remove_program(PIO pio, const int *program, uint32_t offset) {
    (void)pio; (void)program; (void)offset; ++first_unloads;
    assert(!(s_tdma_rx_first_window.flags & TDMA_RX_FIRST_AVAILABLE));
}
static bool tdma_pio_spi_programs_current_persona_quiesced(const tdma_pio_spi_program_manager_t *m,
    const tdma_pio_spi_phys_t *p) { (void)m; (void)p; return first_quiesced; }
static bool tdma_pio_spi_programs_transfer_resources(tdma_pio_spi_program_manager_t *m,
    tdma_pio_spi_phys_t *p, tdma_pio_spi_program_persona_t previous, tdma_pio_spi_program_persona_t next) {
    (void)m; (void)p; (void)previous;
    return first_resources || next == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
}
static bool tdma_pio_spi_phys_load_programs(tdma_pio_spi_program_manager_t *m,
    tdma_pio_spi_program_persona_t persona) {
    (void)m; ++first_loads;
    return first_load || persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
}
static void tdma_pio_spi_programs_release_resources(tdma_pio_spi_program_manager_t *m,
    tdma_pio_spi_phys_t *p, tdma_pio_spi_program_persona_t persona) {
    (void)m; (void)p; (void)persona; ++first_releases;
}
'''

GEOMETRY_SEAMS = r'''
static tdma_frozen_geometry_snapshot_t s_geometry;
static bool s_geometry_active, s_geometry_pending_arm, s_geometry_request_pending, s_geometry_training_valid, s_geometry_physical_stopped;
static uint32_t s_geometry_request_config;
static void tdma_geometry_publish(void) { }
static void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *p, uint32_t reason) { p->snapshot.last_error=reason; }
'''


@pytest.mark.parametrize("capacity", [6, 8])
def test_first_window_production_boundaries(tmp_path, capacity):
    source = prelaunch_source(tmp_path)
    position = source.rindex("int main(void)")
    source = source[:position] + source[position:].replace(
        "int main(void)", "static int prelaunch_regression_main(void)", 1)
    source = lifecycle_source(source)
    cases = ROOT / "tests/unit/tdma_rx_first_window_cases.c"
    source = f"#define PROJECT_NODE_CAPACITY {capacity}\n" + source
    source += "\n" + cases.read_text(encoding="utf-8")
    # Avoid Windows CRT assertion dialogs obscuring actual failed assertions.
    source = source.replace('#include <assert.h>', '''#include <assert.h>
#include <stdlib.h>
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr, "ASSERT %s:%d: %s\\n", __FILE__, __LINE__, #c); fflush(stderr); _Exit(99); } } while (0)''', 1)
    observed = [ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc",
                ROOT / "components/tdma/src/tdma_pio_spi_phys.c",
                ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c",
                ROOT / "components/tdma/src/tdma_pio_spi_phys_geometry.inc",
                ROOT / "components/tdma/src/tdma_pio_spi_persona_fsm.c",
                ROOT / "components/tdma/inc/tdma_rx_first_window.h", cases]
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in observed}
    (tmp_path / "first-window-source-observations.json").write_text(json.dumps({
        "capacity": capacity, "observed_sources": hashes,
        "scope": "unchanged full archive + real prelaunch/service/STOP/feed/counter/copy leaf; volatile ring read and hardware MMIO seams",
    }, indent=2), encoding="utf-8")
    output = run(tmp_path, source, "first-window", enabled=True)
    assert "first-window: all production groups passed" in output
    assert hashes == {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in observed}
