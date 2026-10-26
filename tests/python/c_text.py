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


def typedefs(text: str) -> dict[str, str]:
    """Every ``typedef struct/union/enum {...} name;`` in ``text``, in file order."""
    found: dict[str, str] = {}
    for match in re.finditer(r"(?m)^typedef\s+(?:struct|union|enum)\b[^{;]*\{", text):
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
        name = re.search(r"([A-Za-z_]\w*)", tail)
        if name:
            found[name.group(1)] = text[match.start():end + 1]
    return found


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


def split_top_level(text: str) -> list[tuple[str | None, int, int]]:
    """Split C text into top-level chunks.

    Returns ``(name, start, end)`` where ``name`` is the function name for a
    definition chunk and ``None`` for any other top-level text (declarations,
    ``#define`` blocks, comments).  A definition chunk starts at its signature
    line and ends at the matching closing brace.
    """
    chunks: list[tuple[str | None, int, int]] = []
    index = 0
    chunk_start = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char == ";":
            index += 1
            continue
        if char == "{":
            # Find the matching brace for this top-level block.
            depth = 1
            cursor = index + 1
            while cursor < length and depth:
                if text[cursor] == "{":
                    depth += 1
                elif text[cursor] == "}":
                    depth -= 1
                cursor += 1
            header = text[chunk_start:index]
            name = _definition_name(header)
            chunks.append((name, chunk_start, cursor))
            chunk_start = cursor
            index = cursor
            continue
        index += 1
    if chunk_start < length:
        chunks.append((None, chunk_start, length))
    return chunks


def _definition_name(header: str) -> str | None:
    """Function name for a chunk header, or None when it is not a definition."""
    cleaned = re.sub(r"/\*.*?\*/", " ", header, flags=re.S)
    cleaned = re.sub(r"(?m)//.*$", " ", cleaned)
    cleaned = re.sub(r"__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)", " ", cleaned)
    cleaned = "\n".join(line for line in cleaned.splitlines()
                        if not line.lstrip().startswith("#"))
    cleaned = cleaned.strip()
    if not cleaned:
        return None
    # Placement macro wrapping the real name: MACRO(return type name(...))
    wrapped = re.search(r"[A-Z_][A-Z0-9_]*\s*\(\s*([A-Za-z_]\w*)\s*\)\s*\(", cleaned)
    if wrapped:
        return wrapped.group(1)
    match = re.search(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$", cleaned)
    if match:
        return match.group(1)
    match = re.search(r"([A-Za-z_]\w*)\s*\(", cleaned)
    return match.group(1) if match else None


def keep_named_chunks(text: str, names: set[str]) -> str:
    """Keep every non-function chunk plus the function chunks named in ``names``.

    The chunker can mis-attribute a signature that follows a declaration block;
    any requested function missing from the result is appended by exact
    extraction so a harness never silently loses a definition.
    """
    kept: list[str] = []
    for name, start, end in split_top_level(text):
        if name is None or name in names:
            kept.append(text[start:end])
    result = "".join(kept)
    for name in sorted(names):
        try:
            function_body(result, name)
            continue
        except KeyError:
            pass
        try:
            result += "\n" + function_body(text, name) + "\n"
        except KeyError:
            continue
    return result


def prune_unused_statics(text: str) -> str:
    """Drop single-line file-scope ``static`` declarations nothing refers to."""
    line_static = re.compile(r"(?m)^static\s+([^;{}\n]*);[ \t]*\n?")
    # Everything except the declarations themselves counts as a use.
    used = line_static.sub("", text)
    out: list[str] = []
    last = 0
    for match in line_static.finditer(text):
        names = [n for n in re.findall(
            r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)?(?=[,;]|$)", match.group(1)) if n != "static"]
        if names and not any(re.search(rf"\b{re.escape(n)}\b", used) for n in names):
            out.append(text[last:match.start()])
            last = match.end()
    out.append(text[last:])
    return "".join(out)


def strip_statics(text: str) -> str:
    """Remove every single-line file-scope ``static`` declaration."""
    return re.sub(r"(?m)^static\s+[^;{}\n]*;[ \t]*\n?", "", text)


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
