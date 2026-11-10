"""SWITCH1/SWITCH2 pin mapping: SP8T stays on OUT1..OUT3, SP2T on its pad."""
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(source: str, name: str) -> str:
    start = re.search(rf"(?m)^(?:static )?(?:bool|void|uint32_t) {name}\(", source)
    assert start, name
    end = source.index("\n}\n", start.start()) + 3
    return source[start.start():end]


def test_switch2_drives_only_the_sp2t_pad(tmp_path: Path) -> None:
    source = (ROOT / "components/sync_io/src/sync_io_sequence.c").read_text(
        encoding="utf-8")
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef unsigned uint;
#define BOARD_SYNC_OUTPUT_BASE_PIN 16u
#define SP8T_OUTPUT_BITS 3u
#define SP8T_OUTPUT_MASK ((1u << SP8T_OUTPUT_BITS) - 1u)
#define GPIO_FUNC_SIO 5
#define GPIO_OUT 1
static uint32_t s_switch1 = 1u, s_switch2;
static uint32_t pin_function[32], pin_dir[32];
static uint32_t drive_mask, drive_value;
static uint32_t sp2t_level;
static bool sp2t_driven, sp2t_ok = true;
static bool legacy_ok = true;
static unsigned legacy_depth;
static uint32_t read_outputs_value;

static void gpio_set_function(uint pin, int fn) { pin_function[pin] = (uint32_t)fn; }
static void gpio_set_dir(uint pin, int dir) { pin_dir[pin] = (uint32_t)dir; }
static void gpio_put_masked(uint32_t mask, uint32_t value) {
    drive_mask = mask; drive_value = value;
}
static bool sync_io_sequence_legacy_begin(void) {
    if (!legacy_ok) return false;
    ++legacy_depth;
    return true;
}
static void sync_io_sequence_legacy_end(void) {
    assert(legacy_depth != 0u);
    --legacy_depth;
}
static bool sync_io_sp2t_write(bool level) {
    if (!sp2t_ok) return false;
    sp2t_driven = true;
    sp2t_level = level ? 1u : 0u;
    return true;
}
static bool sync_io_sp2t_read(bool *level) {
    *level = sp2t_level != 0u;
    return true;
}
static bool sync_io_sp2t_is_driven(void) { return sp2t_driven; }
static uint32_t sync_io_sequence_read_outputs(void) { return read_outputs_value; }
'''
    harness += function(source, "sync_io_sequence_set_switch")
    harness += function(source, "sync_io_sequence_get_switch")
    harness += r'''
int main(void) {
    /* Range and numbering are unchanged from the SP8T contract. */
    assert(!sync_io_sequence_set_switch(0u, 1u));
    assert(!sync_io_sequence_set_switch(3u, 1u));
    assert(!sync_io_sequence_set_switch(1u, 0u));
    assert(!sync_io_sequence_set_switch(1u, 9u));
    assert(!sync_io_sequence_set_switch(2u, 2u));
    assert(legacy_depth == 0u);

    /* SWITCH1 keeps driving the three SP8T bits and never OUT4. */
    assert(sync_io_sequence_set_switch(1u, 5u));
    assert(pin_dir[16] == GPIO_OUT && pin_dir[17] == GPIO_OUT &&
           pin_dir[18] == GPIO_OUT);
    assert(pin_function[16] == GPIO_FUNC_SIO);
    assert(pin_dir[19] == 0u && pin_function[19] == 0u);
    assert(drive_mask == (SP8T_OUTPUT_MASK << BOARD_SYNC_OUTPUT_BASE_PIN));
    assert(drive_value == (4u << BOARD_SYNC_OUTPUT_BASE_PIN));
    assert(s_switch1 == 5u);
    assert(legacy_depth == 0u);

    /* SWITCH2 leaves the main output group alone and drives only its pad. */
    drive_mask = drive_value = 0u;
    assert(sync_io_sequence_set_switch(2u, 1u));
    assert(drive_mask == 0u && drive_value == 0u);
    assert(pin_dir[19] == 0u && pin_function[19] == 0u);
    assert(sp2t_driven && sp2t_level == 1u);
    assert(s_switch2 == 1u);
    assert(legacy_depth == 0u);

    /* Readback: SP8T is physical, SP2T is the pad while it is driven. */
    read_outputs_value = 7u;
    assert(sync_io_sequence_get_switch(1u) == 8u);
    assert(sync_io_sequence_get_switch(2u) == 1u);
    sp2t_level = 0u;
    assert(sync_io_sequence_get_switch(2u) == 0u);

    /* An undriven line falls back to the last accepted command. */
    sp2t_driven = false;
    s_switch2 = 1u;
    assert(sync_io_sequence_get_switch(2u) == 1u);
    assert(sync_io_sequence_get_switch(9u) == 0u);

    /* The SMA lease makes both switches busy during a run. */
    legacy_ok = false;
    assert(!sync_io_sequence_set_switch(1u, 2u));
    assert(!sync_io_sequence_set_switch(2u, 1u));
    assert(legacy_depth == 0u);

    /* A rejected SP2T pad write must not publish a new target value. */
    legacy_ok = true;
    sp2t_ok = false;
    s_switch2 = 0u;
    assert(!sync_io_sequence_set_switch(2u, 1u));
    assert(s_switch2 == 0u);
    assert(legacy_depth == 0u);
    return 0;
}
'''
    path = tmp_path / "switch.c"
    path.write_text(harness, encoding="utf-8")
    cc = shutil.which("gcc")
    assert cc, "host gcc is required for switch mapping checks"
    executable = tmp_path / "switch.exe"
    built = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path),
         "-o", str(executable)],
        capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True,
                         timeout=10)
    assert ran.returncode == 0, ran.stdout + ran.stderr
