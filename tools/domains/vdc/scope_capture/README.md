# Sparse DPLL Scope Capture

This library provides the scope setup used by the existing joint-capture CLI:

```powershell
python tools/vdc_priority_trace/vdc_priority_joint_capture.py --help
```

`SparseScope` depends on `tools.common.hdo_scope.reader`, Python's standard
library, and PyVISA with a working VISA/USB backend when opening hardware.
Importing either module does not open hardware. Scope selection is explicit
through the CLI's `--scope-resource` and `--scope-serial`; defaults retain the
existing bench. `--scope off` never opens VISA. Only the verified HDO RAW BYTE
protocol is supported; selecting a different serial does not qualify a new model.

The CLI still requires a validated board adapter. Its board lifecycle and
historical evidence dependencies have NOT migrated in this slice. Keep using the
current validated internal-seal adapter for ordinary joint monitoring; reference
gap injection is a distinct experiment. This module does not configure boards,
change DPLL parameters, or add Core1 observation work.

Setup occurs before board START. The scope keeps the verified MAIN/OPC sequence,
RAW range reset after memory changes, and bounded readbacks. Each sparse capture
must observe a fresh WAIT followed by STOP and transfer all channels from that
same frozen record. The CLI owns trigger source and the asynchronous evidence
writer; no preceding record can stand in for a failed acquisition. CHAN1 means
NO1 OUT1. EXT requires a separately active signal, not merely a connected cable.

Artifacts remain under the caller's `--out` directory: scope setup, per-window
`scope.json`, binary blocks, phase results, and the joint report. The reader
preserves HDO's voltage formula `(code - y_origin - y_reference) * y_increment`.
Capture timestamps are host observations, not proof of exact board-event pairing
or continuously bounded phase between samples. Failure retains partial evidence;
the caller owns all-board STOP, error collection and resource cleanup.

Regression: `tests/python/test_hdo_scope_migration.py`,
`test_vdc_priority_joint_capture.py`, and `test_vdc_scope_evidence.py`.
Migration provenance, acceptance and remaining work live in
`docs/tools/DOCS_TOOLS_TASK_PROGRESS.md` and `DOCS_TOOLS_TODO.md`.
