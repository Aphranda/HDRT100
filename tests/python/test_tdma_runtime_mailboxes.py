"""Exercise the actual product ARM map factory over runtime node counts."""
import os
from pathlib import Path
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_arm_map_uses_configured_nodes_within_static_capacity(tmp_path):
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    body = c_definition_body(source, "distributed_refmem_default_flight_map")
    case = tmp_path / "runtime_map.c"
    case.write_text('''#include <assert.h>
#include <string.h>
#include "tdma_flight_engine.h"
#include "tdma_profile.h"
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE TDMA_FLIGHT_SHORT_SLOT_SIZE
static tdma_process_image_map_t make_map(uint32_t node_count) {
''' + body + '''
}
int main(void) {
    for (uint32_t nodes = 0; nodes <= 8; ++nodes) {
        tdma_process_image_map_t map = make_map(nodes);
        tdma_process_image_map_result_t result;
        bool valid = tdma_process_image_map_validate(&map, &result);
        assert(valid == (nodes >= 2 && nodes <= 6));
        if (!valid) continue;
        assert(map.payload_size == nodes * 32 + 4 && map.segment_count == nodes);
        assert(map.map_crc32 == tdma_process_image_map_crc32(&map));
        for (uint32_t slot = 0; slot < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; ++slot) {
            assert(map.segment[slot].used == (slot < nodes));
            if (slot >= nodes) continue;
            assert(map.segment[slot].owner_slot_id == slot);
            assert(map.segment[slot].byte_offset == slot * 32);
            assert(map.segment[slot].byte_length == 32);
            assert(map.segment[slot].flags == TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE);
        }
    }
    return 0;
}
''', encoding="utf-8")
    gcc = shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = tmp_path / ("runtime_map.exe" if os.name == "nt" else "runtime_map")
    subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-DPROJECT_NODE_CAPACITY=6",
                    "-I" + str(ROOT / "components/tdma/inc"), str(case),
                    str(ROOT / "components/tdma/src/tdma_process_image_map.c"),
                    str(ROOT / "components/tdma/src/tdma_transport_frame.c"), "-o", str(exe)],
                   check=True, capture_output=True, text=True)
    subprocess.run([str(exe)], check=True, capture_output=True, text=True)
