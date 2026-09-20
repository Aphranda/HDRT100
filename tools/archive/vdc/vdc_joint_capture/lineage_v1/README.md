# VDC joint-capture frozen lineage v1

These files are byte-for-byte copies of the historical internal-seal executable
lineage and its two required evidence inputs. They are retained for provenance,
SHA admission, compatibility selection, and clean-workspace handoff only.

Do not import or execute the Python files. The tracked implementation lives in
`tools/workflows/vdc_joint_capture/`; the workflow manifest declares an empty
runtime closure. Original `out/HardwareAcceptance/` sources remain unchanged.

The authoritative role-to-file mapping and SHA256 values are in
`config/tool_workflows/vdc_joint_capture_internal_seal.json`.

This archive does not make an old P3 receipt portable. A clean checkout must run
`python tools/hardware_acceptance/p3_hardware_acceptance.py run` first so the
receipt references a package and raw evidence produced in that workspace.
