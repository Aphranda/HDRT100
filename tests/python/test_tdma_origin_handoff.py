"""Exercise the bounded recorder without any hardware or clock side effects."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_origin_handoff_intervals(tmp_path):
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path/('handoff.exe' if os.name == 'nt' else 'handoff')
    command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I'+str(ROOT/'components/tdma/inc'), str(ROOT/'tests/unit/test_tdma_origin_handoff.c'),
        str(ROOT/'components/tdma/src/tdma_origin_handoff.c'), '-o', str(exe)]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
