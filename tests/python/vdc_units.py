"""Body views of the split VDC units for the host harnesses.

The fragments were merged into synthetic translation units; a harness that
used to compile a fragment now compiles the body of the unit that owns it.
Nothing here reads the archived fragment copies: the archive is an audit
artefact, not a fact source.
"""
from pathlib import Path

import c_text

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
