"""Views of the split SCPI command units for the host harnesses.

A parser harness used to textually ``#include`` a command-family ``.inc``
fragment.  The fragments are archived; each family now owns a real ``.c``
translation unit plus an interface header, so a harness compiles the unit and
includes its header instead of inlining the audit copy.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "middleware/scpi_port/src"
INC = ROOT / "middleware/scpi_port/inc"

# Archived fragment stem -> live translation unit.  The stems match today, but
# the map is explicit so a future rename cannot silently re-point a harness.
FRAGMENT_TO_UNIT = {
    "scpi_action_probe": "scpi_action_probe",
    "scpi_trigger_grid_commands": "scpi_trigger_grid_commands",
    "scpi_trigger_cursor_commands": "scpi_trigger_cursor_commands",
    "scpi_trigger_deployment_commands": "scpi_trigger_deployment_commands",
    "scpi_trigger_template_commands": "scpi_trigger_template_commands",
    "scpi_trigger_t2_runtime_commands": "scpi_trigger_t2_runtime_commands",
    "scpi_trigger_seat_commands": "scpi_trigger_seat_commands",
    "scpi_vdc_output_slices": "scpi_vdc_output_slices",
    "scpi_trigger_counter_commands": "scpi_trigger_counter_commands",
    "scpi_trigger_gateway_commands": "scpi_trigger_gateway_commands",
    "scpi_trigger_switch_commands": "scpi_trigger_switch_commands",
    "scpi_trigger_action_commands": "scpi_trigger_action_commands",
    "scpi_trigger_action_execute": "scpi_trigger_action_execute",
}


def unit_of(fragment: str) -> str:
    """Name of the live SCPI translation unit that owns ``fragment``."""
    try:
        return FRAGMENT_TO_UNIT[fragment]
    except KeyError as error:
        raise KeyError(f"unknown SCPI fragment: {fragment}") from error


def units() -> list[str]:
    return sorted(set(FRAGMENT_TO_UNIT.values()))


def source_path(fragment: str) -> Path:
    return SRC / f"{unit_of(fragment)}.c"


def header_path(fragment: str) -> Path:
    return INC / f"{unit_of(fragment)}.h"


def body(fragment: str) -> str:
    """Unit text without its leading include prologue."""
    text = source_path(fragment).read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    while lines and (not lines[0].strip() or lines[0].lstrip().startswith("#include")):
        lines.pop(0)
    return "".join(lines)


def callbacks(fragment: str) -> list[str]:
    """``scpi_cmd_*`` callbacks the unit defines."""
    import re

    text = source_path(fragment).read_text(encoding="utf-8")
    return sorted(set(re.findall(r"(?m)^scpi_result_t\s+(scpi_cmd_\w+)\s*\(", text)))
