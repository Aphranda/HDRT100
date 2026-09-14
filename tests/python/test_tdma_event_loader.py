"""Run production follower installation/retirement on actual assembled programs.

The narrow host boundary replaces SDK PIO instruction-memory operations with a
32-word owner map. It executes the production loader, latch helper, and matching
unload branch; program lengths and fixed origins come from current pioasm output.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c"


def assembled_programs(directory: Path, enabled: bool) -> str:
    pioasm = os.environ.get("PIOASM") or shutil.which("pioasm") or str(
        Path.home() / ".pico-sdk/tools/2.2.0/pioasm/pioasm.exe")
    wanted = {
        "tdma_pio_spi": ["tdma_pio_spi_flight_control_forward",
                         "tdma_pio_spi_flight_process_follower",
                         "tdma_pio_spi_flight_clock_latch"],
    }
    if enabled:
        wanted["tdma_event"] = ["tdma_event_counter", "tdma_event_sequence"]
    declarations = []
    for source, names in wanted.items():
        header = directory / f"{source}.pio.h"
        completed = subprocess.run([
            pioasm, "-o", "c-sdk", str(ROOT / f"components/tdma/src/{source}.pio"),
            str(header)], capture_output=True, text=True)
        assert completed.returncode == 0, completed.stdout + completed.stderr
        text = header.read_text(encoding="utf-8")
        for name in names:
            for pattern in (
                rf"(?m)^#define {name}_pio_version \d+\s*$",
                rf"static const uint16_t {name}_program_instructions\[\] = \{{.*?\n\}};",
                rf"static const struct pio_program {name}_program = \{{.*?\n\}};",
            ):
                found = re.search(pattern, text, re.S if "static const" in pattern else 0)
                assert found, (name, pattern)
                declarations.append(found.group(0))
    return "\n".join(declarations) + "\n"


def production_routines(enabled: bool) -> str:
    source = PROGRAMS.read_text(encoding="utf-8")
    macros = "\n".join(line for line in source.splitlines()
                       if line.startswith("#define s_tdma_pio_spi_"))
    routines = []
    if not enabled:
        name = "tdma_pio_spi_phys_load_flight_clock_latch_program"
        routines.append("static bool " + name +
            "(tdma_pio_spi_program_manager_t *manager, PIO pio, bool rx_latch) {" +
            c_definition_body(source, name) + "}\n")
    name = "tdma_pio_spi_phys_load_flight_process_follower_programs"
    routines.append("static bool " + name +
        "(tdma_pio_spi_program_manager_t *manager) {" +
        c_definition_body(source, name) + "}\n")
    unload = c_definition_body(source, "tdma_pio_spi_phys_unload_programs")
    first = unload.index("case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:")
    last = unload.index("default:", first)
    # Keep the complete production case (including both feature branches) and
    # the post-switch persona retirement; unrelated personas are out of scope.
    routines.append("static void tdma_pio_spi_phys_unload_programs("
        "tdma_pio_spi_program_manager_t *manager) {"
        "switch (s_tdma_pio_spi_program_persona) {" + unload[first:last] +
        "default: break;}" + unload.rsplit("}", 1)[1] + "}\n")
    return macros + "\n" + "\n".join(routines)


FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned uint;
#define PICO_PIO_VERSION 1
typedef struct pio_program {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin;
    uint8_t pio_version;
    uint32_t used_gpio_ranges;
} pio_program_t;
typedef struct {
    const pio_program_t *owner[32];
    uint16_t memory[32];
} bank_t;
typedef bank_t *PIO;
static bank_t banks[3];
#define BOARD_TDMA_TX_PIO (&banks[1])
#define BOARD_TDMA_RX_PIO (&banks[2])
enum { TDMA_PIO_SPI_PROGRAM_PERSONA_NONE = 0,
       TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER = 9 };
typedef unsigned tdma_pio_spi_program_persona_t;
typedef struct {
    uint *flight_control_forward_offset, *flight_process_follower_offset;
    uint *flight_clock_latch_offset, *flight_rx_clock_latch_offset;
    uint *event_counter_offset, *event_sequence_offset;
    tdma_pio_spi_program_persona_t *program_persona;
} tdma_pio_spi_program_manager_t;
typedef struct { PIO pio; const pio_program_t *program; uint offset; } operation_t;
static operation_t additions[8], removals[8];
static size_t add_count, remove_count, can_count, fixed_checks, dynamic_checks;
static const uint16_t peer_instruction = 0xa55au;
static const pio_program_t peer = {.instructions = &peer_instruction, .length = 1u, .origin = -1};

static bool fits(PIO pio, const pio_program_t *program, uint offset) {
    if (program->length == 0u || program->length > 32u ||
        offset > 32u - program->length ||
        (program->origin >= 0 && offset != (uint)program->origin)) return false;
    for (uint i = offset; i < offset + program->length; ++i)
        if (pio->owner[i] != NULL) return false;
    return true;
}
static int find_offset(PIO pio, const pio_program_t *program) {
    if (program->origin >= 0) return fits(pio, program, (uint)program->origin)
                                    ? program->origin : -1;
    for (int offset = 32 - program->length; offset >= 0; --offset)
        if (fits(pio, program, (uint)offset)) return offset;
    return -1;
}
static bool pio_can_add_program(PIO pio, const pio_program_t *program) {
    ++can_count; ++dynamic_checks;
    return find_offset(pio, program) >= 0;
}
#if PROJECT_TDMA_EVENT_OBSERVER
static bool pio_can_add_program_at_offset(PIO pio, const pio_program_t *program, uint offset) {
    ++can_count; ++fixed_checks;
    return fits(pio, program, offset);
}
#endif
static void add_at(PIO pio, const pio_program_t *program, uint offset) {
    assert(fits(pio, program, offset));
    assert(add_count < 8u);
    additions[add_count++] = (operation_t){pio, program, offset};
    for (uint i = 0u; i < program->length; ++i) {
        pio->owner[offset + i] = program;
        pio->memory[offset + i] = program->instructions[i];
    }
}
#if PROJECT_TDMA_EVENT_OBSERVER
static void pio_add_program_at_offset(PIO pio, const pio_program_t *program, uint offset) {
    add_at(pio, program, offset);
}
#endif
static int pio_add_program(PIO pio, const pio_program_t *program) {
    const int offset = find_offset(pio, program);
    assert(offset >= 0);
    add_at(pio, program, (uint)offset);
    return offset;
}
static void pio_remove_program(PIO pio, const pio_program_t *program, uint offset) {
    assert(offset <= 32u - program->length);
    assert(remove_count < 8u);
    removals[remove_count++] = (operation_t){pio, program, offset};
    for (uint i = 0u; i < program->length; ++i) {
        assert(pio->owner[offset + i] == program); /* Never erase a peer/uninstalled range. */
        pio->owner[offset + i] = NULL;
        pio->memory[offset + i] = 0u;
    }
}
static void put_peer(PIO pio, uint offset) {
    assert(pio->owner[offset] == NULL);
    pio->owner[offset] = &peer;
    pio->memory[offset] = peer_instruction;
}
static void only_peers_remain(void) {
    for (uint bank = 0u; bank < 3u; ++bank)
        for (uint word = 0u; word < 32u; ++word) {
            assert(banks[bank].owner[word] == NULL || banks[bank].owner[word] == &peer);
            if (banks[bank].owner[word] == &peer) assert(banks[bank].memory[word] == peer_instruction);
        }
}
static void reset(void) {
    memset(banks, 0, sizeof(banks));
    memset(additions, 0, sizeof(additions));
    memset(removals, 0, sizeof(removals));
    add_count = remove_count = can_count = fixed_checks = dynamic_checks = 0u;
    put_peer(&banks[0], 7u); /* Unrelated PIO must survive every operation. */
}
'''


ASSERTIONS = r'''
int main(void) {
    /* Assert assembler output, not manually substituted program lengths. */
    assert(tdma_pio_spi_flight_control_forward_program.length == 10u);
    assert(tdma_pio_spi_flight_process_follower_program.length == 28u);
    assert(tdma_pio_spi_flight_clock_latch_program.length == 4u);
    uint control = 99u, process = 99u, tx_latch = 99u, rx_latch = 99u;
    uint counter = 99u, sequence = 99u;
    tdma_pio_spi_program_persona_t persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
    tdma_pio_spi_program_manager_t manager = {
        .flight_control_forward_offset = &control, .flight_process_follower_offset = &process,
        .flight_clock_latch_offset = &tx_latch, .flight_rx_clock_latch_offset = &rx_latch,
        .event_counter_offset = &counter, .event_sequence_offset = &sequence,
        .program_persona = &persona
    };
#if PROJECT_TDMA_EVENT_OBSERVER
    assert(tdma_event_counter_program.length == 10u && tdma_event_counter_program.origin == 10);
    assert(tdma_event_sequence_program.length == 12u && tdma_event_sequence_program.origin == 20);
    for (unsigned fail = 0u; fail < 5u; ++fail) {
        reset();
        /* Real owner occupancy makes the chosen installation step impossible.
         * RX16 blocks every 28-word placement; RX0 only blocks the final latch. */
        const uint blocked_word[] = {0u, 10u, 20u, 16u, 0u};
        PIO blocked_pio = fail < 3u ? BOARD_TDMA_TX_PIO : BOARD_TDMA_RX_PIO;
        put_peer(blocked_pio, blocked_word[fail]);
        assert(!tdma_pio_spi_phys_load_flight_process_follower_programs(&manager));
        assert(add_count == fail && remove_count == fail && can_count == fail + 1u);
        for (unsigned i = 0u; i < fail; ++i) {
            assert(removals[i].pio == additions[fail - 1u - i].pio);
            assert(removals[i].program == additions[fail - 1u - i].program);
            assert(removals[i].offset == additions[fail - 1u - i].offset);
        }
        assert(blocked_pio->owner[blocked_word[fail]] == &peer);
        only_peers_remain();
    }
    for (unsigned which = 0u; which < 2u; ++which) {
        reset();
        tdma_pio_spi_program_manager_t invalid = manager;
        if (which == 0u) invalid.event_counter_offset = NULL;
        else invalid.event_sequence_offset = NULL;
        assert(!tdma_pio_spi_phys_load_flight_process_follower_programs(&invalid));
        assert(add_count == 0u && remove_count == 0u && can_count == 0u);
        only_peers_remain();
    }
#endif
    reset();
    assert(tdma_pio_spi_phys_load_flight_process_follower_programs(&manager));
    assert(remove_count == 0u);
    assert(process == 4u && rx_latch == 0u);
    for (uint word = 0u; word < 32u; ++word) assert(BOARD_TDMA_RX_PIO->owner[word] != NULL);
#if PROJECT_TDMA_EVENT_OBSERVER
    assert(add_count == 5u && fixed_checks == 3u && dynamic_checks == 2u);
    assert(control == 0u && counter == 10u && sequence == 20u);
    assert(tx_latch == 99u); /* No stale one-shot latch installed on TX. */
    for (uint word = 0u; word < 32u; ++word) {
        const pio_program_t *expected = word < 10u ? &tdma_pio_spi_flight_control_forward_program :
            word < 20u ? &tdma_event_counter_program : &tdma_event_sequence_program;
        assert(BOARD_TDMA_TX_PIO->owner[word] == expected);
    }
#else
    assert(add_count == 4u && fixed_checks == 0u && dynamic_checks == 4u);
    assert(control == 22u && tx_latch == 18u);
    assert(counter == 99u && sequence == 99u);
    for (uint word = 0u; word < 32u; ++word) {
        const pio_program_t *expected = word < 18u ? NULL :
            word < 22u ? &tdma_pio_spi_flight_clock_latch_program :
                         &tdma_pio_spi_flight_control_forward_program;
        assert(BOARD_TDMA_TX_PIO->owner[word] == expected);
    }
#endif
    tdma_pio_spi_phys_unload_programs(&manager);
    assert(remove_count == add_count);
    assert(persona == TDMA_PIO_SPI_PROGRAM_PERSONA_NONE);
    only_peers_remain();
    const size_t removed = remove_count;
    tdma_pio_spi_phys_unload_programs(&manager);
    assert(remove_count == removed); /* Retired persona cannot unload a peer later. */
    puts("production follower loader: assembled layout, rollback and retirement passed");
    return 0;
}
'''


@pytest.mark.parametrize("enabled", [True, False], ids=["observer-on", "legacy-off"])
def test_production_follower_loader_and_unload(tmp_path: Path, enabled: bool) -> None:
    unit = tmp_path / "loader.c"
    unit.write_text(FIXTURE + assembled_programs(tmp_path, enabled) +
                    production_routines(enabled) + ASSERTIONS, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "loader.exe"
    built = subprocess.run([
        gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        f"-DPROJECT_TDMA_EVENT_OBSERVER={int(enabled)}", str(unit), "-o", str(executable)],
        capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True)
    assert ran.returncode == 0, ran.stdout + ran.stderr
    assert "assembled layout, rollback and retirement passed" in ran.stdout
