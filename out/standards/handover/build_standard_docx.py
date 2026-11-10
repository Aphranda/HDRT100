#!/usr/bin/env python3
"""Render the general project management standard Markdown into a styled DOCX."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "docs" / "standards" / "STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md"
OUT = ROOT / "docs" / "standards" / "STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.docx"

DARK = "1F3864"
ACCENT = "2E74B5"
GRAY = "595959"
LIGHT = "F2F2F2"
BORDER = "BFBFBF"
BAND = "F5F8FC"

BODY_FONT = "宋体"
HEAD_FONT = "微软雅黑"
CODE_FONT = "Consolas"


def set_run_font(run, name: str, eastasia: str, size: float | None = None,
                 bold: bool | None = None, color: str | None = None) -> None:
    run.font.name = name
    run._element.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), eastasia)
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.font.bold = bold
    if color:
        run.font.color.rgb = RGBColor.from_string(color)


def shade_paragraph(p, fill: str) -> None:
    pPr = p._p.get_or_add_pPr()
    shd = pPr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        pPr.insert_element_before(
            shd,
            "w:tabs", "w:suppressAutoHyphens", "w:kinsoku", "w:wordWrap",
            "w:overflowPunct", "w:topLinePunct", "w:autoSpaceDE", "w:autoSpaceDN",
            "w:bidi", "w:adjustRightInd", "w:snapToGrid", "w:spacing", "w:ind",
            "w:contextualSpacing", "w:mirrorIndents", "w:suppressOverlap", "w:jc",
            "w:textDirection", "w:textAlignment", "w:textboxTightWrap",
            "w:outlineLvl", "w:divId", "w:cnfStyle", "w:rPr", "w:sectPr",
            "w:pPrChange",
        )
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)


def shade_cell(cell, fill: str) -> None:
    tcPr = cell._tc.get_or_add_tcPr()
    shd = tcPr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        tcPr.insert_element_before(
            shd,
            "w:noWrap", "w:tcMar", "w:textDirection", "w:tcFitText", "w:vAlign",
            "w:hideMark", "w:headers", "w:cellIns", "w:cellDel", "w:cellMerge",
            "w:tcPrChange",
        )
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)


def add_bottom_border(p, color: str, size: int = 6) -> None:
    pPr = p._p.get_or_add_pPr()
    pBdr = pPr.find(qn("w:pBdr"))
    if pBdr is None:
        pBdr = OxmlElement("w:pBdr")
        pPr.insert_element_before(
            pBdr,
            "w:shd", "w:tabs", "w:suppressAutoHyphens", "w:kinsoku", "w:wordWrap",
            "w:overflowPunct", "w:topLinePunct", "w:autoSpaceDE", "w:autoSpaceDN",
            "w:bidi", "w:adjustRightInd", "w:snapToGrid", "w:spacing", "w:ind",
            "w:contextualSpacing", "w:mirrorIndents", "w:suppressOverlap", "w:jc",
            "w:textDirection", "w:textAlignment", "w:textboxTightWrap",
            "w:outlineLvl", "w:divId", "w:cnfStyle", "w:rPr", "w:sectPr",
            "w:pPrChange",
        )
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), str(size))
    bottom.set(qn("w:space"), "2")
    bottom.set(qn("w:color"), color)
    pBdr.append(bottom)


def set_cell_margins(cell, top=40, start=80, bottom=40, end=80) -> None:
    tcPr = cell._tc.get_or_add_tcPr()
    mar = tcPr.find(qn("w:tcMar"))
    if mar is None:
        mar = OxmlElement("w:tcMar")
        tcPr.insert_element_before(
            mar,
            "w:textDirection", "w:tcFitText", "w:vAlign", "w:hideMark",
            "w:headers", "w:cellIns", "w:cellDel", "w:cellMerge", "w:tcPrChange",
        )
    for tag, value in (("top", top), ("left", start), ("bottom", bottom), ("right", end)):
        el = OxmlElement(f"w:{tag}")
        el.set(qn("w:w"), str(value))
        el.set(qn("w:type"), "dxa")
        mar.append(el)


def set_table_borders(table, color=BORDER, size=4) -> None:
    tblPr = table._tbl.tblPr
    existing = tblPr.find(qn("w:tblBorders"))
    if existing is not None:
        tblPr.remove(existing)
    borders = OxmlElement("w:tblBorders")
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        el = OxmlElement(f"w:{edge}")
        el.set(qn("w:val"), "single")
        el.set(qn("w:sz"), str(size))
        el.set(qn("w:space"), "0")
        el.set(qn("w:color"), color)
        borders.append(el)
    tblPr.insert_element_before(
        borders,
        "w:shd", "w:tblLayout", "w:tblCellMar", "w:tblLook", "w:tblCaption",
        "w:tblDescription",
    )


def set_table_width(table, pct=5000) -> None:
    tblPr = table._tbl.tblPr
    tblW = tblPr.find(qn("w:tblW"))
    if tblW is None:
        tblW = OxmlElement("w:tblW")
        tblPr.insert_element_before(
            tblW,
            "w:jc", "w:tblCellSpacing", "w:tblInd", "w:tblBorders", "w:shd",
            "w:tblLayout", "w:tblCellMar", "w:tblLook", "w:tblCaption",
            "w:tblDescription",
        )
    tblW.set(qn("w:type"), "pct")
    tblW.set(qn("w:w"), str(pct))


def set_repeat_header(row) -> None:
    trPr = row._tr.get_or_add_trPr()
    el = trPr.find(qn("w:tblHeader"))
    if el is None:
        el = OxmlElement("w:tblHeader")
        trPr.insert_element_before(
            el,
            "w:tblCellSpacing", "w:jc", "w:hidden", "w:ins", "w:del",
            "w:trPrChange",
        )
    el.set(qn("w:val"), "true")


def keep_row_together(row) -> None:
    trPr = row._tr.get_or_add_trPr()
    el = trPr.find(qn("w:cantSplit"))
    if el is None:
        el = OxmlElement("w:cantSplit")
        trPr.insert_element_before(
            el,
            "w:trHeight", "w:tblHeader", "w:tblCellSpacing", "w:jc", "w:hidden",
            "w:ins", "w:del", "w:trPrChange",
        )
    el.set(qn("w:val"), "true")


def add_field(paragraph, instr: str, placeholder: str = "1") -> None:
    run = paragraph.add_run()
    fld = OxmlElement("w:fldChar")
    fld.set(qn("w:fldCharType"), "begin")
    run._r.append(fld)
    run2 = paragraph.add_run()
    it = OxmlElement("w:instrText")
    it.set(qn("xml:space"), "preserve")
    it.text = instr
    run2._r.append(it)
    run3 = paragraph.add_run()
    sep = OxmlElement("w:fldChar")
    sep.set(qn("w:fldCharType"), "separate")
    run3._r.append(sep)
    paragraph.add_run(placeholder)
    run4 = paragraph.add_run()
    end = OxmlElement("w:fldChar")
    end.set(qn("w:fldCharType"), "end")
    run4._r.append(end)


def add_hyperlink(paragraph, url: str, text: str) -> None:
    part = paragraph.part
    r_id = part.relate_to(
        url,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
        is_external=True,
    )
    hyperlink = OxmlElement("w:hyperlink")
    hyperlink.set(qn("r:id"), r_id)
    new_run = OxmlElement("w:r")
    rPr = OxmlElement("w:rPr")
    color = OxmlElement("w:color")
    color.set(qn("w:val"), ACCENT)
    rPr.append(color)
    underline = OxmlElement("w:u")
    underline.set(qn("w:val"), "single")
    rPr.append(underline)
    new_run.append(rPr)
    t = OxmlElement("w:t")
    t.set(qn("xml:space"), "preserve")
    t.text = text
    new_run.append(t)
    hyperlink.append(new_run)
    paragraph._p.append(hyperlink)


def add_inline(paragraph, text: str, size: float = 10.5, color: str | None = None) -> None:
    tokens = re.split(r"(\*\*.*?\*\*|`[^`]*`|\[[^\]]+\]\([^)]+\))", text)
    for token in tokens:
        if not token:
            continue
        if token.startswith("**") and token.endswith("**") and len(token) > 4:
            run = paragraph.add_run(token[2:-2])
            set_run_font(run, BODY_FONT, BODY_FONT, size, bold=True, color=color)
        elif token.startswith("`") and token.endswith("`") and len(token) > 1:
            run = paragraph.add_run(token[1:-1])
            set_run_font(run, CODE_FONT, CODE_FONT, size - 1, color="C7254E")
            rPr = run._element.get_or_add_rPr()
            shd = OxmlElement("w:shd")
            shd.set(qn("w:val"), "clear")
            shd.set(qn("w:color"), "auto")
            shd.set(qn("w:fill"), "F7F7F7")
            rPr.append(shd)
        elif token.startswith("[") and "](" in token and token.endswith(")"):
            m = re.match(r"\[([^\]]+)\]\(([^)]+)\)", token)
            if m:
                add_hyperlink(paragraph, m.group(2), m.group(1))
            else:
                run = paragraph.add_run(token)
                set_run_font(run, BODY_FONT, BODY_FONT, size, color=color)
        else:
            run = paragraph.add_run(token)
            set_run_font(run, BODY_FONT, BODY_FONT, size, color=color)


def body_paragraph(doc, text: str, style: str | None = None, indent: bool = True,
                   size: float = 10.5, color: str | None = None,
                   space_after: float = 5) -> None:
    p = doc.add_paragraph(style=style) if style else doc.add_paragraph()
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after = Pt(space_after)
    p.paragraph_format.line_spacing = 1.45
    p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    if indent and len(text) >= 36:
        p.paragraph_format.first_line_indent = Cm(0.74)
    add_inline(p, text, size=size, color=color)


def list_paragraph(doc, text: str, marker: str, level: int = 0) -> None:
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.6 + 0.55 * level)
    p.paragraph_format.first_line_indent = Cm(-0.45)
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after = Pt(3)
    p.paragraph_format.line_spacing = 1.35
    p.alignment = WD_ALIGN_PARAGRAPH.LEFT
    run = p.add_run(marker + " ")
    set_run_font(run, BODY_FONT, BODY_FONT, 10.5, color=ACCENT if marker in ("•", "☐") else DARK)
    add_inline(p, text, size=10.5)


def code_block(doc, lines: list[str]) -> None:
    if not lines:
        return
    p0 = doc.add_paragraph()
    p0.paragraph_format.space_before = Pt(3)
    p0.paragraph_format.space_after = Pt(0)
    p0.paragraph_format.line_spacing = 1.0
    shade_paragraph(p0, LIGHT)
    run = p0.add_run(" ")
    set_run_font(run, CODE_FONT, CODE_FONT, 1)

    for i, line in enumerate(lines):
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.line_spacing = 1.0
        p.paragraph_format.left_indent = Cm(0.25)
        shade_paragraph(p, LIGHT)
        if i < len(lines) - 1 and len(lines) < 18:
            p.paragraph_format.keep_with_next = True
        run = p.add_run(line if line else " ")
        set_run_font(run, CODE_FONT, CODE_FONT, 8.5, color="333333")

    p1 = doc.add_paragraph()
    p1.paragraph_format.space_before = Pt(0)
    p1.paragraph_format.space_after = Pt(3)
    p1.paragraph_format.line_spacing = 1.0
    run = p1.add_run(" ")
    set_run_font(run, CODE_FONT, CODE_FONT, 1)


def add_table(doc, rows: list[list[str]]) -> None:
    if not rows:
        return
    header = rows[0]
    data = rows[1:]
    ncols = max(len(r) for r in rows)
    table = doc.add_table(rows=1, cols=ncols, style="Table Grid")
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = True
    set_table_width(table)
    hdr = table.rows[0]
    set_repeat_header(hdr)
    keep_row_together(hdr)
    for i in range(ncols):
        cell = hdr.cells[i]
        cell.text = ""
        shade_cell(cell, DARK)
        set_cell_margins(cell)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.LEFT
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.space_after = Pt(0)
        text = header[i] if i < len(header) else ""
        add_inline(p, text, size=8.5, color="FFFFFF")
        for run in p.runs:
            run.font.bold = True
            if not run.font.color.rgb:
                run.font.color.rgb = RGBColor.from_string("FFFFFF")
    for ridx, row in enumerate(data):
        tr = table.add_row()
        keep_row_together(tr)
        for i in range(ncols):
            cell = tr.cells[i]
            cell.text = ""
            set_cell_margins(cell)
            if ridx % 2 == 1:
                shade_cell(cell, BAND)
            p = cell.paragraphs[0]
            p.paragraph_format.space_before = Pt(0)
            p.paragraph_format.space_after = Pt(0)
            p.paragraph_format.line_spacing = 1.15
            text = row[i] if i < len(row) else ""
            add_inline(p, text, size=8.5)
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after = Pt(2)
    p.paragraph_format.line_spacing = 1.0
    run = p.add_run(" ")
    set_run_font(run, BODY_FONT, BODY_FONT, 2)


def is_separator(cells: list[str]) -> bool:
    return all(re.fullmatch(r":?-{3,}:?", c.strip()) for c in cells if c.strip() != "")


def parse_table_row(line: str) -> list[str]:
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|"):
        s = s[:-1]
    return [c.strip() for c in s.split("|")]


def extract_cover(lines: list[str]) -> tuple[str, str, dict[str, str], int]:
    title = ""
    subtitle = ""
    meta: dict[str, str] = {}
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("# ") and not title:
            title = line[2:].strip()
            i += 1
            continue
        if title and not subtitle and line.startswith("**") and line.endswith("**"):
            subtitle = line.strip("*").strip()
            i += 1
            continue
        if line.startswith(("Status:", "Domain:", "Canonical:", "Related:", "Last updated:", "Author:")):
            key, _, value = line.partition(":")
            meta[key.strip()] = value.strip().strip("`")
            i += 1
            continue
        if line.startswith(("版本：", "日期：", "状态：", "适用：")):
            key, _, value = line.partition("：")
            meta[key.strip()] = value.strip()
            i += 1
            continue
        if line == "---":
            i += 1
            break
        if line.startswith("## "):
            break
        i += 1
    return title, subtitle, meta, i


def build() -> None:
    text = SRC.read_text(encoding="utf-8")
    lines = text.splitlines()
    title, subtitle, meta, start = extract_cover(lines)

    doc = Document()
    section = doc.sections[0]
    section.page_width = Cm(21.0)
    section.page_height = Cm(29.7)
    section.top_margin = Cm(2.2)
    section.bottom_margin = Cm(2.0)
    section.left_margin = Cm(2.2)
    section.right_margin = Cm(2.2)
    section.different_first_page_header_footer = True

    # Normal style
    normal = doc.styles["Normal"]
    normal.font.name = BODY_FONT
    normal.font.size = Pt(10.5)
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), BODY_FONT)

    # Heading styles
    for name, size, color, before, after in (
        ("Heading 1", 18, DARK, 6, 10),
        ("Heading 2", 13.5, ACCENT, 12, 6),
        ("Heading 3", 11.5, DARK, 8, 4),
    ):
        st = doc.styles[name]
        st.font.name = HEAD_FONT
        st.font.size = Pt(size)
        st.font.bold = True
        st.font.color.rgb = RGBColor.from_string(color)
        st._element.rPr.rFonts.set(qn("w:eastAsia"), HEAD_FONT)
        st.paragraph_format.space_before = Pt(before)
        st.paragraph_format.space_after = Pt(after)
        st.paragraph_format.keep_with_next = True

    # Header
    header = section.header
    header.is_linked_to_previous = False
    hp = header.paragraphs[0]
    hp.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    hr = hp.add_run("大型项目 AI 协作管理体系（通用规范）")
    set_run_font(hr, HEAD_FONT, HEAD_FONT, 8, color=GRAY)
    add_bottom_border(hp, "D9E2F3", 6)

    # Footer page number
    footer = section.footer
    footer.is_linked_to_previous = False
    fp = footer.paragraphs[0]
    fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = fp.add_run("第 ")
    set_run_font(r, BODY_FONT, BODY_FONT, 8.5, color=GRAY)
    add_field(fp, "PAGE", "1")
    r = fp.add_run(" 页")
    set_run_font(r, BODY_FONT, BODY_FONT, 8.5, color=GRAY)

    # Cover
    for _ in range(2):
        doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.LEFT
    r = p.add_run(title)
    set_run_font(r, HEAD_FONT, HEAD_FONT, 26, bold=True, color=DARK)

    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(10)
    p.paragraph_format.space_after = Pt(4)
    r = p.add_run(subtitle)
    set_run_font(r, HEAD_FONT, HEAD_FONT, 13, color=ACCENT)

    # accent line
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(6)
    p.paragraph_format.space_after = Pt(18)
    add_bottom_border(p, ACCENT, 18)

    author_full = meta.get("Author", "董力 <mr.aphranda@gmail.com>")
    author_match = re.match(r"(.+?)\s*<([^>]+)>", author_full)
    author_name = author_match.group(1).strip() if author_match else author_full
    author_email = author_match.group(2).strip() if author_match else ""

    info = [
        ("作者 / 签署", f"{author_name}　签名：____________________"),
        ("邮箱", author_email),
        ("版本", meta.get("版本", "1.0")),
        ("日期", meta.get("日期", "2026-11-06")),
        ("状态", meta.get("状态", "交付版")),
        ("适用", meta.get("适用", "跨专业大型工程项目；可按团队规模裁剪")),
        ("维护", "项目治理负责人；季度复审"),
    ]
    tbl = doc.add_table(rows=0, cols=2)
    tbl.autofit = False
    for label, value in info:
        row = tbl.add_row()
        c0, c1 = row.cells
        c0.width = Cm(2.6)
        c1.width = Cm(11.8)
        p0 = c0.paragraphs[0]
        p0.paragraph_format.space_before = Pt(3)
        p0.paragraph_format.space_after = Pt(3)
        r0 = p0.add_run(label)
        set_run_font(r0, HEAD_FONT, HEAD_FONT, 9.5, color=GRAY)
        p1 = c1.paragraphs[0]
        p1.paragraph_format.space_before = Pt(3)
        p1.paragraph_format.space_after = Pt(3)
        r1 = p1.add_run(value)
        set_run_font(r1, BODY_FONT, BODY_FONT, 9.5, color="262626")

    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(26)
    r = p.add_run("本规范抽象自 HDRT100 的文档自回归体系、工具治理、Skill 体系与 WorkflowSpec，供跨项目 AI 工作流建设参考。")
    set_run_font(r, BODY_FONT, BODY_FONT, 8.5, color=GRAY)
    p.alignment = WD_ALIGN_PARAGRAPH.LEFT

    doc.add_page_break()

    # TOC
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after = Pt(12)
    r = p.add_run("目  录")
    set_run_font(r, HEAD_FONT, HEAD_FONT, 18, bold=True, color=DARK)

    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.space_after = Pt(4)
    add_field(p, 'TOC \\o "1-1" \\h \\z \\u', "右键更新目录")
    for run in p.runs:
        set_run_font(run, BODY_FONT, BODY_FONT, 9, color=GRAY)

    p = doc.add_paragraph()
    r = p.add_run("提示：在 Word 中右键目录 →“更新域”可刷新页码；本文件已设置打开时自动更新。")
    set_run_font(r, BODY_FONT, BODY_FONT, 8, color=GRAY)

    doc.add_page_break()

    # Body
    i = start
    first_h1 = True
    in_code = False
    code_lines: list[str] = []
    code_fence = ""
    table_rows: list[list[str]] = []

    def flush_table() -> None:
        nonlocal table_rows
        if table_rows:
            # drop separator row
            cleaned = [r for r in table_rows if not is_separator(r)]
            if cleaned:
                add_table(doc, cleaned)
            table_rows = []

    while i < len(lines):
        raw = lines[i]
        line = raw.rstrip()
        stripped = line.strip()

        if in_code:
            if stripped.startswith(code_fence) and stripped.strip("`") == "":
                code_block(doc, code_lines)
                code_lines = []
                in_code = False
                code_fence = ""
            else:
                code_lines.append(line)
            i += 1
            continue

        if stripped.startswith("```"):
            flush_table()
            fence = stripped[: len(stripped) - len(stripped.lstrip("`"))]
            code_fence = fence
            in_code = True
            code_lines = []
            i += 1
            continue

        if not stripped:
            flush_table()
            i += 1
            continue

        if stripped.startswith("|"):
            cells = parse_table_row(stripped)
            table_rows.append(cells)
            i += 1
            continue

        flush_table()

        if stripped.startswith("#### "):
            p = doc.add_paragraph(style="Heading 3")
            add_inline(p, stripped[5:], size=11.5, color=DARK)
        elif stripped.startswith("### "):
            p = doc.add_paragraph(style="Heading 2")
            add_inline(p, stripped[4:], size=13.5, color=ACCENT)
        elif stripped.startswith("## "):
            if not first_h1:
                doc.add_page_break()
            first_h1 = False
            p = doc.add_paragraph(style="Heading 1")
            add_inline(p, stripped[3:], size=18, color=DARK)
        elif stripped.startswith("# "):
            p = doc.add_paragraph(style="Heading 1")
            add_inline(p, stripped[2:], size=18, color=DARK)
        elif stripped == "---":
            pass
        elif stripped.startswith("> "):
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(0.6)
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after = Pt(4)
            p.paragraph_format.line_spacing = 1.3
            add_inline(p, stripped[2:], size=9.5, color=GRAY)
        elif re.match(r"^[-*] \[[ xX]\] ", stripped):
            list_paragraph(doc, stripped[6:], "☐", level=0)
        elif re.match(r"^[-*] ", stripped):
            level = max(0, (len(raw) - len(raw.lstrip())) // 2)
            list_paragraph(doc, stripped[2:], "•", level=min(level, 2))
        elif re.match(r"^\d+\.\s+", stripped):
            m = re.match(r"^(\d+)\.\s+(.*)$", stripped)
            level = max(0, (len(raw) - len(raw.lstrip())) // 3)
            list_paragraph(doc, m.group(2), f"{m.group(1)}.", level=min(level, 2))
        elif stripped.startswith("*") and stripped.endswith("*") and len(stripped) > 2:
            p = doc.add_paragraph()
            p.paragraph_format.space_before = Pt(8)
            p.paragraph_format.space_after = Pt(4)
            add_inline(p, stripped.strip("*"), size=8.5, color=GRAY)
        else:
            body_paragraph(doc, stripped)

        i += 1

    if in_code and code_lines:
        code_block(doc, code_lines)
    flush_table()

    # Document properties
    doc.core_properties.title = title
    doc.core_properties.subject = subtitle
    doc.core_properties.author = "董力"
    doc.core_properties.last_modified_by = "董力"
    doc.core_properties.keywords = "项目管理, 文档治理, 契约, 工具治理, Skill, Workflow, 证据, AI 工作流"
    doc.core_properties.comments = "通用化大型项目 AI 协作管理体系"

    # Update fields on open
    settings = doc.settings.element
    update = OxmlElement("w:updateFields")
    update.set(qn("w:val"), "true")
    settings.insert_element_before(
        update,
        "w:hdrShapeDefaults", "w:footnotePr", "w:endnotePr", "w:compat",
        "w:docVars", "w:rsids", "w:mathPr", "w:themeFontLang",
        "w:clrSchemeMapping", "w:doNotIncludeSubdocsInStats",
        "w:doNotAutoCompressPictures", "w:forceUpgrade", "w:captions",
        "w:readModeInkLockDown", "w:smartTagType", "w:schemaLibrary",
        "w:shapeDefaults", "w:doNotEmbedSmartTags", "w:decimalSymbol",
        "w:listSeparator",
    )

    OUT.parent.mkdir(parents=True, exist_ok=True)
    doc.save(OUT)
    print(f"WROTE {OUT}")
    print(f"paragraphs={len(doc.paragraphs)} tables={len(doc.tables)}")


if __name__ == "__main__":
    build()
