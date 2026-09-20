# VDC joint-capture workflow inputs

This directory owns the versioned input boundary for the four-board VDC joint
capture. `inputs.py` validates the selected internal-seal archive selector and
its frozen inputs before constructing tracked runtime code or opening hardware.
`lifecycle.py` owns the tracked outer four-board serial sessions, all-board STOP
barriers, temporary RAM timing/delay application, child process boundary,
restoration, and final connection close. It never stores Flash settings.
`internal_seal.py` owns the former internal-summary, quiet-startup, and sealed
GUARD adapter layers. Their original bytes remain pinned as migrated sources;
the tracked factory delegates only to tracked runtime layers.
`runtime_base.py` owns the former monitor control and bounded parallel START
layers and delegates to tracked `run_output.py`. Historical scope, Trial and HIL
modules are retained as migrated sources but never imported.
`run_output.py` owns output timing/RUN prepare, output STOP and STOP-only RUN
validation. It rejects historical internal scope ownership; external sparse
scope remains a separate workflow component.
`phase.py` owns phase status decoding, phase/origin trace ARM, native trace
collection, endpoint consistency, and trace release. It loads only the next
tracked long-window layer; the archived phase source remains lineage evidence
and is not executed.
`longwindow.py` owns the finite-run wait, temporary origin trial, rational
decision-window oracle, and final correlated projection check. It delegates to
the tracked native trace layer; the archived long-window source remains lineage
evidence and is not executed.
`trace.py` owns trace status/ACK polling, follower trace ARM, STOP/download/decode,
coverage and last-decision consistency, and release/retain recovery. It loads
the tracked follow layer; the archived trace source remains lineage evidence and
is not executed.
`follow.py` owns signed/wide follower status decoding, independent decision
checking, follower enable/disable, STOP-only status snapshots, and model/DCO
endpoint consistency. It delegates to tracked observer and match layers; the
archived follow source remains lineage evidence and is not executed.
`observer.py` owns the four-board STOP-only TDMA event snapshots and persists
each snapshot before delegating to match collection. It loads only the next
tracked match layer; the archived observer wrapper remains lineage evidence and
is not executed.
`match.py` owns exact-match status decoding, directed path-delay reconstruction,
optional TAP install/restore, follower MATCH enable/readback, STOP-only evidence
collection, and per-follower identity/model assessment. It loads only the next
tracked direct-RX layer; the archived match source remains lineage evidence and
is not executed.
`direct.py` owns direct-RX/lane/timing/budget decoding, frozen timing-baseline
comparison, retained header/mailbox validation, and per-follower delivery
assessment. It delegates to the tracked typed-TX layer; the archived direct-RX
source remains lineage evidence and is not executed.
`typed.py` owns typed mailbox encoding validation plus the child receipt/UID/build,
configuration, START/TRIAL, final STOP, disable/restore, connection-close, and
conclusion lifecycle. It delegates to tracked base; the archived typed-TX source
remains lineage evidence and is not executed.
`base.py` owns the bounded command/action log, all-board STOP barrier, load-mask
control, TDMA/trace export helpers, pipeline snapshots, and base probe state. It
uses repository imports and the already-admitted TDMA backend owner; the archived
base source remains lineage evidence and no `out/` Python remains executable.

The CLI remains `tools/vdc_priority_trace/vdc_priority_joint_capture.py`. Its
`--workflow-inputs` JSON fixes the scenario, board UID order, the complete
migrated source archive, empty executable closure, repository-relative evidence
paths, and SHA256 values. Archive Python is checked as bytes and never imported.
The P3 receipt is checked by its schema, referenced evidence, and current source
fingerprint instead of a static digest because each accepted source revision
generates a new receipt.

The current internal-seal runtime is self-contained in tracked workflow/domain
modules. Historical lineage and baseline evidence remain mandatory pinned inputs;
missing evidence fails closed. Reference interruption, profile A/B, mirror, and
vector adapters are separate experiments and must not reuse this manifest.

Run from the repository root with Python 3.10 or newer, pyserial, NumPy, and the
declared four-board bench. `--scope on` additionally requires PyVISA, the VISA
USB backend, and the declared HDO scope; `--scope off` does not import PyVISA.

A clean checkout intentionally does not carry a reusable hardware receipt: its
tracked receipt references ignored package and raw evidence under `out/`. First
run the normal P3 workflow to build/OTA the current source, exercise the four-board
bench, and generate a current receipt plus its referenced evidence:

```powershell
python tools/hardware_acceptance/p3_hardware_acceptance.py run
```

After P3 succeeds, the same workspace can run the joint CLI directly:

```powershell
python tools/vdc_priority_trace/vdc_priority_joint_capture.py `
  --bench-adapter tools/archive/vdc/vdc_joint_capture/lineage_v1/internal_seal.py `
  --workflow-inputs config/tool_workflows/vdc_joint_capture_internal_seal.json `
  --scope on --scope-trigger CHAN1 --seconds 60 `
  --out out/HardwareAcceptance/<date>/<fresh-run>
```

Use a fresh output directory for every attempt. Manifest, SHA, receipt, or UID
failure occurs before tracked runtime construction and before hardware access.
Once the child starts, the tracked adapter owns all-board STOP, state restoration, error
collection, and connection cleanup; failed runs remain evidence and are never
overwritten.

Migration status and hardware evidence are recorded in
`docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`.
