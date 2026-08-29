from pathlib import Path

from tools.source_size_check.source_size_check import scan


def test_scan_reports_oversize_file_with_split_guidance(tmp_path: Path) -> None:
    source = tmp_path / "tdma_pio_spi_phys.c"
    source.write_text("line\n" * 1001, encoding="utf-8")
    result = scan(tmp_path, 1000)
    assert len(result) == 1
    assert result[0].path == "tdma_pio_spi_phys.c"
    assert result[0].lines == 1001
    assert "physical transport" in result[0].split_candidates


def test_scan_excludes_build_outputs(tmp_path: Path) -> None:
    output = tmp_path / "out" / "generated.c"
    output.parent.mkdir()
    output.write_text("line\n" * 2000, encoding="utf-8")
    assert scan(tmp_path, 1000) == []
