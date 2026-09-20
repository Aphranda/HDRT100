"""Lock Core0's RTOS tick to the board clock before scheduler startup."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.rindex(signature)
    brace = source.index("{", start)
    depth = 0
    for offset in range(brace, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:offset]
    raise AssertionError(f"unterminated function: {signature}")


def define_value(source: str, name: str) -> int:
    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+([0-9]+)(?:[uUlL]+)?\s*$",
        source, re.MULTILINE)
    assert match, name
    return int(match.group(1))


def test_board_clock_precedes_freertos_tick_setup():
    runtime = (ROOT / "application/src/app_runtime.c").read_text(encoding="utf-8")
    init = function_body(runtime, "bool app_runtime_init(void)")
    assert init.index("board_clock_init()") < init.index("osal_kernel_init()")
    assert init.index("board_clock_init()") < init.index("app_tasks_create_all()")

    board = (ROOT / "boards/rp2350_trig/src/board.c").read_text(encoding="utf-8")
    clock_init = function_body(board, "bool board_clock_init(void)")
    assert "clock_get_hz(clk_sys) == BOARD_SYS_CLOCK_HZ" in clock_init
    assert "set_sys_clock_hz(BOARD_SYS_CLOCK_HZ, true)" in clock_init
    board_init = function_body(board, "bool board_init(void)")
    assert board_init.index("board_clock_init()") < board_init.index("diagnostics_init()")

    port = (ROOT / "third_party/freertos/FreeRTOS-Kernel/portable/ThirdParty/"
            "Community-Supported-Ports/GCC/RP2350_ARM_NTZ/non_secure/port.c")
    tick_setup = function_body(
        port.read_text(encoding="utf-8"), "void vPortSetupTimerInterrupt( void )")
    assert "clock_get_hz( clk_sys ) / configTICK_RATE_HZ" in tick_setup


def test_freertos_and_board_clock_contracts_match():
    board = (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
        encoding="utf-8")
    freertos = (ROOT / "config/freertos/FreeRTOSConfig.h").read_text(
        encoding="utf-8")
    assert define_value(board, "BOARD_SYS_CLOCK_HZ") == define_value(
        freertos, "configCPU_CLOCK_HZ")
