"""Body views of the split VDC units for the host harnesses."""
from pathlib import Path

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


def body_for(fragment: str) -> str:
    """Body of the merged unit that owns the given fragment."""
    return body(FRAGMENT_TO_UNIT[fragment])
