# VDC joint-capture frozen lineage v1

These files are byte-for-byte copies of the historical internal-seal executable
lineage and its required evidence inputs. They are retained for provenance, SHA
admission, compatibility selection, and clean-workspace handoff only.

Do not import or execute the Python files. The tracked implementation lives in
`tools/workflows/vdc_joint_capture/`; the workflow manifest declares an empty
runtime closure. Original `out/HardwareAcceptance/` sources remain unchanged.

The authoritative role-to-file mapping and SHA256 values are in
`config/tool_workflows/vdc_joint_capture_internal_seal.json`.

`golden_native_report.json` and `golden_phase_report.json` are test-only frozen
hardware reports. They let the tracked parsers and verdicts be recomputed in a
clean workspace without executing archive Python or reading historical `out/`.
Their historical provenance locators and copied-byte SHA256 values are:

- `golden_native_report.json`: `out/HardwareAcceptance/20260917/dpll-longwindow-adoption-r1/native-r1/input-probe.json`, `c6ad2b8ffb5814caaf76dc9106a6dbfce37b7e907943f798f6112fe6795ed051`.
- `golden_phase_report.json`: `out/HardwareAcceptance/20260920/p3-dpll-short-r4/dpll-four-board-short/capture/input-probe.json`, `1d4c9fa2a67b6ab8e2bd13fe30c7f7d8857e6db6364acdce9d0df7998555b6d5`.

The historical paths are provenance locators, not clean-checkout dependencies.
The hashes are enforced by `tests/python/test_vdc_joint_phase.py`; neither
report is a runtime workflow input.

This archive does not make an old P3 receipt portable. A clean checkout must run
`python tools/hardware_acceptance/p3_hardware_acceptance.py run` first so the
receipt references a package and raw evidence produced in that workspace.
