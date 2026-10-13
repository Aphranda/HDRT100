"""`T2:CONFigure` is a positional command: its parameter list and every caller must
move together.

Adding the scan STEP to this command (docs/trigger/t2/TRIGGER_T2_TODO.md A.4.1) is a
coordinated change: the handler in `scpi_trigger_t2_runtime_commands.inc` and all four
bench call sites must be updated in the same commit, or the tools start sending a
payload the firmware misreads (a silently truncated scan, not an error).

Nothing else guards that coordination today, so this test pins:
  1. the ordered parameter list the handler parses, and
  2. the exact set of call sites that send the command.

Both are deliberately textual. Adding STEP must update this file - which is the point:
it forces the implementer to look at the callers instead of discovering them on the bench.
"""
import re

from test_trigger_grid_preview import ROOT

COMMANDS = ROOT / "middleware/scpi_port/src/scpi_trigger_t2_runtime_commands.inc"

# Parsed in this order by scpi_cmd_trigger_t2_configure().
EXPECTED_PARAMETERS = (
    "run_id",
    "source_slot",
    "target_mask",
    "origin",
    "pre_roll",
    "post_roll",
    "threshold",
    "start_angle",
    "zero_angle",
    "terminal_angle",
    "step_mdeg",
    "pulses_per_degree",
)

# Every place that sends TRIGger:REServation:T2:CONFigure, with the line it lives on.
EXPECTED_CALL_SITES = {
    "tools/sync_trigger/t2_bench_probe.py": (242,),
    "tools/sync_trigger/t2_workflow.py": (214, 520, 972, 1737,),
}


def _configure_handler(source):
    start = source.index("scpi_result_t scpi_cmd_trigger_t2_configure(")
    end = source.index("trigger_grid_preview_snapshot_t grid;", start)
    return source[start:end]


def test_t2_configure_parameter_list_is_pinned():
    body = _configure_handler(COMMANDS.read_text(encoding="utf-8"))
    # ONE pass over both reads, in source order: the command is positional, so the order
    # the handler reads arguments in IS the order callers must send them. Collecting the
    # two kinds separately (as this test first did) hid an interleaving mistake - ppd was
    # parsed before the angles while callers appended it last, and the bench caught it as
    # a rejected configure instead of a red test.
    parsed = re.findall(
        r"scpi_trigger_grid_uint\(context,[^,]+,\s*&(\w+)\)|SCPI_ParamInt32\(context,\s*&(\w+)",
        body)
    parsed = [name for pair in parsed for name in pair if name]
    assert tuple(parsed) == EXPECTED_PARAMETERS, (
        "the T2 configure parameter list changed; every caller sends positional "
        f"arguments, so all of them must be updated with it. Expected "
        f"{EXPECTED_PARAMETERS}, found {tuple(parsed)}")


def test_t2_configure_call_sites_are_pinned():
    for relative, lines in EXPECTED_CALL_SITES.items():
        text = (ROOT / relative).read_text(encoding="utf-8").splitlines()
        found = tuple(
            number for number, line in enumerate(text, 1)
            if "T2:CONFigure" in line and "TRIGger:REServation" in line)
        assert found == lines, (
            f"{relative} sends T2:CONFigure from {found}, not {lines}: a new or moved "
            "call site must be added here so it cannot miss a parameter change")
