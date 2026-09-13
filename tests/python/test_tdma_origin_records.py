"""Compile actual stopped-archive lifecycle functions against controlled devices."""
import os
from pathlib import Path
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_origin_record_frozen_lifetime(tmp_path):
    phys = (ROOT/'components/tdma/src/tdma_pio_spi_phys.c').read_text(encoding='utf-8')
    origin = (ROOT/'components/tdma/src/tdma_pio_spi_phys_origin.inc').read_text(encoding='utf-8')
    functions = [
        (phys,'tdma_pio_spi_phys_origin_record_invalidate',
         'static void tdma_pio_spi_phys_origin_record_invalidate(tdma_pio_spi_phys_t *phys)'),
        (phys,'tdma_pio_spi_phys_stop_command_dma',
         'static bool tdma_pio_spi_phys_stop_command_dma(tdma_pio_spi_phys_t *phys)'),
        (phys,'tdma_pio_spi_phys_select_program_persona',
         'bool tdma_pio_spi_phys_select_program_persona(tdma_pio_spi_phys_t *phys, tdma_pio_spi_program_persona_t persona)'),
        (origin,'tdma_pio_spi_phys_origin_get_frozen_record',
         'bool tdma_pio_spi_phys_origin_get_frozen_record(const tdma_pio_spi_phys_t *phys, uint32_t age, tdma_origin_record_frozen_t *out)'),
    ]
    source = ''.join(signature+'{'+c_definition_body(text,name)+'}\n'
                     for text,name,signature in functions)
    (tmp_path/'origin_record_frozen_impl.inc').write_text(source,encoding='utf-8')
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path/('origin-frozen.exe' if os.name=='nt' else 'origin-frozen')
    command = [compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
               '-I'+str(ROOT/'components/tdma/inc'),'-I'+str(tmp_path),
               str(ROOT/'tests/unit/tdma_origin_record_frozen_cases.c'),
               str(ROOT/'components/tdma/src/tdma_origin_build_job.c'),'-o',str(exe)]
    subprocess.run(command,check=True,capture_output=True,text=True,timeout=60)
    result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
    assert result.returncode==0,result.stdout+result.stderr
