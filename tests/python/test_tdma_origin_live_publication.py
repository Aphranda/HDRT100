"""Exercise actual Core1 collection, guarded readback and SCPI serialization."""
import os
from pathlib import Path
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_live_publication_and_readback(tmp_path):
    origin = (ROOT / 'components/tdma/src/tdma_pio_spi_phys_origin.inc').read_text(encoding='utf-8')
    scpi = (ROOT / 'middleware/scpi_port/src/scpi_calibration_commands.c').read_text(encoding='utf-8')
    functions = [
        (origin, 'tdma_pio_spi_phys_origin_collect_live',
         'static void tdma_pio_spi_phys_origin_collect_live(tdma_pio_spi_phys_t *phys)'),
        (origin, 'tdma_pio_spi_phys_origin_observe',
         'bool tdma_pio_spi_phys_origin_observe(void *context, tdma_origin_observation_t *observation)'),
        (origin, 'tdma_pio_spi_phys_origin_get_live_snapshot',
         'bool tdma_pio_spi_phys_origin_get_live_snapshot(const tdma_pio_spi_phys_t *phys, tdma_origin_live_snapshot_t *out)'),
        (scpi, 'scpi_calibration_origin_record_fields',
         'static void scpi_calibration_origin_record_fields(scpi_t *context, const tdma_origin_record_frozen_t *snapshot)'),
        (scpi, 'scpi_calibration_origin_record_q',
         'scpi_result_t scpi_calibration_origin_record_q(scpi_t *context)'),
        (scpi, 'scpi_calibration_origin_live_q',
         'scpi_result_t scpi_calibration_origin_live_q(scpi_t *context)'),
    ]
    source = ''.join(signature + '{' + c_definition_body(text, name) + '}\n'
                     for text, name, signature in functions)
    (tmp_path / 'live_impl.inc').write_text(source, encoding='utf-8')
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path / ('live-publication.exe' if os.name == 'nt' else 'live-publication')
    command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-ffunction-sections', '-Wl,--gc-sections',
               '-I' + str(ROOT / 'components/tdma/inc'), '-I' + str(tmp_path),
               str(ROOT / 'tests/unit/tdma_origin_live_publication_cases.c'),
               str(ROOT / 'components/tdma/src/tdma_origin_exchange.c'), '-o', str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (tmp_path / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    (tmp_path / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
