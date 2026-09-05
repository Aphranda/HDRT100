"""Shared host-side timeout defaults for Calibration tools.

Action acknowledgement waits are intentionally separate from phase-completion
timeouts.  Keeping the defaults in one table makes the acceptance phases
predictable while still allowing a bench-specific CLI override.
"""

from __future__ import annotations

TIMEOUT_DEFAULTS_S: dict[str, float] = {
    "action_ack": 0.10,
    "serial_settle": 0.20,
    "phase_gap": 0.03,
    "topology_pair_activity": 0.50,
    "arm_completion": 3.0,
    "training_completion": 5.0,
    "storage_job_completion": 20.0,
}

DEFAULT_ACTION_TIMEOUT_S = TIMEOUT_DEFAULTS_S["action_ack"]
DEFAULT_SERIAL_SETTLE_S = TIMEOUT_DEFAULTS_S["serial_settle"]
DEFAULT_PHASE_GAP_S = TIMEOUT_DEFAULTS_S["phase_gap"]
DEFAULT_TOPOLOGY_PAIR_WAIT_S = TIMEOUT_DEFAULTS_S["topology_pair_activity"]
DEFAULT_STORAGE_JOB_TIMEOUT_S = TIMEOUT_DEFAULTS_S["storage_job_completion"]
