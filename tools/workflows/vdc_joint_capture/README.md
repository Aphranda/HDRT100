# VDC joint-capture workflow inputs

This directory owns the versioned input boundary for the four-board VDC joint
capture. `inputs.py` validates the selected internal-seal adapter and its frozen
baseline inputs before importing executable evidence code or opening hardware.
`lifecycle.py` owns the tracked outer four-board serial sessions, all-board STOP
barriers, temporary RAM timing/delay application, child process boundary,
restoration, and final connection close. It never stores Flash settings.
`internal_seal.py` owns the former internal-summary, quiet-startup, and sealed
GUARD adapter layers. Their original bytes remain pinned as migrated sources;
only the deeper admitted runtime closure is dynamically loaded.
`runtime_base.py` owns the former monitor control and bounded parallel START
layers and loads the remaining RunProbe ancestry directly. Historical scope,
Trial and HIL modules are retained as migrated sources but no longer executed.
`run_output.py` owns output timing/RUN prepare, output STOP and STOP-only RUN
validation. It rejects historical internal scope ownership; external sparse
scope remains a separate workflow component.

The CLI remains `tools/vdc_priority_trace/vdc_priority_joint_capture.py`. Its
`--workflow-inputs` JSON fixes the scenario, board UID order, the complete
remaining historical runtime closure, migrated source archive, repository-relative
evidence paths, and SHA256 values. Every runtime executable is checked before
the runtime entrypoint is imported. The P3
receipt is intentionally checked by its schema and current source fingerprint
instead of a static digest because each accepted source revision generates a
new receipt.

The current manifest does not make the historical adapter chain supported or
self-contained in Git. Missing evidence fails closed. Reference interruption,
profile A/B, mirror, and vector adapters are separate experiments and must not
reuse the internal-seal manifest.

Run from the repository root with Python 3.10 or newer, pyserial, NumPy, and the
declared four-board bench. `--scope on` additionally requires PyVISA, the VISA
USB backend, and the declared HDO scope; `--scope off` does not import PyVISA:

```powershell
python tools/vdc_priority_trace/vdc_priority_joint_capture.py `
  --bench-adapter out/HardwareAcceptance/20260919/internal-seal-r1/capture.py `
  --workflow-inputs config/tool_workflows/vdc_joint_capture_internal_seal.json `
  --scope on --scope-trigger CHAN1 --seconds 60 `
  --out out/HardwareAcceptance/<date>/<fresh-run>
```

Use a fresh output directory for every attempt. Manifest, SHA, receipt, or UID
failure occurs before adapter import and before hardware access. Once the child
starts, the retained adapter owns all-board STOP, state restoration, error
collection, and connection cleanup; failed runs remain evidence and are never
overwritten.

Migration status and hardware evidence are recorded in
`docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`.
