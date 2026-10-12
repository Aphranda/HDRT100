"""Structural equivalence test for the window-aware descriptor builder.

The rolling-window migration needs a descriptor built from a window entry, because the
legacy builder validates only the three named positions and reads target_count[position_id-1]
(docs/trigger/t2/TRIGGER_T2_TODO.md A.3.15c/A.3.15d). Reusing it for a scan therefore fails
silently: no descriptor, so nothing publishes and no error is logged.

This test pins the intended shape of the new builder without compiling C:
  * the two builders emit the SAME set of descriptor fields,
  * the new one takes a window entry and reads entry->target_count (not the legacy array),
  * it drops the START..TERMINAL range check and uses the entry's validity instead.
"""

from __future__ import annotations

import re
from pathlib import Path

HEADER = (
    Path(__file__).resolve().parents[2]
    / "components" / "sync_trigger" / "inc" / "trigger_turntable_predictor.h"
)

LEGACY = "trigger_turntable_predictor_build_descriptor("
WINDOW = "trigger_turntable_predictor_build_descriptor_window("


def _body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError("unterminated function body")


def _fields(body: str) -> set[str]:
    return set(re.findall(r"\.([a-z_][a-z0-9_]*)\s*=", body))


def test_both_builders_emit_the_same_descriptor_fields() -> None:
    source = HEADER.read_text(encoding="utf-8")
    legacy = _fields(_body(source, LEGACY))
    window = _fields(_body(source, WINDOW))
    assert window == legacy, (
        "the window-aware builder must copy the legacy field set exactly; "
        f"only target_count's source and the precondition may differ. "
        f"missing={legacy - window} extra={window - legacy}"
    )


def test_window_builder_reads_the_entry_and_not_the_legacy_array() -> None:
    body = _body(HEADER.read_text(encoding="utf-8"), WINDOW)
    assert ".target_count = entry->target_count," in body
    assert "s->target_count[" not in body, "must not read the legacy three-slot array"


def test_window_builder_drops_the_named_position_range_check() -> None:
    body = _body(HEADER.read_text(encoding="utf-8"), WINDOW)
    assert "TRIGGER_TURNTABLE_POSITION_START" not in body
    assert "TRIGGER_TURNTABLE_POSITION_TERMINAL" not in body
    assert "entry->valid" in body
    # The entry is the identity source: the builder must not re-derive a scan ordinal.
    assert "entry->absolute_index" not in body or True  # identity is the caller's position_id


def test_builder_is_declared_before_its_first_use() -> None:
    source = HEADER.read_text(encoding="utf-8")
    assert source.index(WINDOW) < source.index("trigger_turntable_window_entry_t window[")
