from pathlib import Path

from tools.checks.check_source_lengths import find_large_sources, line_count


def test_line_count_reads_utf8_and_preserves_logical_lines(tmp_path: Path) -> None:
    source = tmp_path / "sample.c"
    source.write_text("/* 中文 */\n" + "x;\n" * 1000, encoding="utf-8")
    assert line_count(source) == 1001


def test_threshold_is_strictly_greater_than_limit(tmp_path: Path) -> None:
    root = tmp_path / "repo"
    source_dir = root / "components"
    source_dir.mkdir(parents=True)
    (source_dir / "ok.c").write_text("x;\n" * 1000, encoding="utf-8")
    (source_dir / "large.c").write_text("x;\n" * 1001, encoding="utf-8")

    result = find_large_sources(root, 1000)

    assert [(path.name, lines) for path, lines in result] == [("large.c", 1001)]
