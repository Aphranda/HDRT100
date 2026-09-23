"""Execute startup order and idempotent board clock setup from production C."""
from pathlib import Path
import re

from test_tdma_diagnostic_burst import compile_run
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_board_and_kernel_clock_contract_match():
    def value(path, name):
        source = (ROOT / path).read_text(encoding='utf-8')
        match = re.search(rf'^#define\s+{name}\s+(\d+)[uUlL]*\s*$', source, re.MULTILINE)
        assert match, name
        return int(match.group(1))
    assert value('boards/rp2350_trig/inc/board_config.h', 'BOARD_SYS_CLOCK_HZ') == value(
        'config/freertos/FreeRTOSConfig.h', 'configCPU_CLOCK_HZ')


def test_clock_is_established_before_kernel_and_tasks(tmp_path):
    runtime = (ROOT / 'application/src/app_runtime.c').read_text(encoding='utf-8')
    board = (ROOT / 'boards/rp2350_trig/src/board.c').read_text(encoding='utf-8')
    body = c_definition_body(board, 'board_clock_init')
    init = c_definition_body(runtime, 'app_runtime_init')
    source = tmp_path / 'clock.c'
    source.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define BOARD_SYS_CLOCK_HZ 250000000u
#define clk_sys 0
static uint32_t frequency;
static unsigned sets, kernels, tasks;
static bool set_ok, set_takes_effect, kernel_ok, task_ok;
static uint32_t clock_get_hz(int clock) { assert(clock == clk_sys); return frequency; }
static bool set_sys_clock_hz(uint32_t hz, bool required) {
    assert(hz == BOARD_SYS_CLOCK_HZ && required); sets++;
    if (set_ok && set_takes_effect) frequency = hz;
    return set_ok;
}
static bool osal_kernel_init(void) {
    assert(frequency == BOARD_SYS_CLOCK_HZ); kernels++; return kernel_ok;
}
static bool app_tasks_create_all(void) {
    assert(kernels && kernel_ok); tasks++; return task_ok;
}
''' + 'bool board_clock_init(void) {' + body + '}\n'
        + 'bool app_runtime_init(void) {' + init + '}\n' + r'''
int main(void) {
    frequency = 150000000u;
    assert(!app_runtime_init());
    assert(sets == 1 && kernels == 0 && tasks == 0);
    set_ok = true;
    assert(!app_runtime_init()); /* SDK success with wrong readback is rejected. */
    assert(sets == 2 && kernels == 0 && tasks == 0);
    set_takes_effect = true;
    assert(!app_runtime_init()); /* Kernel failure never creates tasks. */
    assert(sets == 3 && kernels == 1 && tasks == 0);
    kernel_ok = true;
    assert(!app_runtime_init());
    assert(sets == 3 && kernels == 2 && tasks == 1);
    task_ok = true;
    assert(app_runtime_init());
    assert(sets == 3 && kernels == 3 && tasks == 2);
    assert(board_clock_init()); /* Later board_init must not change a live clock. */
    assert(sets == 3);
    return 0;
}
''', encoding='utf-8')
    compile_run(tmp_path, 'clock', [source])
    board_init = c_definition_body(board, 'board_init')
    assert board_init.index('board_clock_init()') < board_init.index('diagnostics_init()')
    # The port remains the sole tick owner: bringup must not reset a live tick.
    assert 'systick_hw->' not in runtime
    assert 'app_runtime_rebind_kernel_tick' not in runtime
    port = ROOT / ('third_party/freertos/FreeRTOS-Kernel/portable/ThirdParty/'
                   'Community-Supported-Ports/GCC/RP2350_ARM_NTZ/non_secure/port.c')
    assert 'clock_get_hz( clk_sys ) / configTICK_RATE_HZ' in port.read_text(encoding='utf-8')
