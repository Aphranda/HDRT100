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


class ControlPlaneTransport:
    """Wrap the demo transport so REFMEM read queries share ControlPlane.

    Writes and non-REFTMEM commands pass through unchanged; every
    ``SYSTem:REFMEM:...?`` read goes through ``ControlPlane.query`` so the
    pages, CLI and tests use one host read path (M5).
    """

    def __init__(self, raw) -> None:
        self._raw = raw

    def execute(self, command: str) -> str:
        stripped = command.strip()
        if stripped.startswith("SYSTem:REFMEM:") and stripped.endswith("?"):
            return _control_plane(self._raw).query(stripped)
        if stripped.startswith("SYSTem:CTRL:EXECute"):
            fields = stripped.split(None, 1)
            if len(fields) == 2:
                values = [int(piece) for piece in fields[1].split(",")
                          if piece.strip()]
                plane = _control_plane(self._raw)
                seq = plane.write(values[0], values[3:], values[1], values[2])
                return f"QUEUED,{seq}"
        return self._raw.execute(command)

    def __getattr__(self, name):
        return getattr(self._raw, name)
