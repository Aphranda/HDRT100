# Archived: `p3_hardware_acceptance.py`

- **Archived**: 2026-11-04
- **Reason**: M7.5 completed the migration of the acceptance step library into
  the workflow dependency `tools/hardware_acceptance/workflow_steps.py`.
  Every active importer, the tests and `.githooks/pre-commit` now use
  `workflow_steps` (or `hardware_acceptance_workflow.py`); keeping a second
  implementation path was the remaining source of drift.
- **Replacement**: `tools/hardware_acceptance/workflow_steps.py`
  (`ACCEPTANCE_STEPS`, `AcceptanceRun`, CLI `run`/`resume`/`check-staged`).
- **Code commits**: `7410d8a4` (step29 fixes + FULL credential),
  `5e16bb8b` (M7.3 implementation move), and the M7.4 reference-sweep commit
  that accompanies this archive.
- **Status**: historical evidence only. The file is not imported by the
  repository. If an external script still imports the old path, the shim
  aliases it to `workflow_steps`; do not add new logic here.
- **Frozen sources**: `tools/archive/vdc/...` still mentions the old path in
  its pinned lineage files; those files are hash-frozen and never executed.
  `tools/scratch/` scripts are not maintained.
