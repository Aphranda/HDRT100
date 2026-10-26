"""Body views of the split VDC units for the host harnesses.

The fragments were merged into synthetic translation units; a harness that
used to compile a fragment now compiles the body of the unit that owns it.
Nothing here reads the archived fragment copies: the archive is an audit
artefact, not a fact source.
"""
from pathlib import Path
import re

import c_text
from vdc_fragment_functions import FRAGMENT_FUNCTIONS, FRAGMENT_INCLUDES

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/vdc_dpll_manager/src"


def body(name: str) -> str:
    """Unit code without its include prologue."""
    text = (SRC / f"{name}.c").read_text(encoding="utf-8")
    marker = '#include "vdc_dpll_manager_internal.h"'
    idx = text.rfind(marker)
    return text[idx + len(marker):] if idx >= 0 else text


# The fragments were merged into synthetic translation units; a harness that
# used to compile a fragment now compiles the body of the unit that owns it.
FRAGMENT_TO_UNIT = {
    "vdc_boundary_capture": "vdc_boundary",
    "vdc_boundary_control": "vdc_boundary",
    "vdc_dpll_feedback_match": "vdc_dpll_feedback",
    "vdc_model_feedback": "vdc_dpll_feedback",
    "vdc_fixed_output": "vdc_output",
    "vdc_run_output": "vdc_output",
    "vdc_reference_discipline": "vdc_output",
    "vdc_priority_match": "vdc_priority_follow",
    "vdc_priority_follow": "vdc_priority_follow",
    "vdc_priority_phase": "vdc_priority_follow",
    "vdc_priority_trace": "vdc_priority_trace",
    "vdc_priority_trace_summary": "vdc_priority_trace",
    "vdc_priority_guard": "vdc_priority_trace",
    "vdc_priority_tx": "vdc_priority_tx",
    "vdc_priority_tx_gap": "vdc_priority_tx",
    "vdc_priority_host_control": "vdc_priority_host_control",
    "vdc_priority_ingress": "vdc_priority_ingress",
    "vdc_priority_rx": "vdc_priority_rx",
    "vdc_priority_follow_config": "vdc_priority_follow_config",
    "vdc_output_delay_config": "vdc_output_delay_config",
    "vdc_output_timing_config": "vdc_output_timing_config",
    "vdc_reference_config": "vdc_reference_config",
}


def unit_of(fragment: str) -> str:
    """Name of the live unit that owns ``fragment``."""
    try:
        return FRAGMENT_TO_UNIT[fragment]
    except KeyError as error:
        raise KeyError(f"unknown VDC fragment: {fragment}") from error


def units() -> list[str]:
    """Every live unit referenced by the fragment map."""
    return sorted(set(FRAGMENT_TO_UNIT.values()))


def body_for(fragment: str) -> str:
    """Body of the merged unit that owns the given fragment."""
    return body(unit_of(fragment))


def function_body(unit: str, name: str) -> str:
    """Definition of ``name`` taken from the live unit ``unit``."""
    text = (SRC / f"{unit}.c").read_text(encoding="utf-8")
    return c_text.function_body(text, name)


def function_body_for(fragment: str, name: str) -> str:
    """Definition of ``name`` taken from the unit that owns ``fragment``."""
    return function_body(unit_of(fragment), name)


# Fragments that used to define their own file-scope types.  The split hoisted
# those types into the unit's private header, so a harness that inlines the
# unit body must carry the declarations the fragment used to own.
FRAGMENT_DECLS = {
    "vdc_priority_tx": ("vdc_priority_tx_work_t",),
    "vdc_priority_match": ("vdc_priority_match_work_t",),
    "vdc_boundary_capture": ("vdc_boundary_peer_t", "vdc_boundary_meta_t",
                             "vdc_boundary_request_t"),
    "vdc_dpll_feedback_match": ("vdc_feedback_match_source_t",
                                "vdc_feedback_match_binding_t",
                                "vdc_local_follow_cursor_t"),
    "vdc_model_feedback": ("vdc_feedback_match_source_t",
                           "vdc_feedback_match_binding_t",
                           "vdc_local_follow_cursor_t"),
}
FRAGMENT_DECL_HEADER = "vdc_priority_shared.h"

# Fragments that call inline helpers the split hoisted into the private header.
FRAGMENT_HELPERS = {
    "vdc_dpll_feedback_match": ("match_inc",),
    "vdc_priority_match": ("priority_match_inc",),
    "vdc_boundary_capture": ("match_inc",),
    "vdc_boundary_control": ("match_inc",),
}

# Fragments that used to default a hook before the parent supplied one.
FRAGMENT_PROLOGUE = {
    "vdc_priority_tx": (
        "#ifndef VDC_PRIORITY_TRACE_ORIGIN_HOOK\n"
        "#define VDC_PRIORITY_TRACE_ORIGIN_HOOK(evidence) ((void)(evidence))\n"
        "#endif\n"
    ),
    "vdc_model_feedback": (
        "#ifndef VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK\n"
        "#define VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK(model) ((void)(model))\n"
        "#endif\n"
    ),
}


def _declarations(header: str, names: tuple[str, ...]) -> list[str]:
    """Typedefs needed by ``names``, with dependencies, in header order."""
    table = c_text.typedefs(header)
    needed: set[str] = set()
    queue = [name for name in names if name in table]
    missing = [name for name in names if name not in table]
    if missing:
        raise KeyError(f"missing typedefs in {FRAGMENT_DECL_HEADER}: {missing}")
    while queue:
        name = queue.pop()
        if name in needed:
            continue
        needed.add(name)
        for ident in re.findall(r"[A-Za-z_]\w*", table[name]):
            if ident in table and ident not in needed:
                queue.append(ident)
    return [text for name, text in table.items() if name in needed]


def _fragment_closure(fragment: str, seen: set[str] | None = None) -> set[str]:
    """Function names a fragment contributes, including its nested fragments."""
    seen = set() if seen is None else seen
    if fragment in seen:
        return set()
    seen.add(fragment)
    names = set(FRAGMENT_FUNCTIONS.get(fragment, ()))
    for included in FRAGMENT_INCLUDES.get(fragment, ()):
        names |= _fragment_closure(included, seen)
    return names


def fragment_slice(fragment: str) -> str:
    """Code the archived fragment used to contribute, taken from the live unit.

    The unit body merges sibling fragments and moves fragment-local types into
    the private header.  The slice removes the sibling fragments' functions (so
    a harness stub cannot collide with them) and restores the declarations the
    fragment used to carry.  Remaining sibling statics are harmless and are
    suppressed with ``-Wno-unused-*`` by the harness.
    """
    unit = unit_of(fragment)
    text = body(unit)
    names = _fragment_closure(fragment)
    if not names:
        raise KeyError(f"no function map for VDC fragment: {fragment}")
    text = c_text.keep_named_chunks(text, names)
    text = c_text.prune_unused_statics(text)

    parts: list[str] = []
    if FRAGMENT_HELPERS.get(fragment):
        header = (SRC / FRAGMENT_DECL_HEADER).read_text(encoding="utf-8")
        parts.extend(c_text.function_body(header, name) for name in FRAGMENT_HELPERS[fragment])
    if FRAGMENT_DECLS.get(fragment):
        header = (SRC / FRAGMENT_DECL_HEADER).read_text(encoding="utf-8")
        parts.extend(_declarations(header, FRAGMENT_DECLS[fragment]))
    if FRAGMENT_PROLOGUE.get(fragment):
        parts.append(FRAGMENT_PROLOGUE[fragment])
    parts.append(text)
    return "\n".join(part for part in parts if part)
