# HDO Frozen RAW Reader

`reader.py` is the explicit transport and block-decoding dependency of
`tools.domains.vdc.scope_capture.sparse`. It has no dependency on `out/` code,
board adapters, or the legacy NORM waveform capture tool.

`decode_block(raw, expected)` is offline and validates IEEE definite-length BYTE
blocks. `Scope` opens the selected VISA resource only on construction (or accepts
an injected instrument), records commands and readbacks, and owns the scope
session it opens. The caller must close it after failures as well as success.
PyVISA and its platform backend are needed only for actual instrument access.

The generic reader retains four-channel frozen export for existing acquisition
styles. Joint sparse capture uses its raw query/decoder with the existing
650000-point prefix and asynchronous persistence, not the generic full export.
Scaling, source/range, STOP state, instrument identity and errors are checked;
opening a resource alone does not qualify it for measurement.

No device action occurs on import. Configuration belongs to the workflow; runtime
state and binary evidence belong in a fresh `out/` directory. This is an
EXPERIMENTAL registered component until the separate handoff qualification closes.
