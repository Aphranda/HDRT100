"""Exercise the real non-MPU PendSV FP stack layout across task switches.

The host model covers the software FP bank and conditional stack offset;
hardware exception entry/return and silicon timing still require HIL.
"""
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'third_party/freertos/FreeRTOS-Kernel/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ/non_secure/portasm.c'


def compile_handler(directory, disabled=False):
    compiler = shutil.which('arm-none-eabi-gcc')
    if not compiler:
        candidates = sorted((Path.home()/'.pico-sdk/toolchain').glob('*/bin/arm-none-eabi-gcc.exe'))
        if not candidates:
            pytest.skip('RP2350 ARM cross compiler unavailable')
        compiler = str(candidates[-1])
    objdump = Path(compiler).with_name('arm-none-eabi-objdump' + ('.exe' if compiler.endswith('.exe') else ''))
    source = PORT.read_text(encoding='utf-8')
    # The final definition is the actual configENABLE_MPU == 0 implementation.
    definition = source[source.rfind('    void PendSV_Handler'):]
    body = c_definition_body(definition, 'PendSV_Handler')
    override = '#undef configENABLE_FPU\n#define configENABLE_FPU 0\n' if disabled else ''
    unit = directory/'context.c'
    unit.write_text('#include <stdint.h>\n#include "FreeRTOSConfig.h"\n' + override +
        '#define portUSE_DCP_SAVE_RESTORE 1\n#define SIO_BASE 0xd0000000u\n'
        'uint32_t pxCurrentTCB;\nvoid vTaskSwitchContext(void) {}\n'
        'void PendSV_Handler(void) __attribute__((naked));\n'
        'void PendSV_Handler(void) {' + body + '}\n', encoding='utf-8')
    elf = directory/'context.elf'
    subprocess.run([compiler, '-mcpu=cortex-m33', '-mthumb', '-mfloat-abi=softfp', '-mfpu=fpv5-sp-d16',
        '-O2', '-nostdlib', '-Wl,-e,PendSV_Handler', '-I'+str(ROOT/'config/freertos'), str(unit), '-o', str(elf)],
        check=True, timeout=60)
    dis = subprocess.check_output([str(objdump), '-d', str(elf)], text=True)
    (directory/'context.dis').write_text(dis, encoding='utf-8')
    match = re.search(r'<PendSV_Handler>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)', dis, re.S)
    assert match
    return match[1]


class ContextLayout:
    """Interpret the emitted FP transfers and stack offsets around the switch."""
    def __init__(self, disassembly):
        save, restore = re.split(r'^.*\tbl\s+.*<vTaskSwitchContext>.*$', disassembly, flags=re.M)
        self.save_fp = self.fp_transfer(save, 'vstmdb', 'r1', 'lr')
        self.restore_fp = self.fp_transfer(restore, 'vldmia', 'r0', 'r3')
        # Real basic register frame and the RP2350 DCP scratch are retained.
        assert re.search(r'stmdb\s+r1!, \{r2, r3, r4, r5, r6, r7, r8, r9, sl, fp\}', save)
        assert re.search(r'ldmia(?:\.w)?\s+r0!, \{r2, r3, r4, r5, r6, r7, r8, r9, sl, fp\}', restore)
        assert re.search(r'stmdb\s+r1, \{r4, r5, r6, r7, r8, r9\}', save)
        assert re.search(r'subs\s+r0, #24', restore)
        assert re.search(r'ldmia(?:\.w)?\s+r0!, \{r4, r5, r6, r7, r8, r9\}', restore)

    @staticmethod
    def fp_transfer(text, opcode, pointer, condition_register):
        transfers = re.findall(r'\t(v(?:stm|ldm)\w*)\s+([^\n]+)', text)
        if not transfers:
            return False
        assert len(transfers) == 1
        mnemonic, operands = transfers[0]
        assert mnemonic == opcode+'eq'
        assert operands.strip() == pointer+'!, {s16-s31}'
        assert re.search(r'tst(?:\.w)?\s+'+condition_register+r', #16', text)
        assert re.search(r'\bit\s+eq', text)
        return True

    def save(self, bank, psp, exc_return, core, dcp):
        memory = {}
        pointer = psp
        extended = not (exc_return & 16)
        if self.save_fp and extended:
            pointer -= 64
            memory.update({pointer+i*4: word for i, word in enumerate(bank)})
        pointer -= 40
        memory.update({pointer+i*4: word for i, word in enumerate([core[0], exc_return, *core[1:]])})
        memory.update({pointer-24+i*4: word for i, word in enumerate(dcp)})
        return pointer, memory

    def restore(self, frame, bank):
        pointer, memory = frame
        dcp = [memory[pointer-24+i*4] for i in range(6)]
        words = [memory[pointer+i*4] for i in range(10)]
        pointer += 40
        exc_return = words[1]
        if self.restore_fp and not (exc_return & 16):
            bank[:] = [memory[pointer+i*4] for i in range(16)]
            pointer += 64
        return pointer, exc_return, [words[0], *words[2:]], dcp


@pytest.fixture(scope='module')
def handlers(tmp_path_factory):
    return [ContextLayout(compile_handler(tmp_path_factory.mktemp('fp-'+str(disabled)), disabled))
            for disabled in [False, True]]


@pytest.mark.parametrize('basic_between', [False, True])
def test_preempted_fp_task_preserves_bank_and_stack(handlers, basic_between):
    layout = handlers[0]
    a = [0xA0000000+i for i in range(16)]
    b = [0xB0000000+i for i in range(16)]
    core_a, core_b = list(range(9)), list(range(10, 19))
    dcp_a, dcp_b = list(range(6)), list(range(20, 26))
    bank = a.copy()
    frame_a = layout.save(bank, 0x20001000, 0xffffffed, core_a, dcp_a)
    bank[:] = b
    exc_b = 0xfffffffd if basic_between else 0xffffffed
    frame_b = layout.save(bank, 0x20002000, exc_b, core_b, dcp_b)
    assert layout.restore(frame_a, bank) == (0x20001000, 0xffffffed, core_a, dcp_a)
    assert bank == a
    assert layout.restore(frame_b, bank) == (0x20002000, exc_b, core_b, dcp_b)
    assert bank == (a if basic_between else b)
    assert len(frame_b[1]) == (16 if basic_between else 32)


def test_disabled_context_reproduces_cross_task_corruption(handlers):
    layout = handlers[1]
    bank = list(range(16))
    frame = layout.save(bank, 0x20001000, 0xffffffed, list(range(9)), list(range(6)))
    bank[:] = [0xdead0000+i for i in range(16)]
    layout.restore(frame, bank)
    assert bank != list(range(16))
