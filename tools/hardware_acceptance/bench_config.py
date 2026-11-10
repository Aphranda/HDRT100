"""Bench-config loading, host I/O timing and acceptance time budget.

Extracted from the monolithic step library.  These are pure host-side helpers:
the workflow and the step implementations must resolve the same defaults and
the same ``extends`` merge order.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

try:  # package import (tests, workflow)
    from .acceptance_errors import AcceptanceError
except ImportError:  # top-level import (tools/hardware_acceptance on sys.path)
    from acceptance_errors import AcceptanceError

# The product ring is four boards, but P0T measures whoever is physically wired:
# a bench may carry 2..4 boards.  The declaration is explicit and defaults to
# "every configured board", so an unmodified bench config keeps measuring four.
RING_BOARD_COUNT_MIN = 2
RING_BOARD_COUNT_MAX = 4
# Per-node arrays are indexed by the physical ring order, so a bench that
# carries fewer boards must have them cut to the same head slice instead of
# mixing (for example) three measured boards with four configured offsets.
PER_NODE_CONFIG_KEYS = (
    "training_marker_offsets_by_node",
    "training_sck_offsets_by_node",
    "training_data_offsets_by_node",
)

DEFAULT_ACCEPTANCE_TIMING = {
    "serial_timeout_s": 3.0,
    "serial_settle_s": 0.05,
    "action_timeout_s": 0.5,
    "p3_capture_timeout_s": 3.0,
    "phase_gap_s": 0.1,
    "status_poll_interval_s": 0.05,
    "serial_read_timeout_s": 0.02,
    "board_reset_timeout_s": 15.0,
    "board_reset_poll_interval_s": 0.5,
}


def load_bench_config(path: Path, *,
                      _seen: set[Path] | None = None) -> dict[str, Any]:
    """Load one bench config with optional relative ``extends`` overlays."""
    resolved = path.resolve()
    seen = set() if _seen is None else _seen
    if resolved in seen:
        raise AcceptanceError(f"cyclic bench config extends: {resolved}")
    seen.add(resolved)
    raw = json.loads(resolved.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise AcceptanceError("hardware acceptance config must be an object")
    parent_name = raw.pop("extends", None)
    if parent_name is None:
        return raw
    if not isinstance(parent_name, str) or not parent_name:
        raise AcceptanceError("bench config extends must be a non-empty path")
    result = load_bench_config(resolved.parent / parent_name, _seen=seen)

    def merge(base: dict[str, Any], overlay: dict[str, Any]) -> dict[str, Any]:
        merged = dict(base)
        for key, value in overlay.items():
            if isinstance(value, dict) and isinstance(merged.get(key), dict):
                merged[key] = merge(merged[key], value)
            else:
                merged[key] = value
        return merged

    return merge(result, raw)


def ring_board_count(config: dict[str, Any]) -> int:
    """How many boards form the ring; default = every configured board."""
    ids = config.get("p3_board_ids_in_physical_order")
    if not isinstance(ids, list) or not ids:
        raise AcceptanceError(
            "p3_board_ids_in_physical_order must be a non-empty list")
    count = config.get("ring_board_count", len(ids))
    if isinstance(count, bool) or not isinstance(count, int):
        raise AcceptanceError(f"ring_board_count must be an integer, got {count!r}")
    if not RING_BOARD_COUNT_MIN <= count <= RING_BOARD_COUNT_MAX:
        raise AcceptanceError(
            f"ring_board_count {count} is outside the supported "
            f"{RING_BOARD_COUNT_MIN}..{RING_BOARD_COUNT_MAX} boards")
    if count > len(ids):
        raise AcceptanceError(
            f"ring_board_count {count} exceeds the {len(ids)} configured boards")
    return count


def apply_ring_board_count(config: dict[str, Any]) -> int:
    """Cut the bench config to the declared ring board count, in physical order.

    P0T derives the line order from physical activity, so the board set has to
    be exactly the boards that are wired.  Truncating every board-indexed input
    to the same head slice keeps one topology for the whole run: the board
    lists, the OTA list and each per-node training array.  The resolved count is
    written back into the config so later stages and the receipt read the same
    ring size.
    """
    count = ring_board_count(config)
    ring_ids = list(config["p3_board_ids_in_physical_order"])[:count]
    if len(set(ring_ids)) != len(ring_ids):
        raise AcceptanceError(f"ring board ids must be unique: {ring_ids}")
    anchor = config.get("topology_anchor_board_id")
    if anchor is not None and anchor not in ring_ids:
        raise AcceptanceError(
            "topology_anchor_board_id must be one of the ring boards")
    ota_ids = config.get("ota_board_ids")
    if not isinstance(ota_ids, list) or len(ota_ids) < count:
        raise AcceptanceError(
            f"ota_board_ids must carry every ring board ({count})")
    if list(ota_ids)[:count] != ring_ids:
        raise AcceptanceError(
            "ota_board_ids and p3_board_ids_in_physical_order must start with "
            "the same ring boards")
    for key in PER_NODE_CONFIG_KEYS:
        values = config.get(key)
        if values is None:
            continue
        if not isinstance(values, list) or len(values) < count:
            raise AcceptanceError(f"{key} needs at least {count} per-node entries")
        config[key] = list(values)[:count]
    # A follower-candidate gate that asks for more followers than the ring has
    # can never pass; refuse it here instead of inside the SCK training stage.
    min_followers = config.get("training_sck_min_follower_candidates")
    if (isinstance(min_followers, int) and not isinstance(min_followers, bool)
            and min_followers > count - 1):
        raise AcceptanceError(
            f"training_sck_min_follower_candidates {min_followers} needs more "
            f"than the {count - 1} followers a {count}-board ring has")
    config["ring_board_count"] = count
    config["p3_board_ids_in_physical_order"] = ring_ids
    config["ota_board_ids"] = list(ota_ids)[:count]
    return count


def acceptance_timing(config: dict[str, Any]) -> dict[str, float]:
    """Resolve one explicit host I/O timing contract for every phase."""
    raw = config.get("hardware_acceptance_timing", {})
    if not isinstance(raw, dict):
        raise AcceptanceError("hardware_acceptance_timing must be an object")
    result: dict[str, float] = {}
    for name, default in DEFAULT_ACCEPTANCE_TIMING.items():
        value = raw.get(name, default)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise AcceptanceError(f"invalid acceptance timing {name}: {value!r}")
        value = float(value)
        if value <= 0.0:
            raise AcceptanceError(f"acceptance timing {name} must be > 0")
        result[name] = value
    # Preserve the existing tool-facing names while recording the contract in
    # terms of direction.  New configs may use the explicit names; legacy
    # configs continue to map serial_settle/phase_gap to them.
    for alias, legacy_name in (("input_settle_s", "serial_settle_s"),
                               ("output_handoff_s", "phase_gap_s")):
        value = raw.get(alias, result[legacy_name])
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise AcceptanceError(f"invalid acceptance timing {alias}: {value!r}")
        value = float(value)
        if value <= 0.0:
            raise AcceptanceError(f"acceptance timing {alias} must be > 0")
        result[alias] = value
        result[legacy_name] = value
    if result["serial_read_timeout_s"] > result["serial_timeout_s"]:
        raise AcceptanceError(
            "serial_read_timeout_s must not exceed serial_timeout_s")
    if result["action_timeout_s"] > result["serial_timeout_s"]:
        raise AcceptanceError("action_timeout_s must not exceed serial_timeout_s")
    return result


def acceptance_budget_status(config: dict[str, Any], elapsed_s: float) -> dict[str, Any]:
    """Classify total acceptance duration without weakening any gate."""
    raw = config.get("acceptance_time_budget", {})
    warning_s = float(raw.get("warning_s", 60.0))
    error_s = float(raw.get("error_s", 100.0))
    if warning_s <= 0 or error_s <= warning_s:
        raise AcceptanceError(
            "acceptance_time_budget must satisfy 0 < warning_s < error_s")
    status = "WARN" if elapsed_s > warning_s else "PASS"
    return {"status": status, "elapsed_s": round(elapsed_s, 3),
            "warning_s": warning_s, "error_s": error_s,
            "over_error_budget": elapsed_s > error_s}
