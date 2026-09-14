"""Excluded artifacts must not be visited or hide live-source violations."""
import os
from pathlib import Path
import sys

from tools.checks import check_scpi_usb_namespace as namespace
from tools.flash_map import flash_inventory as flash
from tools import doc_regression_check as docs


def put(root, name, text):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return path


def forbid_descent(monkeypatch, paths):
    original = os.scandir
    visited = []
    forbidden = {p.resolve() for p in paths}

    def scan(path):
        resolved = Path(path).resolve()
        assert resolved not in forbidden, f'excluded tree was traversed: {path}'
        visited.append(resolved)
        return original(path)

    monkeypatch.setattr(os, 'scandir', scan)
    # Python 3.10 pathlib caches scandir on its accessor; cover both callers.
    accessor = getattr(Path(), '_accessor', None)
    if accessor is not None:
        monkeypatch.setattr(accessor, 'scandir', scan)
    return visited


def test_namespace_prunes_artifacts_and_still_rejects_live_use(tmp_path, monkeypatch, capsys):
    excluded = ['out', '.git', 'build', 'docs', 'third_party', 'src/.pytest-work']
    for name in excluded:
        put(tmp_path, name+'/nested/bad.c', 'SYST:USB:BAD\n')
    put(tmp_path, 'src/live.c', 'SYST:USB:BAD\n')
    put(tmp_path, 'src/.pytest-note.c', 'SYST:USB:IGNORED\n')
    put(tmp_path, 'src/readme.txt', 'SYST:USB:IGNORED\n')
    put(tmp_path, 'src/doc.md', 'ordinary source-side documentation\n')
    put(tmp_path, 'middleware/scpi_port/src/scpi_usb_control.c', 'SYST:USB:ALLOWED\n')
    expected = {p.relative_to(tmp_path) for p in tmp_path.rglob('*')
                if p.is_file() and p.suffix in namespace.SCAN_SUFFIXES
                and not any(part in namespace.SKIP_DIRS or part.startswith('.pytest')
                            for part in p.relative_to(tmp_path).parts)}
    forbid_descent(monkeypatch, [tmp_path/p for p in excluded])
    assert {rel for rel, path in namespace.iter_files(tmp_path)} == expected
    monkeypatch.setattr(sys, 'argv', ['check', '--root', str(tmp_path)])
    assert namespace.main() == 1
    output = capsys.readouterr().out
    assert 'src/live.c' in output and 'nested/bad.c' not in output


def test_flash_prunes_artifacts_without_losing_real_calls(tmp_path, monkeypatch):
    excluded = ['out', '.git', 'build', 'tests', 'tools', 'third_party', 'src/build-custom']
    for name in excluded:
        put(tmp_path, name+'/nested/bad.c', 'drv_flash_program_parked();\n')
    put(tmp_path, 'src/live.c', 'drv_flash_read();\ndrv_flash_xip_ptr();\n')
    put(tmp_path, 'src/build-ignored.c', 'drv_flash_program_parked();\n')
    put(tmp_path, 'src/readme.md', 'drv_flash_program_parked();\n')
    put(tmp_path, 'drivers/mcu/flash/src/drv_flash.c', 'drv_flash_program_parked();\n')
    upper = put(tmp_path, 'src/upper.C', 'drv_flash_read();\n')
    expected = {'src/live.c': {'operations': ['read', 'xip_ptr'], 'lines': [1, 2]}}
    # pathlib's platform-specific glob matching must survive the traversal change.
    if upper in set(tmp_path.rglob('*.c')):
        expected['src/upper.C'] = {'operations': ['read'], 'lines': [1]}
    forbid_descent(monkeypatch, [tmp_path/p for p in excluded])
    assert flash.scan_raw_references(tmp_path) == expected


def test_registry_requires_source_anchor_and_constants_ignore_artifact_copies(tmp_path, monkeypatch):
    put(tmp_path, 'docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md', 'Status: Active\n')
    put(tmp_path, 'components/inc/live.h', '#define LOCAL_CONSTANT 42\n')
    row = ('| TDMA-SCAN-01 | tdma | anchor | 1 | docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md '
           '| live.h | x | 2026-09-14 | active |\n')
    registry = put(tmp_path, 'docs/check/DOCS_REGISTRY.md', row)
    excluded = ['out', '.git', 'build', 'node_modules', 'third_party']
    for name in excluded:
        put(tmp_path, name+'/nested/ghost.h', '#define LOCAL_CONSTANT 999\n')
    forbid_descent(monkeypatch, [tmp_path/p for p in excluded])
    good = docs.Result([], [])
    docs.check_registry(tmp_path, good)
    assert not good.failures
    assert docs.code_define_map(tmp_path)['LOCAL_CONSTANT'] == '42'
    registry.write_text(row.replace('live.h', 'ghost.h'), encoding='utf-8')
    missing = docs.Result([], [])
    docs.check_registry(tmp_path, missing)
    assert any('code_anchor' in failure for failure in missing.failures)
