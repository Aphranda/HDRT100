"""Locate live C definitions inside split translation units.

Host harnesses used to inline ``.inc`` fragments because a fragment had no
identity the compiler could resolve.  The fragments are archived now; the same
code lives in real ``.c`` units, so a harness must take its text from the unit
that *ships*, by symbol name, instead of from the audit copy.

Every helper here refuses to read a path outside the live source directory: the
archive is an audit artefact, never a fact source.
"""
from __future__ import annotations

import re
from pathlib import Path

_CONTROL = re.compile(r"\b(if|for|while|switch|return|assert|sizeof|case)\b")
_ATTRIBUTE = re.compile(r"__attribute__\s*\(\([^\n]*?\)\)")


def _close_paren(text: str, index: int) -> int:
    """Return the index just past the ``)`` matching ``text[index] == '('``."""
    depth = 0
    for cursor in range(index, len(text)):
        char = text[cursor]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if not depth:
                return cursor + 1
    raise ValueError("unbalanced parentheses")


def _is_definition_prefix(prefix: str) -> bool:
    """True when the text before a name is a C return type, not a call site."""
    prefix = prefix.strip()
    if not prefix or _CONTROL.search(prefix):
        return False
    stripped = _ATTRIBUTE.sub(" ", prefix)
    if "(" in stripped or ")" in stripped:
        return False
    return bool(re.fullmatch(r"[A-Za-z_][\w\s\*]*", stripped))


def function_body(text: str, name: str) -> str:
    """Return the complete definition (signature + body) of ``name``.

    Raises ``KeyError`` when the text only declares or calls the symbol.
    """
    pattern = re.compile(rf"(?m)^([A-Za-z_][^\n]*?)\b{re.escape(name)}\s*\(")
    for match in pattern.finditer(text):
        if not _is_definition_prefix(match.group(1)):
            continue
        try:
            after = _close_paren(text, match.end() - 1)
        except ValueError:
            continue
        # Skip attributes/whitespace between the parameter list and the body.
        cursor = after
        while cursor < len(text) and text[cursor] not in "{;":
            cursor += 1
        if cursor >= len(text) or text[cursor] != "{":
            continue
        depth = 1
        cursor += 1
        while depth:
            char = text[cursor]
            depth += (char == "{") - (char == "}")
            cursor += 1
        return text[match.start(1):cursor].rstrip()
    raise KeyError(name)


def definition_units(directory: Path, name: str) -> list[Path]:
    """Every ``*.c`` in ``directory`` that defines ``name``."""
    hits: list[Path] = []
    for path in sorted(Path(directory).glob("*.c")):
        try:
            function_body(path.read_text(encoding="utf-8"), name)
        except KeyError:
            continue
        hits.append(path)
    return hits


def typedef_body(text: str, name: str) -> str:
    """Return the ``typedef struct/enum/union ... name;`` declaration."""
    pattern = re.compile(rf"(?m)^typedef\s+(?:struct|union|enum)\b[^{{;]*\{{")
    for match in pattern.finditer(text):
        depth = 0
        cursor = match.end() - 1
        while cursor < len(text):
            char = text[cursor]
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if not depth:
                    break
            cursor += 1
        else:
            continue
        end = text.find(";", cursor)
        if end < 0:
            continue
        tail = text[cursor + 1:end]
        if re.search(rf"\b{re.escape(name)}\b", tail):
            return text[match.start():end + 1]
    raise KeyError(name)


def strip_prologue(text: str) -> str:
    """Drop the leading blank/``#include`` lines of a unit."""
    lines = text.splitlines(keepends=True)
    while lines:
        head = lines[0].strip()
        if not head or head.startswith("#include"):
            lines.pop(0)
            continue
        break
    return "".join(lines)
