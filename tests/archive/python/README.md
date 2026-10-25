# Archived host harnesses

These Python tests built a synthetic translation unit by concatenating the old
`components/vdc_dpll_manager/src/*.inc` fragments. The 2026-10-25 split gave
every owner fragment a real translation unit, so the fragments no longer exist
and these harnesses can no longer be assembled.

They are replaced by `tests/harness` (CMake + Pico SDK host platform), which
compiles the production units unmodified, plus `tests/python/test_vdc_host_harness.py`
that runs the scenario binaries.

| Archived file | Replaced by |
|---|---|
| `test_vdc_reference_discipline.py` | `tests/harness/scenarios/reference_discipline_scenario.c` (same 28 modes: track, negative, slow, freeze, limits, bypass, filter_extreme, stop, origin_epoch, no_origin, stale_origin, role, session, epoch, run, config, profile, servo, timeout, generation, range, reject, cancel, disabled, rearm, duplicate, stale, old_window) |
