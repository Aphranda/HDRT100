#!/usr/bin/env python3
"""Extract UTF-8 text from a PDF for repository-local document review."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from PyPDF2 import PdfReader


def parse_pages(spec: str | None, page_count: int) -> list[int]:
    if not spec:
        return list(range(page_count))
    pages: set[int] = set()
    for item in spec.split(","):
        bounds = item.strip().split("-", 1)
        start = int(bounds[0])
        end = int(bounds[-1])
        if start < 1 or end < start or end > page_count:
            raise ValueError(
                f"page range {item!r} outside 1..{page_count}")
        pages.update(range(start - 1, end))
    return sorted(pages)


def extract(pdf: Path, page_spec: str | None) -> tuple[int, str]:
    reader = PdfReader(str(pdf))
    selected = parse_pages(page_spec, len(reader.pages))
    chunks: list[str] = []
    for index in selected:
        text = reader.pages[index].extract_text() or ""
        chunks.append(f"\n===== PDF page {index + 1} =====\n{text.rstrip()}\n")
    return len(reader.pages), "".join(chunks)


def matching_lines(text: str, pattern: str, context: int) -> str:
    expression = re.compile(pattern, re.IGNORECASE)
    lines = text.splitlines()
    selected: set[int] = set()
    for index, line in enumerate(lines):
        if expression.search(line):
            selected.update(range(max(0, index - context),
                                  min(len(lines), index + context + 1)))
    output: list[str] = []
    previous = -2
    for index in sorted(selected):
        if index != previous + 1:
            output.append("--")
        output.append(lines[index])
        previous = index
    return "\n".join(output) + ("\n" if output else "")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pdf", type=Path)
    parser.add_argument("--pages", help="one-based ranges, for example 3,7-9")
    parser.add_argument("--match", help="case-insensitive regular expression")
    parser.add_argument("--context", type=int, default=3)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    if args.context < 0:
        parser.error("--context must be non-negative")
    if not args.pdf.is_file():
        parser.error(f"PDF does not exist: {args.pdf}")
    try:
        page_count, text = extract(args.pdf, args.pages)
    except (ValueError, re.error) as exc:
        parser.error(str(exc))
    if args.match:
        try:
            text = matching_lines(text, args.match, args.context)
        except re.error as exc:
            parser.error(str(exc))
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    print(f"pdf_pages={page_count}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
