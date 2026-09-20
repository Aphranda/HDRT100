# VDC priority trace modules

This directory contains tracked VDC-domain implementation shared by priority
trace tools and the joint capture workflow. `probe_support.py` is an independent
pure host leaf for UID ordering,
SCPI field decoding, receipt provenance, hashing, and frozen-record summaries.
Importing it never opens serial, VISA, or other hardware.

The remaining historical Probe inheritance chain is migrated incrementally.
Archived sources remain pinned in the workflow manifest and are not deleted.
Workflow execution, STOP/restore ownership, and current migration evidence live
under `tools/workflows/vdc_joint_capture/` and `docs/tools/`.
