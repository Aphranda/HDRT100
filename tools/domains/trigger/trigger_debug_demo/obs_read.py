"""ControlPlane-backed REFMEM OBS reads for the demo pages (M5).

The demo's readback page must not talk to `SYSTem:REFMEM:OBS?` directly: it
goes through the host `ControlPlane.read` so pages share one read path with
the CLI and tests.
"""

from __future__ import annotations

import sys
from pathlib import Path


def _control_plane(transport):
    refmem = Path(__file__).resolve().parents[3] / "refmem"
    if str(refmem) not in sys.path:
        sys.path.insert(0, str(refmem))
    from control_plane import ControlPlane

    return ControlPlane(transport)


def read_switch_obs(transport, key: int, offset: int = 0, count: int = 4) -> str:
    """Read one switch key (1/2) and format the per-slot rows for the page."""
    page = _control_plane(transport).read(key, offset, count)
    text = f"key={page['key']} offset={page['offset']} count={page['count']}"
    for row in page["rows"]:
        values = ",".join(str(value) for value in row["value"])
        text += (f"; slot={row['slot']} valid={row['valid']}"
                 f" age={row['age_ms']} stale={row['stale']} value={values}")
    return text
