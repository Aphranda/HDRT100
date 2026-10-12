"""The appended tail of `TRIGger:REServation:T2?` is a contract for its readers.

The T2 bench tools parse this page positionally (`t2_workflow.py`, `t2_bench_probe.py`
and the acceptance grader all index into the comma-separated reply). Appending new
fields at the end is therefore safe, while reordering or inserting them silently
breaks every reader - exactly the kind of change that otherwise only shows up as a
nonsense bench reading.

This test pins the order of the fields appended for the rolling prediction window
(2026-10-12) against the runtime command source. It is intentionally textual: the
page is assembled by a macro-expanded SCPI handler, so there is nothing to link and
nothing to run - the guard is about the reply layout, not about behaviour.
"""
import re

from test_trigger_grid_preview import ROOT

PAGE = ROOT / "middleware/scpi_port/src/scpi_trigger_t2_runtime_commands.inc"

# The fields appended after the event log, in the order readers will see them.
EXPECTED_TAIL = (
    "window_entered_count",
    "window_announced_count",
    "window_overwrote_unannounced_count",
    "window_late_entry_count",
    "window_recomputed_count",
    "window_rate_stale_count",
    "window_step_mdeg",
    "scan_geometry.positions",
    "scan_geometry.step_pulses",
)


def test_t2_page_appended_window_fields_keep_their_order():
    source = PAGE.read_text(encoding="utf-8")
    marker = "Appended: the rolling prediction window's own health facts"
    assert marker in source, "the window-tail comment disappeared from the T2 page"

    tail = source[source.index(marker):]
    found = re.findall(r"SCPI_ResultUInt32\(context,\s*s\.predictor\.([A-Za-z_.]+)\)", tail)
    assert tuple(found) == EXPECTED_TAIL, (
        "the appended window fields moved or changed; positional T2 readers "
        f"(t2_workflow.py / t2_bench_probe.py / the acceptance grader) expect "
        f"{EXPECTED_TAIL} but the page now has {tuple(found)}")

    # They must stay AFTER the event log: inserting them earlier would shift every
    # index a reader already uses.
    events = source.index("s.event_count")
    assert source.index(marker) > events, "the appended window fields must stay last"
