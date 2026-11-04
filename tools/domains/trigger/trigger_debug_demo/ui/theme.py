"""Theme tokens; pages should use these instead of ad-hoc colors."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ThemeTokens:
    bg: str = "#eef2f7"
    panel: str = "#ffffff"
    ink: str = "#0f172a"
    muted: str = "#64748b"
    accent: str = "#1d4ed8"
    accent_soft: str = "#dbeafe"
    ok: str = "#15803d"
    warn: str = "#b45309"
    err: str = "#b91c1c"


DEFAULT_THEME = ThemeTokens()


def apply_tk_scaling(widget) -> None:
    """Apply the host DPI to Tk once, before widget creation."""
    try:
        widget.tk.call("tk", "scaling", widget.winfo_fpixels("1i") / 72.0)
    except Exception:
        pass

